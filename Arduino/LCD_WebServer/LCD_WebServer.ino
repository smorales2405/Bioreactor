#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_ADS1X15.h>
#include <DFRobot_ESP_PH_WITH_ADC.h>
#include <EEPROM.h>
#include <PCF8574.h>

// === Configuración WiFi ===
const char* ap_ssid = "ESP32-Bioreactor";
const char* ap_password = "bioreactor2025";

// === LCD 20x4 ===
LiquidCrystal_I2C lcd(0x27, 20, 4);

// === RTC DS3231 ===
RTC_DS3231 rtc;

// === Sensores ===
Adafruit_MAX31865 thermo = Adafruit_MAX31865(5, 23, 19, 18); // CS, MOSI, MISO, CLK
Adafruit_ADS1115 ads;
DFRobot_ESP_PH_WITH_ADC phSensor;

#define RREF      430.0
#define RNOMINAL  100.0

// === Pines del encoder ===
#define ENCODER_CLK 34
#define ENCODER_DT  35
#define ENCODER_SW  32

// === Pin del Boton Emergencia ===
#define EMERGENCY_PIN 39

// === Pin SQW del RTC ===
#define SQW_PIN 17

// Flujometro
#define FLOW_SENSOR_PIN 36
#define EEPROM_VOLUME_ADDR 4  // Dirección para guardar volumen
#define K_FACTOR 7.5
#define PULSOS_POR_LITRO (K_FACTOR * 60)

// Variables de flujo
volatile unsigned long pulseCount = 0;
float volumeTotal = 0.0;
float volumeLlenado = 0.0;
float targetVolume = 0.0;
bool fillingActive = false;
unsigned long lastFlowCheck = 0;

//Aireacion
bool aireacionActive = false;
bool co2Active = false;

bool sequenceLoopMode = false;

volatile bool emergencyActive = false;
unsigned long lastEmergencyCheck = 0;

// === Pines PWM LEDS ===
const int ledPins[] = {4, 26, 16, 25};
const char* ledNames[] = {"Blanco", "Rojo", "Verde", "Azul"};
const char* ledNamesWeb[] = {"blanco", "rojo", "verde", "azul"};
const int numLeds = 4;

// === microSD SPI ===
SPIClass sdSPI(VSPI);
#define SD_CS   27
#define SD_MOSI 13
#define SD_MISO 12
#define SD_SCK  14

// === Variables de estado ===
int selectedLed = 0;
int selectedAction = 0;
int pwmValues[4] = {0, 0, 0, 0};
bool ledStates[4] = {false, false, false, false};

// === Variables del encoder ===
int lastClk = HIGH;
unsigned long lastButtonPress = 0;
unsigned long lastExtraButtonPress = 0;
const unsigned long debounceDelay = 200;

// Instancias PCF8574
PCF8574 pcfInput(0x20);
PCF8574 pcfOutput(0x21);

// === Estados del menú ===
enum MenuState {
  MENU_MAIN,           
  MENU_SENSORS,        
  MENU_SENSOR_PH,      
  MENU_PH_CALIBRATION_SELECT, // Nuevo: selección buffer calibración
  MENU_PH_CALIBRATION_4,      // Nuevo: calibración pH 4
  MENU_PH_CALIBRATION_7,      // Nuevo: calibración pH 7
  MENU_PH_CALIBRATION_10,     // Nuevo: calibración pH 10
  MENU_PH_PANEL,              // Panel principal de pH
  MENU_PH_SET_LIMIT,          // Configurar pH límite
  MENU_PH_MANUAL_CO2,         // Inyección manual de CO2
  MENU_PH_MANUAL_CO2_CONFIRM, // Confirmar inyección
  MENU_PH_MANUAL_CO2_ACTIVE,  // CO2 activo
  MENU_ACTION,         
  MENU_LED_SELECT,
  MENU_ONOFF,
  MENU_INTENSITY,
  MENU_SEQ_LIST,
  MENU_SEQ_OPTIONS,
  MENU_SEQ_CONFIG_CANTIDAD,
  MENU_SEQ_CONFIG_COLOR,
  MENU_SEQ_CONFIG_TIME,
  MENU_SEQ_CONFIG_TIME_CONFIRM, 
  MENU_SEQ_EXECUTION_MODE,       
  MENU_SEQ_DELETE_ALL_CONFIRM,  
  MENU_SEQ_CONFIRM_SAVE,
  MENU_SEQ_RUNNING,
  MENU_SEQ_STOP_CONFIRM,
  MENU_SEQ_EXIT_CONFIG_CONFIRM,
  MENU_LLENADO,
  MENU_LLENADO_SET_VOLUME,
  MENU_LLENADO_CONFIRM,
  MENU_LLENADO_ACTIVE,
  MENU_LLENADO_STOP_CONFIRM,
  MENU_LLENADO_RESET_CONFIRM,
  MENU_AIREACION,
  MENU_CO2,
  MENU_WEBSERVER
};

MenuState currentMenu = MENU_MAIN;
int menuCursor = 0;

// === Variables de sensores ===
float temperature = 0.0;
float phValue = 0.0;
float turbidez = 0.0;
unsigned long lastSensorRead = 0;
const unsigned long sensorReadInterval = 1000;

// === Variables para calibración pH ===
float calibrationValue = 0.0;
int calibrationStep = 0;

float phLimitSet = 7.0;      // pH límite establecido
bool phControlActive = false; // Control automático activo
bool co2InjectionActive = false; // Inyección manual activa
int co2MinutesSet = 0;       // Minutos de CO2 a inyectar
int co2MinutesRemaining = 0; // Minutos restantes
unsigned long co2StartTime = 0;

// === Variables para las secuencias ===
struct SequenceStep {
  int colorIntensity[4];
  int hours;    
  int minutes;  
  int seconds;  
};

struct Sequence {
  bool configured;
  int stepCount;
  SequenceStep steps[10];
};

Sequence sequences[10];
int selectedSequence = 0;
int currentConfigStep = 0;
int currentColorConfig = 0;
bool sequenceRunning = false;
int currentSequenceStep = 0;

// === Variables de tiempo para la secuencia ===
DateTime sequenceStartTime;
DateTime stepStartTime;
volatile bool rtcInterrupt = false;

// === Configuración PWM ===
const int pwmFreq = 5000;
const int pwmResolution = 8;

// === Servidor Web ===
AsyncWebServer server(80);

// === Función de interrupción del RTC ===
void IRAM_ATTR onRTCInterrupt() {
  rtcInterrupt = true;
}

// Función de interrupción para el sensor de flujo
void IRAM_ATTR contarPulso() {
  pulseCount++;
}

void IRAM_ATTR handleEmergency() {
  emergencyActive = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando sistema integrado...");
  
  // Inicializar EEPROM
  EEPROM.begin(32);

  EEPROM.get(EEPROM_VOLUME_ADDR, volumeTotal);
  if (isnan(volumeTotal) || volumeTotal < 0 || volumeTotal > 1000) {
  volumeTotal = 0.0;
  }
  
  // Inicializar I2C y LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("Sistema Integrado");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");
  
  // Configurar pines
  pinMode(ENCODER_CLK, INPUT);
  pinMode(ENCODER_DT, INPUT);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(SQW_PIN, INPUT_PULLUP);
  pinMode(FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), contarPulso, FALLING);
  pinMode(EMERGENCY_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_PIN), handleEmergency, RISING);

  // Configurar PCF8574
  pcfInput.pinMode(P0, INPUT);
  pcfInput.pinMode(P1, INPUT);
  pcfInput.begin();

  pcfOutput.pinMode(P0, OUTPUT);  // Aireación
  pcfOutput.pinMode(P1, OUTPUT);  // Bomba de agua
  pcfOutput.pinMode(P2, OUTPUT);  // Solenoide
  pcfOutput.pinMode(P3, OUTPUT);  // LEd
  pcfOutput.digitalWrite(P0, HIGH);
  pcfOutput.digitalWrite(P1, HIGH);
  pcfOutput.digitalWrite(P2, HIGH);
  pcfOutput.digitalWrite(P3, HIGH);
  pcfOutput.begin();

  // Configurar PWM para cada LED
  for (int i = 0; i < numLeds; i++) {
    ledcAttach(ledPins[i], pwmFreq, pwmResolution);
    ledcWrite(ledPins[i], 0);
  }
  
  // Inicializar RTC
  if (!rtc.begin()) {
    Serial.println("RTC no detectado!");
    lcd.setCursor(0, 2);
    lcd.print("RTC Error!");
  } else {
    Serial.println("RTC OK");
    lcd.setCursor(0, 2);
    lcd.print("RTC OK");
    
    if (rtc.lostPower()) {
      Serial.println("RTC sin energía, configurando hora...");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    
    rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
    attachInterrupt(digitalPinToInterrupt(SQW_PIN), onRTCInterrupt, FALLING);
  }
  
  // Inicializar sensores
  //lcd.setCursor(10, 2);
  //lcd.print("Sensores...");
  
  // MAX31865
  thermo.begin(MAX31865_3WIRE);
  
  // pH Sensor
  phSensor.begin();
  
  // ADS1115
  ads.setGain(GAIN_TWOTHIRDS);
  if (!ads.begin()) {
    Serial.println("Error: No se detectó el ADS1115.");
    //lcd.setCursor(10, 2);
    //lcd.print("ADS Error!");
  } //else {
  //  lcd.setCursor(10, 2);
  //  lcd.print("OK");
  //}
  
  delay(5000);

  // Inicializar SD
  lcd.setCursor(0, 3);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("Error al inicializar SD");
    lcd.print("SD Card Error!");
  } else {
    Serial.println("SD inicializada correctamente");
    lcd.print("SD Card OK");
    
    // Cargar secuencias desde SD
    loadAllSequences();
  }
  
  // Inicializar secuencias en memoria
  for (int i = 0; i < 10; i++) {
    if (!sequences[i].configured) {
      sequences[i].configured = false;
      sequences[i].stepCount = 0;
      for (int j = 0; j < 10; j++) {
        for (int k = 0; k < 4; k++) {
          sequences[i].steps[j].colorIntensity[k] = 0;
        }
        sequences[i].steps[j].hours = 0;
        sequences[i].steps[j].minutes = 0;  
        sequences[i].steps[j].seconds = 0;  
      }
    }
  }
  
  delay(1000);
  
  // Conectar WiFi
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Configurando WiFi");
  setupWiFi();
  
  // Configurar servidor web
  setupWebServer();
  
  // Iniciar servidor
  server.begin();
  
  delay(1000);
  
  // Mostrar menú inicial
  displayMainMenu();
}

void loop() {

  handleEmergencyState();

  // Si hay emergencia, no procesar nada más
  if (emergencyActive) {
    delay(100);  // Pequeña pausa para no saturar
    return;
  }  

  handleEncoder();
  handleButtons();
  
  
  // Leer sensores periódicamente
  if (millis() - lastSensorRead > sensorReadInterval) {
    lastSensorRead = millis();
    readSensors();
    
    // Actualizar display si estamos en el menú de sensores
    if (currentMenu == MENU_SENSORS) {
      updateDisplay();
    }
  }
  
  // Actualizar medición de flujo continuamente
  updateFlowMeasurement();
  updateCO2Time();
  checkPhControl();
  
  // Si la secuencia está ejecutándose, verificar el tiempo
  if (sequenceRunning && rtcInterrupt) {
    rtcInterrupt = false;
    checkSequenceProgress();
    updateDisplay();
  }

  if (currentMenu == MENU_PH_MANUAL_CO2_ACTIVE && millis() - lastSensorRead > 1000) {
  updateDisplay();
  }

}

void setupWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Creando WiFi AP...");
  
  // Configurar ESP32 como Access Point
  WiFi.mode(WIFI_AP);
  
  // Configurar canal y máximo de conexiones
  const int channel = 1;          // Canal WiFi (1-13)
  const int max_connection = 4;   // Máximo 4 dispositivos conectados
  const bool hidden = false;      // false = red visible, true = red oculta

  // Configurar IP estática para el AP (opcional)
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(local_IP, gateway, subnet);

  // Crear AP con configuración completa
  bool result = WiFi.softAP(ap_ssid, ap_password, channel, hidden, max_connection);
  
  if (result) {
    
    lcd.setCursor(0, 1);
    lcd.print("WiFi AP OK");
    lcd.setCursor(0, 2);
    lcd.print("Red: ");
    lcd.print(ap_ssid);
    lcd.setCursor(0, 3);
    lcd.print("IP: ");
    lcd.print(WiFi.softAPIP());
    delay(3000);
  } else {
    Serial.println("\nError al crear AP");
    lcd.setCursor(0, 1);
    lcd.print("WiFi AP Error!");
    delay(2000);
  }
}

void setupWebServer() {
  // Servir archivos estáticos desde la SD
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/index.html", "text/html");
  });
  
  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/styles.css", "text/css");
  });
  
  // API para sensores
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"ph\":" + String(phValue, 2) + ",";
    json += "\"turbidity\":" + String(turbidez, 0);
    json += "}";
    request->send(200, "application/json", json);
  });
  
// Control de LEDs individuales - ON/OFF
server.on("/led/blanco/on", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(0, true, 100);
  request->send(200, "text/plain", "OK");
});

server.on("/led/blanco/off", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(0, false, 0);
  request->send(200, "text/plain", "OK");
});

server.on("/led/rojo/on", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(1, true, 100);
  request->send(200, "text/plain", "OK");
});

server.on("/led/rojo/off", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(1, false, 0);
  request->send(200, "text/plain", "OK");
});

server.on("/led/verde/on", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(2, true, 100);
  request->send(200, "text/plain", "OK");
});

server.on("/led/verde/off", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(2, false, 0);
  request->send(200, "text/plain", "OK");
});

server.on("/led/azul/on", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(3, true, 100);
  request->send(200, "text/plain", "OK");
});

server.on("/led/azul/off", HTTP_GET, [](AsyncWebServerRequest *request){
  setLED(3, false, 0);
  request->send(200, "text/plain", "OK");
});

  // Control PWM individual para cada color
server.on("/led/blanco/pwm/*", HTTP_GET, [](AsyncWebServerRequest *request){
  String path = request->url();
  int value = path.substring(path.lastIndexOf('/') + 1).toInt();
  if (value >= 0 && value <= 100) {
    setLED(0, value > 0, value);
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid value");
  }
});

server.on("/led/rojo/pwm/*", HTTP_GET, [](AsyncWebServerRequest *request){
  String path = request->url();
  int value = path.substring(path.lastIndexOf('/') + 1).toInt();
  if (value >= 0 && value <= 100) {
    setLED(1, value > 0, value);
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid value");
  }
});

server.on("/led/verde/pwm/*", HTTP_GET, [](AsyncWebServerRequest *request){
  String path = request->url();
  int value = path.substring(path.lastIndexOf('/') + 1).toInt();
  if (value >= 0 && value <= 100) {
    setLED(2, value > 0, value);
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid value");
  }
});

server.on("/led/azul/pwm/*", HTTP_GET, [](AsyncWebServerRequest *request){
  String path = request->url();
  int value = path.substring(path.lastIndexOf('/') + 1).toInt();
  if (value >= 0 && value <= 100) {
    setLED(3, value > 0, value);
    request->send(200, "text/plain", "OK");
  } else {
    request->send(400, "text/plain", "Invalid value");
  }
});
  
  // Estado de todos los LEDs
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    for (int i = 0; i < numLeds; i++) {
      json += "\"" + String(ledNamesWeb[i]) + "\":{";
      json += "\"state\":" + String(ledStates[i] ? "true" : "false") + ",";
      json += "\"intensity\":" + String(pwmValues[i] * 10); // Convertir de 0-10 a 0-100
      json += "}";
      if (i < numLeds - 1) json += ",";
    }
    json += "}";
    request->send(200, "application/json", json);
  });
  
  // API para secuencias
  server.on("/api/sequences", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "[";
    for (int i = 0; i < 10; i++) {
      json += "{";
      json += "\"id\":" + String(i) + ",";
      json += "\"configured\":" + String(sequences[i].configured ? "true" : "false") + ",";
      json += "\"steps\":" + String(sequences[i].stepCount);
      json += "}";
      if (i < 9) json += ",";
    }
    json += "]";
    request->send(200, "application/json", json);
  });
  
  // Detalles de una secuencia
  server.on("/api/sequence/*", HTTP_GET, [](AsyncWebServerRequest *request){
    String path = request->url();
    int seqId = path.substring(path.lastIndexOf('/') + 1).toInt();
    
    if (seqId >= 0 && seqId < 10) {
      String json = "{";
      json += "\"id\":" + String(seqId) + ",";
      json += "\"configured\":" + String(sequences[seqId].configured ? "true" : "false") + ",";
      json += "\"steps\":[";
      
      for (int i = 0; i < sequences[seqId].stepCount; i++) {
        json += "{";
        json += "\"colors\":[";
        for (int j = 0; j < 4; j++) {
          json += String(sequences[seqId].steps[i].colorIntensity[j]);
          if (j < 3) json += ",";
        }
        json += "],";
        json += "\"hours\":" + String(sequences[seqId].steps[i].hours) + ",";
        json += "\"minutes\":" + String(sequences[seqId].steps[i].minutes) + ","; 
        json += "\"seconds\":" + String(sequences[seqId].steps[i].seconds);   
        json += "}";
        if (i < sequences[seqId].stepCount - 1) json += ",";
      }
      
      json += "]}";
      request->send(200, "application/json", json);
    } else {
      request->send(400, "text/plain", "Invalid sequence ID");
    }
  });
  
  // Iniciar secuencia
  server.on("/api/sequence/*/start", HTTP_POST, [](AsyncWebServerRequest *request){
    String path = request->url();
    int startPos = path.indexOf("/sequence/") + 10;
    int endPos = path.indexOf("/start");
    int seqId = path.substring(startPos, endPos).toInt();
    
    if (seqId >= 0 && seqId < 10 && sequences[seqId].configured) {
      selectedSequence = seqId;
      startSequence();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Invalid sequence or not configured");
    }
  });
  
  // Detener secuencia
  server.on("/api/sequence/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    if (sequenceRunning) {
      stopSequence();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "No sequence running");
    }
  });
  
  // Estado de secuencia actual
  server.on("/api/sequence/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"running\":" + String(sequenceRunning ? "true" : "false");
    if (sequenceRunning) {
      json += ",\"sequenceId\":" + String(selectedSequence);
      json += ",\"currentStep\":" + String(currentSequenceStep);
      json += ",\"totalSteps\":" + String(sequences[selectedSequence].stepCount);
      
      // Calcular tiempo transcurrido
      DateTime now = rtc.now();
      TimeSpan elapsed = now - stepStartTime;
      int elapsedTotalSeconds = elapsed.days() * 86400 + elapsed.hours() * 3600 + 
                              elapsed.minutes() * 60 + elapsed.seconds();
      int elapsedHours = elapsedTotalSeconds / 3600;
      int elapsedMinutes = (elapsedTotalSeconds % 3600) / 60;
      int elapsedSeconds = elapsedTotalSeconds % 60;

      json += ",\"elapsedHours\":" + String(elapsedHours);
      json += ",\"elapsedMinutes\":" + String(elapsedMinutes);
      json += ",\"elapsedSeconds\":" + String(elapsedSeconds);
      json += ",\"totalHours\":" + String(sequences[selectedSequence].steps[currentSequenceStep].hours);
      json += ",\"totalMinutes\":" + String(sequences[selectedSequence].steps[currentSequenceStep].minutes);
      json += ",\"totalSeconds\":" + String(sequences[selectedSequence].steps[currentSequenceStep].seconds);
    }
    json += "}";
    request->send(200, "application/json", json);
  });
  
  // Guardar secuencia (recibe JSON)
  server.on("/api/sequence/save", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      static String body = "";
      
      if (index == 0) {
        body = "";
      }
      
      for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
      }
      
      if (index + len == total) {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, body);
        
        if (!error) {
          int seqId = doc["id"];
          if (seqId >= 0 && seqId < 10) {
            sequences[seqId].configured = true;
            sequences[seqId].stepCount = doc["steps"].size();
            
            JsonArray steps = doc["steps"];
            for (int i = 0; i < sequences[seqId].stepCount && i < 10; i++) {
              JsonObject step = steps[i];
              JsonArray colors = step["colors"];
              
              for (int j = 0; j < 4; j++) {
                sequences[seqId].steps[i].colorIntensity[j] = colors[j];
              }
              
              sequences[seqId].steps[i].hours = step["hours"];
              sequences[seqId].steps[i].minutes = step["minutes"];
              sequences[seqId].steps[i].seconds = step["seconds"];
            }
            
            saveSequence(seqId);
            request->send(200, "text/plain", "OK");
          } else {
            request->send(400, "text/plain", "Invalid sequence ID");
          }
        } else {
          request->send(400, "text/plain", "Invalid JSON");
        }
        
        body = "";
      }
    });

    // Control de Aireación
  server.on("/api/aireacion/on", HTTP_GET, [](AsyncWebServerRequest *request){
    aireacionActive = true;
    pcfOutput.digitalWrite(P1, LOW);
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/aireacion/off", HTTP_GET, [](AsyncWebServerRequest *request){
    aireacionActive = false;
    pcfOutput.digitalWrite(P1, HIGH);
    request->send(200, "text/plain", "OK");
  });

  // Control de CO2
  server.on("/api/co2/on", HTTP_GET, [](AsyncWebServerRequest *request){
    co2Active = true;
    pcfOutput.digitalWrite(P2, LOW);
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/co2/off", HTTP_GET, [](AsyncWebServerRequest *request){
    co2Active = false;
    pcfOutput.digitalWrite(P2, HIGH);
    request->send(200, "text/plain", "OK");
  });

  // Estado de aireación y CO2
  server.on("/api/aireacion/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"aireacion\":" + String(aireacionActive ? "true" : "false") + ",";
    json += "\"co2\":" + String(co2Active ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });
    
    // Control de Llenado
  server.on("/api/llenado/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"volumeTotal\":" + String(volumeTotal, 1) + ",";
    json += "\"volumeLlenado\":" + String(volumeLlenado, 1) + ",";
    json += "\"targetVolume\":" + String(targetVolume, 0) + ",";
    json += "\"fillingActive\":" + String(fillingActive ? "true" : "false") + ",";
    json += "\"isManualMode\":" + String(targetVolume >= 9999 ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Reiniciar volumen
  server.on("/api/llenado/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    volumeTotal = 0.0;
    pulseCount = 0;
    saveVolumeToEEPROM();
    request->send(200, "text/plain", "OK");
  });

  // Iniciar llenado con volumen específico
  server.on("/api/llenado/start", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("volume", /*post=*/true)) {
      const AsyncWebParameter* p = request->getParam("volume", /*post=*/true);
      const String s = p->value();
      const float vol = s.toFloat();

      if (vol > 0 && vol <= 200) {
        targetVolume = vol;
        startFilling();
        request->send(200, "text/plain", "OK");
      } else {
        request->send(400, "text/plain", "Invalid volume");
      }
    } else {
      request->send(400, "text/plain", "Missing volume parameter");
    }
  });


  // Iniciar bomba manual
  server.on("/api/llenado/manual/start", HTTP_POST, [](AsyncWebServerRequest *request){
    fillingActive = true;
    volumeLlenado = 0.0;
    targetVolume = 9999;
    pcfOutput.digitalWrite(P0, HIGH);
    request->send(200, "text/plain", "OK");
  });

  // Detener llenado/bomba
  server.on("/api/llenado/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    stopFilling();
    request->send(200, "text/plain", "OK");
  });

  // Manejo de errores 404
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });
}

void readSensors() {
  // Leer temperatura
  temperature = thermo.temperature(RNOMINAL, RREF);
  
  // Leer turbidez (AIN0)
  int16_t raw_turbidez = ads.readADC_SingleEnded(0);
  float voltage_turbidez = ads.computeVolts(raw_turbidez);
  turbidez = -1120.4 * voltage_turbidez * voltage_turbidez + 5742.3 * voltage_turbidez - 4352.9;
  if (turbidez < 0) turbidez = 0;
  if (turbidez > 3000) turbidez = 3000;
  
  // Leer pH (AIN1)
  int16_t raw_ph = ads.readADC_SingleEnded(1) / 10;
  phValue = phSensor.readPH(raw_ph, temperature);
}

void handlePhCalibration(float targetPH) {
  static bool isCalibrating = false;
  if (isCalibrating) return; // Evitar reentrada
  isCalibrating = true;
  
  int16_t raw_ph = ads.readADC_SingleEnded(1) / 10;
  float voltage_ph = ads.computeVolts(raw_ph);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrando pH ");
  lcd.print(targetPH, 1);
  
  lcd.setCursor(0, 1);
  lcd.print("Voltage: ");
  lcd.print(voltage_ph, 3);
  lcd.print("V");
  
  lcd.setCursor(0, 2);
  lcd.print("pH actual: ");
  lcd.print(phValue, 2);
  
  lcd.setCursor(0, 3);
  lcd.print("OK:Calibrar ESC:Salir");
  
  // Esperar confirmación
  while (true) {
    if (digitalRead(ENCODER_SW) == LOW) {
      delay(debounceDelay);
      // Calibrar con el valor objetivo
      phSensor.calibration(voltage_ph, temperature, (char*)(targetPH == 7.0 ? "CALPH" : 
                                                               targetPH == 4.0 ? "CALPH4" : "CALPH10"));
      
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("Calibracion OK!");
      delay(2000);
      
      currentMenu = MENU_PH_CALIBRATION_SELECT;
      menuCursor = targetPH == 4.0 ? 0 : targetPH == 7.0 ? 1 : 2;
      isCalibrating = false;
      updateDisplay();
      return;
    }
    
    if (pcfInput.digitalRead(P1) == LOW) {
      delay(debounceDelay);
      currentMenu = MENU_PH_CALIBRATION_SELECT;
      isCalibrating = false;
      updateDisplay();
      return;
    }
    
    // Actualizar lecturas
    if (millis() - lastSensorRead > 500) {
      lastSensorRead = millis();
      raw_ph = ads.readADC_SingleEnded(1) / 10;
      voltage_ph = ads.computeVolts(raw_ph);
      phValue = phSensor.readPH(raw_ph, temperature);
      
      lcd.setCursor(9, 1);
      lcd.print(voltage_ph, 3);
      lcd.print("V ");
      
      lcd.setCursor(11, 2);
      lcd.print(phValue, 2);
      lcd.print(" ");
    }
  }
}

void handleEncoder() {
  int clkState = digitalRead(ENCODER_CLK);
  
  if (clkState != lastClk && clkState == LOW) {
    if (digitalRead(ENCODER_DT) != clkState) {
      incrementCursor();
    } else {
      decrementCursor();
    }
    updateDisplay();
  }
  
  lastClk = clkState;
}

void handleButtons() {

  if (emergencyActive) return;

  // Botón del encoder
  if (digitalRead(ENCODER_SW) == LOW) {
    if (millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis();
      handleSelection();
    }
  }
  
  // Pulsador adicional
  if (pcfInput.digitalRead(P1) == LOW) {
    if (millis() - lastExtraButtonPress > debounceDelay) {
      lastExtraButtonPress = millis();
      handleExtraButton();
    }
  }
}

void handleExtraButton() {
  switch (currentMenu) {
    // Menús principales - volver al menú anterior
    case MENU_SENSORS:
      currentMenu = MENU_MAIN;
      menuCursor = 0;
      updateDisplay();
      break;
      
    case MENU_ACTION:
      currentMenu = MENU_MAIN;
      menuCursor = 1;
      updateDisplay();
      break;
      
    case MENU_LLENADO:
      currentMenu = MENU_MAIN;
      menuCursor = 2;
      updateDisplay();
      break;
      
    case MENU_AIREACION:
      currentMenu = MENU_MAIN;
      menuCursor = 3;
      updateDisplay();
      break;
      
    case MENU_CO2:
      currentMenu = MENU_MAIN;
      menuCursor = 4;
      updateDisplay();
      break;
      
    case MENU_WEBSERVER:
      currentMenu = MENU_MAIN;
      menuCursor = 5;
      updateDisplay();
      break;
      
    // Submenús de sensores
    case MENU_SENSOR_PH:
      currentMenu = MENU_SENSORS;
      menuCursor = 1;
      updateDisplay();
      break;
      
    case MENU_PH_CALIBRATION_SELECT:
      currentMenu = MENU_SENSOR_PH;
      menuCursor = 0;
      updateDisplay();
      break;
      
    // Submenús de LEDs
    case MENU_LED_SELECT:
      currentMenu = MENU_ACTION;
      menuCursor = selectedAction;
      updateDisplay();
      break;
      
    case MENU_ONOFF:
    case MENU_INTENSITY:
      currentMenu = MENU_LED_SELECT;
      menuCursor = selectedLed;
      updateDisplay();
      break;
      
    // Menús de secuencias
    case MENU_SEQ_LIST:
      currentMenu = MENU_ACTION;
      menuCursor = 2;
      updateDisplay();
      break;
      
    case MENU_SEQ_OPTIONS:
      currentMenu = MENU_SEQ_LIST;
      menuCursor = selectedSequence;
      updateDisplay();
      break;
      
    case MENU_SEQ_EXECUTION_MODE:
      currentMenu = MENU_SEQ_OPTIONS;
      menuCursor = 0;
      updateDisplay();
      break;
      
    // Configuración de secuencias - mostrar confirmación de salida
    case MENU_SEQ_CONFIG_CANTIDAD:
    case MENU_SEQ_CONFIG_COLOR:
    case MENU_SEQ_CONFIG_TIME:
    case MENU_SEQ_CONFIG_TIME_CONFIRM:
      currentMenu = MENU_SEQ_EXIT_CONFIG_CONFIRM;
      menuCursor = 1;
      updateDisplay();
      break;
      
    // Secuencia en ejecución - mostrar confirmación
    case MENU_SEQ_RUNNING:
      currentMenu = MENU_SEQ_STOP_CONFIRM;
      menuCursor = 1;
      updateDisplay();
      break;
      
    // Menús de llenado
    case MENU_LLENADO_SET_VOLUME:
      currentMenu = MENU_LLENADO;
      menuCursor = 1;
      updateDisplay();
      break;
      
    case MENU_LLENADO_CONFIRM:
      currentMenu = MENU_LLENADO;
      menuCursor = 1;
      updateDisplay();
      break;
      
    case MENU_LLENADO_ACTIVE:
      currentMenu = MENU_LLENADO_STOP_CONFIRM;
      menuCursor = 1;
      updateDisplay();
      break;
      
    // Confirmaciones - actuar como "NO/Cancelar"
    case MENU_SEQ_CONFIRM_SAVE:
      currentMenu = MENU_SEQ_LIST;
      menuCursor = selectedSequence;
      updateDisplay();
      break;
      
    case MENU_SEQ_STOP_CONFIRM:
      currentMenu = MENU_SEQ_RUNNING;
      updateDisplay();
      break;
      
    case MENU_SEQ_EXIT_CONFIG_CONFIRM:
      if (currentConfigStep < sequences[selectedSequence].stepCount) {
        if (currentColorConfig < 4) {
          currentMenu = MENU_SEQ_CONFIG_COLOR;
        } else {
          currentMenu = MENU_SEQ_CONFIG_TIME;
        }
      } else {
        currentMenu = MENU_SEQ_CONFIG_CANTIDAD;
      }
      updateDisplay();
      break;
      
    case MENU_LLENADO_STOP_CONFIRM:
      currentMenu = MENU_LLENADO_ACTIVE;
      updateDisplay();
      break;
      
    case MENU_LLENADO_RESET_CONFIRM:
      currentMenu = MENU_LLENADO;
      menuCursor = 0;
      updateDisplay();
      break;
      
    case MENU_SEQ_DELETE_ALL_CONFIRM:
      currentMenu = MENU_SEQ_LIST;
      menuCursor = 10;
      updateDisplay();
      break;
      
    // Calibración pH - casos especiales
    case MENU_PH_CALIBRATION_4:
    case MENU_PH_CALIBRATION_7:
    case MENU_PH_CALIBRATION_10:
      currentMenu = MENU_PH_CALIBRATION_SELECT;
      updateDisplay();
      break;

    case MENU_PH_PANEL:
      currentMenu = MENU_SENSOR_PH;
      menuCursor = 0;
      updateDisplay();
      break;

    case MENU_PH_SET_LIMIT:
      phControlActive = false; // Desactivar control si se cancela
      currentMenu = MENU_PH_PANEL;
      menuCursor = 0;
      updateDisplay();
      break;

    case MENU_PH_MANUAL_CO2:
      currentMenu = MENU_PH_PANEL;
      menuCursor = 1;
      updateDisplay();
      break;

    case MENU_PH_MANUAL_CO2_ACTIVE:
      stopCO2Injection();
      currentMenu = MENU_PH_PANEL;
      menuCursor = 1;
      updateDisplay();
      break;

    // Menú principal - no hacer nada
    case MENU_MAIN:
      // Ya estamos en el menú principal
      break;
      
    default:
      // Para cualquier otro caso no manejado
      break;
  }
}

void incrementCursor() {
  switch (currentMenu) {
    case MENU_MAIN:
      menuCursor++;
      if (menuCursor > 5) menuCursor = 0; // Ahora tenemos 3 opciones
      break;
      
    case MENU_SENSORS:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0;
      break;
      
    case MENU_SENSOR_PH:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // Fijar, Calibrar, Atrás
      break;

    case MENU_PH_PANEL:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0;
      break;

    case MENU_PH_SET_LIMIT:
      if (menuCursor < 140) menuCursor++; // pH 0.0 a 14.0
      break;

    case MENU_PH_MANUAL_CO2:
      if (co2MinutesSet < 60) co2MinutesSet++;
      break;

    case MENU_PH_MANUAL_CO2_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;
      
    case MENU_PH_CALIBRATION_SELECT:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0; // pH 4, 7, 10, Atrás
      break;
      
    case MENU_ACTION:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0;
      break;
      
    case MENU_LED_SELECT:
      menuCursor++;
      if (menuCursor > numLeds) menuCursor = 0;
      break;
      
    case MENU_SEQ_LIST:
      menuCursor++;
      if (menuCursor > 11) menuCursor = 0;
      break;
      
    case MENU_SEQ_OPTIONS:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0;
      break;
      
    case MENU_SEQ_CONFIG_CANTIDAD:
      if (menuCursor < 10) menuCursor++;
      break;
      
    case MENU_SEQ_CONFIG_COLOR:
      if (currentColorConfig < 4) {
        if (menuCursor < 20) {
          menuCursor++;
          updateColorPreview();
        }
      } else {
        menuCursor = (menuCursor == 0) ? 1 : 0;
      }
      break;
      
    case MENU_SEQ_CONFIG_TIME:
      if (currentColorConfig == 0) {
        if (menuCursor < 23) menuCursor++;
      } else {
        if (menuCursor < 59) menuCursor++;
      }
      break;
      
    case MENU_ONOFF:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0;
      break;
      
    case MENU_INTENSITY:
      if (menuCursor < 20) {  // Cambiar de 10 a 20 (0-100 en pasos de 5)
        menuCursor++;
        int intensity = menuCursor * 5;  // Cambiar de 10 a 5
        int pwmValue = map(intensity, 0, 100, 0, 255);
        ledcWrite(ledPins[selectedLed], pwmValue);
      }
      break;
      
    case MENU_SEQ_CONFIRM_SAVE:
    case MENU_SEQ_STOP_CONFIRM:
    case MENU_SEQ_EXIT_CONFIG_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_LLENADO:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0; // Solo 3 opciones ahora
      break;

    case MENU_LLENADO_RESET_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_LLENADO_SET_VOLUME:
      if (targetVolume < 200) targetVolume += 5;
      break;

    case MENU_LLENADO_CONFIRM:
    case MENU_LLENADO_STOP_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_AIREACION:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // 3 opciones
      break;

    case MENU_SEQ_EXECUTION_MODE:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0;
      break;

    case MENU_SEQ_CONFIG_TIME_CONFIRM:
    case MENU_SEQ_DELETE_ALL_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_CO2:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // 3 opciones
      break;  

  }
}

void decrementCursor() {
  switch (currentMenu) {
    case MENU_MAIN:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 5;
      break;
      
    case MENU_SENSORS:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 3;
      break;
      
    case MENU_SENSOR_PH:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2; // Ahora son 3 opciones: Fijar, Calibrar, Atrás
      break;
      
    case MENU_PH_CALIBRATION_SELECT:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 3;
      break;
      
    case MENU_PH_PANEL:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2; // 3 opciones: Auto, Manual CO2, Atrás
      break;
      
    case MENU_PH_SET_LIMIT:
      if (menuCursor > 0) menuCursor--; // pH 0.0 a 14.0 (0-140)
      break;
      
    case MENU_PH_MANUAL_CO2:
      if (co2MinutesSet > 0) co2MinutesSet--;
      break;
      
    case MENU_PH_MANUAL_CO2_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;
      
    case MENU_ACTION:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 3;
      break;
      
    case MENU_LED_SELECT:
      menuCursor--;
      if (menuCursor < 0) menuCursor = numLeds;
      break;
      
    case MENU_SEQ_LIST:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 11;
      break;
      
    case MENU_SEQ_OPTIONS:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2;
      break;
      
    case MENU_SEQ_CONFIG_CANTIDAD:
      if (menuCursor > 1) menuCursor--;
      break;
      
    case MENU_SEQ_CONFIG_COLOR:
      if (currentColorConfig < 4) {
        if (menuCursor > 0) {
          menuCursor--;
          updateColorPreview();
        }
      } else {
        menuCursor = (menuCursor == 0) ? 1 : 0;
      }
      break;
      
    case MENU_SEQ_CONFIG_TIME:
      if (currentColorConfig == 0) {
        // Horas
        if (menuCursor > 0) menuCursor--;
      } else if (currentColorConfig == 1) {
        // Minutos
        if (menuCursor > 0) menuCursor--;
      } else {
        // Segundos
        if (menuCursor > 0) menuCursor--;
      }
      break;
      
    case MENU_ONOFF:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2;
      break;
      
    case MENU_INTENSITY:
      if (menuCursor > 0) {
        menuCursor--;
        int intensity = menuCursor * 5;  // Cambiar de 10 a 5
        int pwmValue = map(intensity, 0, 100, 0, 255);
        ledcWrite(ledPins[selectedLed], pwmValue);
      }
      break;
      
    case MENU_SEQ_CONFIRM_SAVE:
    case MENU_SEQ_STOP_CONFIRM:
    case MENU_SEQ_EXIT_CONFIG_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

  case MENU_LLENADO:
    menuCursor--;
    if (menuCursor < 0) menuCursor = 3; // Solo 3 opciones ahora
    break;

  case MENU_LLENADO_RESET_CONFIRM:
    menuCursor = (menuCursor == 0) ? 1 : 0;
    break;

    case MENU_LLENADO_SET_VOLUME:
      if (targetVolume > 0) targetVolume -= 5;
      break;

    case MENU_LLENADO_CONFIRM:
    case MENU_LLENADO_STOP_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_AIREACION:
    menuCursor--;
    if (menuCursor < 0) menuCursor = 2; // 3 opciones
    break;

    case MENU_SEQ_EXECUTION_MODE:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2; // 3 opciones (1 vez, bucle, atrás)
      break;

    case MENU_SEQ_CONFIG_TIME_CONFIRM:
    case MENU_SEQ_DELETE_ALL_CONFIRM:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_CO2:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2; // 3 opciones
      break;

  }
}

void handleSelection() {
  switch (currentMenu) {
    case MENU_MAIN:
      if (menuCursor == 0) {
        currentMenu = MENU_SENSORS;
        menuCursor = 0;
      } else if (menuCursor == 1) {
        currentMenu = MENU_ACTION;
        menuCursor = 0;
      } else if (menuCursor == 2) {
        currentMenu = MENU_LLENADO;
        menuCursor = 0;
      } else if (menuCursor == 3) {
        currentMenu = MENU_AIREACION;
        menuCursor = 0;
      } else if (menuCursor == 4) {
        currentMenu = MENU_CO2;
        menuCursor = 0;
      } else {
        currentMenu = MENU_WEBSERVER;
      }
      break;
      
    case MENU_WEBSERVER:
      // Volver al menú principal
      currentMenu = MENU_MAIN;
      menuCursor = 5;
      break;
      
    case MENU_SENSORS:
      if (menuCursor == 0) {
        // Temperatura - no hace nada, solo muestra
      } else if (menuCursor == 1) {
        // pH - entrar a submenú
        currentMenu = MENU_SENSOR_PH;
        menuCursor = 0;
      } else if (menuCursor == 2) {
        // Turbidez - no hace nada, solo muestra
      } else {
        // Atrás
        currentMenu = MENU_MAIN;
        menuCursor = 0;
      }
      break;
      
    case MENU_SENSOR_PH:
      if (menuCursor == 0) {
        // Fijar - ir al panel de pH
        currentMenu = MENU_PH_PANEL;
        menuCursor = 0;
      } else if (menuCursor == 1) {
        // Calibrar
        currentMenu = MENU_PH_CALIBRATION_SELECT;
        menuCursor = 0;
      } else {
        // Atrás
        currentMenu = MENU_SENSORS;
        menuCursor = 1;
      }
      break;
      
    case MENU_PH_CALIBRATION_SELECT:
      if (menuCursor == 0) {
        // Calibrar pH 4
        //currentMenu = MENU_PH_CALIBRATION_4;
        handlePhCalibration(4.0);
      } else if (menuCursor == 1) {
        // Calibrar pH 7
        //currentMenu = MENU_PH_CALIBRATION_7;
        handlePhCalibration(7.0);
      } else if (menuCursor == 2) {
        // Calibrar pH 10
        //currentMenu = MENU_PH_CALIBRATION_10;
        handlePhCalibration(10.0);
      } else {
        // Atrás
        currentMenu = MENU_SENSOR_PH;
        menuCursor = 0;
      }
      break;

    case MENU_PH_PANEL:
      if (menuCursor == 0) {
        // Control automático
        currentMenu = MENU_PH_SET_LIMIT;
        menuCursor = phLimitSet * 10; // Convertir a escala 0-140 (0.0 a 14.0)
      } else if (menuCursor == 1) {
        // Inyección manual
        currentMenu = MENU_PH_MANUAL_CO2;
        menuCursor = 0;
        co2MinutesSet = 0;
      } else {
        // Atrás
        currentMenu = MENU_SENSOR_PH;
        menuCursor = 0;
      }
      break;

    case MENU_PH_SET_LIMIT:
      // Guardar pH límite y activar control
      phLimitSet = menuCursor / 10.0;
      phControlActive = true;
      currentMenu = MENU_PH_PANEL;
      menuCursor = 0;
      break;

    case MENU_PH_MANUAL_CO2:
      if (co2MinutesSet > 0) {
        currentMenu = MENU_PH_MANUAL_CO2_CONFIRM;
        menuCursor = 0;
      }
      break;

    case MENU_PH_MANUAL_CO2_CONFIRM:
      if (menuCursor == 0) {
        // SI - iniciar inyección
        startCO2Injection();
        currentMenu = MENU_PH_MANUAL_CO2_ACTIVE;
      } else {
        // NO - volver
        currentMenu = MENU_PH_MANUAL_CO2;
        menuCursor = co2MinutesSet;
      }
      break;

    case MENU_PH_MANUAL_CO2_ACTIVE:
      // Detener inyección
      stopCO2Injection();
      currentMenu = MENU_PH_PANEL;
      menuCursor = 1;
      break; 

    case MENU_ACTION:
      if (menuCursor == 0) {
        // On/Off
        selectedAction = 0;
        currentMenu = MENU_LED_SELECT;
        menuCursor = 0;
      } else if (menuCursor == 1) {
        // Intensidad
        selectedAction = 1;
        currentMenu = MENU_LED_SELECT;
        menuCursor = 0;
      } else if (menuCursor == 2) {
        // Secuencias
        currentMenu = MENU_SEQ_LIST;
        menuCursor = 0;
      } else {
        // Atrás
        currentMenu = MENU_MAIN;
        menuCursor = 1;
      }
      break;
      
    case MENU_LED_SELECT:
      if (menuCursor == numLeds) {
        currentMenu = MENU_ACTION;
        menuCursor = selectedAction;
      } else {
        selectedLed = menuCursor;
        if (selectedAction == 0) {
          currentMenu = MENU_ONOFF;
          menuCursor = 0;
        } else {
          currentMenu = MENU_INTENSITY;
          menuCursor = pwmValues[selectedLed] * 2;
        }
      }
      break;
      
    case MENU_ONOFF:
      if (menuCursor == 0) {
        // ON - Cambiar estas líneas
        ledStates[selectedAction] = true;
        pwmValues[selectedAction] = 10;  // AGREGAR ESTA LÍNEA
        ledcWrite(ledPins[selectedAction], 255);  // CAMBIAR de 'pwmValues[selectedAction]' a '255'
      } else {
        // OFF
        ledStates[selectedAction] = false;
        pwmValues[selectedAction] = 0;
        ledcWrite(ledPins[selectedAction], 0);
      }
      currentMenu = MENU_ACTION;
      menuCursor = 0;
      break;
      
    case MENU_INTENSITY:
      pwmValues[selectedLed] = (menuCursor + 1) / 2;
      ledStates[selectedLed] = (menuCursor > 0);
      currentMenu = MENU_LED_SELECT;
      menuCursor = selectedLed;
      break;
      
  case MENU_SEQ_LIST:
    if (menuCursor == 10) {
      // Borrar todas
      currentMenu = MENU_SEQ_DELETE_ALL_CONFIRM;
      menuCursor = 1; // Por defecto en NO
    } else if (menuCursor == 11) {
      // Atrás
      currentMenu = MENU_ACTION;
      menuCursor = 2;
    } else {
      selectedSequence = menuCursor;
      currentMenu = MENU_SEQ_OPTIONS;
      menuCursor = 0;
    }
    break;
      
  case MENU_SEQ_OPTIONS:
    if (menuCursor == 0) {
      // Realizar - ahora pide modo de ejecución
      if (sequences[selectedSequence].configured) {
        currentMenu = MENU_SEQ_EXECUTION_MODE;
        menuCursor = 0;
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Secuencia no");
        lcd.setCursor(0, 2);
        lcd.print("configurada!");
        delay(2000);
        currentMenu = MENU_SEQ_OPTIONS;
        updateDisplay();
      }
    } else if (menuCursor == 1) {
        // Configurar
        currentConfigStep = 0;
        currentColorConfig = 0;
        currentMenu = MENU_SEQ_CONFIG_CANTIDAD;
        menuCursor = 1;
        sequences[selectedSequence].stepCount = 1;
        for (int i = 0; i < 10; i++) {
          for (int j = 0; j < 4; j++) {
            sequences[selectedSequence].steps[i].colorIntensity[j] = 0;
          }
          sequences[selectedSequence].steps[i].hours = 0;
          sequences[selectedSequence].steps[i].minutes = 0;  
          sequences[selectedSequence].steps[i].seconds = 0; 
        }
      } else {
        currentMenu = MENU_SEQ_LIST;
        menuCursor = selectedSequence;
      }
      break;
      
    case MENU_SEQ_CONFIG_CANTIDAD:
      sequences[selectedSequence].stepCount = menuCursor;
      currentConfigStep = 0;
      currentColorConfig = 0;
      currentMenu = MENU_SEQ_CONFIG_COLOR;
      menuCursor = 0;
      for (int i = 0; i < 4; i++) {
        sequences[selectedSequence].steps[0].colorIntensity[i] = 0;
      }
      updateDisplay();
      break;
      
    case MENU_SEQ_CONFIG_COLOR:
      if (currentColorConfig < 4) {
        sequences[selectedSequence].steps[currentConfigStep].colorIntensity[currentColorConfig] = (menuCursor + 1) / 2;
        
        currentColorConfig++;
        if (currentColorConfig < 4) {
          menuCursor = sequences[selectedSequence].steps[currentConfigStep].colorIntensity[currentColorConfig];
          updateColorPreview();
        } else {
          for (int i = 0; i < numLeds; i++) {
            ledcWrite(ledPins[i], 0);
          }
          menuCursor = 0;
        }
      } else {
        if (menuCursor == 0) {
          currentColorConfig = 0;
          currentMenu = MENU_SEQ_CONFIG_TIME;
          menuCursor = 0;
        } else {
          currentColorConfig = 0;
          menuCursor = sequences[selectedSequence].steps[currentConfigStep].colorIntensity[0];
          updateColorPreview();
        }
      }
      break;
      
    case MENU_SEQ_CONFIG_TIME:
      if (currentColorConfig == 0) {
        sequences[selectedSequence].steps[currentConfigStep].hours = menuCursor;
        currentColorConfig = 1;
        menuCursor = 0;
      } else if (currentColorConfig == 1) {
        sequences[selectedSequence].steps[currentConfigStep].minutes = menuCursor;
        currentColorConfig = 2;
        menuCursor = 0;
      } else {
        sequences[selectedSequence].steps[currentConfigStep].seconds = menuCursor;
        // Mostrar confirmación
        currentMenu = MENU_SEQ_CONFIG_TIME_CONFIRM;
        menuCursor = 0;
      }
      break;
      
    case MENU_SEQ_CONFIRM_SAVE:
      if (menuCursor == 0) {
        sequences[selectedSequence].configured = true;
        saveSequence(selectedSequence);
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Secuencia guardada");
        lcd.setCursor(0, 2);
        lcd.print("en SD!");
        delay(2000);
        currentMenu = MENU_SEQ_LIST;
        menuCursor = selectedSequence;
      } else {
        currentMenu = MENU_SEQ_LIST;
        menuCursor = selectedSequence;
      }
      break;
      
    case MENU_SEQ_RUNNING:
      currentMenu = MENU_SEQ_STOP_CONFIRM;
      menuCursor = 1;
      break;
      
    case MENU_SEQ_STOP_CONFIRM:
      if (menuCursor == 0) {
        stopSequence();
        currentMenu = MENU_ACTION;
        menuCursor = 2;
      } else {
        currentMenu = MENU_SEQ_RUNNING;
      }
      break;
      
    case MENU_SEQ_EXIT_CONFIG_CONFIRM:
      if (menuCursor == 0) {
        for (int i = 0; i < numLeds; i++) {
          ledcWrite(ledPins[i], 0);
        }
        currentMenu = MENU_SEQ_LIST;
        menuCursor = selectedSequence;
      } else {
        if (currentConfigStep < sequences[selectedSequence].stepCount) {
          if (currentColorConfig < 4) {
            currentMenu = MENU_SEQ_CONFIG_COLOR;
          } else {
            currentMenu = MENU_SEQ_CONFIG_TIME;
          }
        } else {
          currentMenu = MENU_SEQ_CONFIG_CANTIDAD;
        }
      }
      break;

  case MENU_LLENADO:
    if (menuCursor == 0) {
      // Pedir confirmación para reiniciar volumen
      currentMenu = MENU_LLENADO_RESET_CONFIRM;
      menuCursor = 1; // Por defecto en NO
    } else if (menuCursor == 1) {
      // Configurar llenado
      currentMenu = MENU_LLENADO_SET_VOLUME;
      menuCursor = 0;
      targetVolume = 0;
    } else if (menuCursor == 2) {
      // Encender/Apagar bomba manual
      if (!fillingActive) {
        // Encender bomba sin límite
        fillingActive = true;
        volumeLlenado = 0.0;
        targetVolume = 9999; // Valor alto para que no se detenga
        pcfOutput.digitalWrite(P0, HIGH);
        currentMenu = MENU_LLENADO_ACTIVE;
      } else {
        // Si ya está activa, ir a pantalla de control
        currentMenu = MENU_LLENADO_ACTIVE;
      }
    } else {
      // Atrás
      currentMenu = MENU_MAIN;
      menuCursor = 2;
    }
    break;

    case MENU_LLENADO_SET_VOLUME:
      currentMenu = MENU_LLENADO_CONFIRM;
      menuCursor = 1; // Por defecto en NO
      break;

    case MENU_LLENADO_CONFIRM:
      if (menuCursor == 0) {
        // SI - Iniciar llenado
        startFilling();
        currentMenu = MENU_LLENADO_ACTIVE;
      } else {
        // NO - Volver
        currentMenu = MENU_LLENADO;
        menuCursor = 1;
      }
      break;

    case MENU_LLENADO_ACTIVE:
      currentMenu = MENU_LLENADO_STOP_CONFIRM;
      menuCursor = 1;
      break;

    case MENU_LLENADO_STOP_CONFIRM:
      if (menuCursor == 0) {
        // SI - Detener
        stopFilling();
        currentMenu = MENU_LLENADO;
        menuCursor = 0;
      } else {
        // NO - Continuar
        currentMenu = MENU_LLENADO_ACTIVE;
      }
      break;
    
    case MENU_LLENADO_RESET_CONFIRM:
      if (menuCursor == 0) {
        // SI - Reiniciar volumen
        volumeTotal = 0.0;
        pulseCount = 0;
        saveVolumeToEEPROM();
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Volumen reiniciado!");
        delay(1500);
      }
      // En ambos casos volver al menú llenado
      currentMenu = MENU_LLENADO;
      menuCursor = 0;
      break;

    case MENU_AIREACION:
      if (menuCursor == 0) {
        if (emergencyActive) return;
        // Encender
        aireacionActive = true;
        pcfOutput.digitalWrite(P0, LOW);
      } else if (menuCursor == 1) {
        // Apagar
        aireacionActive = false;
        pcfOutput.digitalWrite(P0, HIGH);
      } else {
        // Atrás
        currentMenu = MENU_MAIN;
        menuCursor = 3;
      }
      break;

    case MENU_SEQ_EXECUTION_MODE:
      if (menuCursor == 0) {
        // Realizar 1 vez
        sequenceLoopMode = false;
        startSequence();
      } else if (menuCursor == 1) {
        // Realizar en bucle
        sequenceLoopMode = true;
        startSequence();
      } else {
        // Atrás
        currentMenu = MENU_SEQ_OPTIONS;
        menuCursor = 0;
      }
      break;

case MENU_SEQ_CONFIG_TIME_CONFIRM:
  if (menuCursor == 0) {
    // SI - continuar
    currentConfigStep++;
    if (currentConfigStep < sequences[selectedSequence].stepCount) {
      currentColorConfig = 0;
      currentMenu = MENU_SEQ_CONFIG_COLOR;
      menuCursor = 0;
      for (int i = 0; i < 4; i++) {
        sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i] = 0;
      }
    } else {
      currentMenu = MENU_SEQ_CONFIRM_SAVE;
      menuCursor = 0;
    }
  } else {
    // NO - reconfigurar tiempo
    currentColorConfig = 0;
    currentMenu = MENU_SEQ_CONFIG_TIME;
    menuCursor = 0;
  }
  break;

case MENU_SEQ_DELETE_ALL_CONFIRM:
  if (menuCursor == 0) {
    // SI - borrar todas
    for (int i = 0; i < 10; i++) {
      sequences[i].configured = false;
      sequences[i].stepCount = 0;
      String filename = "/seq_" + String(i + 1) + ".json";
      SD.remove(filename);
    }
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Todas las secuencias");
    lcd.setCursor(0, 2);
    lcd.print("han sido borradas");
    delay(2000);
  }
  currentMenu = MENU_SEQ_LIST;
  menuCursor = 0;
  break;

  case MENU_CO2:
    if (menuCursor == 0) {
      // Encender
      co2Active = true;
      pcfOutput.digitalWrite(P2, LOW);
    } else if (menuCursor == 1) {
      // Apagar
      co2Active = false;
      pcfOutput.digitalWrite(P2, HIGH);
    } else {
      // Atrás
      currentMenu = MENU_MAIN;
      menuCursor = 4;
    }
    break;

  }
  
  updateDisplay();
}

void updateDisplay() {
  switch (currentMenu) {
    case MENU_MAIN:
      displayMainMenu();
      break;
    case MENU_SENSORS:
      displaySensorsMenu();
      break;
    case MENU_SENSOR_PH:
      displaySensorPhMenu();
      break;
    case MENU_PH_CALIBRATION_SELECT:
      displayPhCalibrationSelect();
      break;
    case MENU_PH_PANEL:
      displayPhPanel();
      break;
    case MENU_PH_SET_LIMIT:
      displayPhSetLimit();
      break;
    case MENU_PH_MANUAL_CO2:
      displayPhManualCO2();
      break;
    case MENU_PH_MANUAL_CO2_CONFIRM:
      displayPhManualCO2Confirm();
      break;
    case MENU_PH_MANUAL_CO2_ACTIVE:
      displayPhManualCO2Active();
      break;
    case MENU_WEBSERVER:
      displayWebServerMenu();
      break;
    case MENU_ACTION:
      displayActionMenu();
      break;
    case MENU_LED_SELECT:
      displayLedSelectMenu();
      break;
    case MENU_ONOFF:
      displayOnOffMenu();
      break;
    case MENU_INTENSITY:
      displayIntensityMenu();
      break;
    case MENU_SEQ_LIST:
      displaySeqList();
      break;
    case MENU_SEQ_OPTIONS:
      displaySeqOptions();
      break;
    case MENU_SEQ_CONFIG_CANTIDAD:
      displaySeqCantidad();
      break;
    case MENU_SEQ_CONFIG_COLOR:
      displaySeqConfigColor();
      break;
    case MENU_SEQ_CONFIG_TIME:
      displaySeqConfigTime();
      break;
    case MENU_SEQ_CONFIRM_SAVE:
      displaySeqConfirmSave();
      break;
    case MENU_SEQ_RUNNING:
      displaySeqRunning();
      break;
    case MENU_SEQ_STOP_CONFIRM:
      displaySeqStopConfirm();
      break;
    case MENU_SEQ_EXIT_CONFIG_CONFIRM:
      displaySeqExitConfirm();
      break;
    case MENU_LLENADO:
      displayLlenadoMenu();
      break;
    case MENU_LLENADO_SET_VOLUME:
      displaySetVolume();
      break;
    case MENU_LLENADO_CONFIRM:
      displayConfirmFilling();
      break;
    case MENU_LLENADO_ACTIVE:
      displayFillingActive();
      break;
    case MENU_LLENADO_STOP_CONFIRM:
      displayStopConfirm();
      break;
    case MENU_LLENADO_RESET_CONFIRM:
      displayResetConfirm();
      break;
    case MENU_AIREACION:
      displayAireacionMenu();
      break;
    case MENU_SEQ_CONFIG_TIME_CONFIRM:
      displayTimeConfirm();
      break;
    case MENU_SEQ_EXECUTION_MODE:
      displayExecutionMode();
      break;
    case MENU_SEQ_DELETE_ALL_CONFIRM:
      displayDeleteAllConfirm();
      break;
    case MENU_CO2:
      displayCO2Menu();
      break;
  }
}

void displayMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEMA PRINCIPAL:");
  
  // Con 6 opciones, necesitamos scroll
  int startIndex = 0;
  if (menuCursor > 2) {
    startIndex = menuCursor - 2;
    if (startIndex > 3) startIndex = 3; // Max 3 para mostrar las últimas 3
  }
  
  const char* opciones[] = {"Sensores", "LEDs", "Llenado", "Aireacion", "Ingreso CO2", "WebServer"};
  
  for (int i = 0; i < 3; i++) {
    int optionIndex = startIndex + i;
    if (optionIndex < 6) {
      lcd.setCursor(0, i + 1);
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print(opciones[optionIndex]);
    }
  }
  
  // Indicadores de scroll
  if (startIndex > 0) {
    lcd.setCursor(19, 1);
    lcd.print("^");
  }
  if (startIndex < 3) {
    lcd.setCursor(19, 3);
    lcd.print("v");
  }
  
  // Hora
  //DateTime now = rtc.now();
  //cd.setCursor(14, 0);
  //if (now.hour() < 10) lcd.print("0");
  //lcd.print(now.hour());
  //lcd.print(":");
  //if (now.minute() < 10) lcd.print("0");
  //lcd.print(now.minute());
}

void displayActionMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONTROL LEDs:");
  
  int startIndex = 0;
  if (menuCursor > 2) startIndex = 1;
  
  for (int i = 0; i < 3; i++) {
    int optionIndex = startIndex + i;
    lcd.setCursor(0, i + 1);
    
    if (optionIndex == 0) {
      lcd.print(menuCursor == 0 ? "> " : "  ");
      lcd.print("On/Off");
    } else if (optionIndex == 1) {
      lcd.print(menuCursor == 1 ? "> " : "  ");
      lcd.print("Intensidad");
    } else if (optionIndex == 2) {
      lcd.print(menuCursor == 2 ? "> " : "  ");
      lcd.print("Secuencias");
    } else if (optionIndex == 3) {
      lcd.print(menuCursor == 3 ? "> " : "  ");
      lcd.print("Atras");
    }
  }
}

void displaySensorsMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SENSORES:");
  
  // Siempre mostrar los 3 sensores con sus valores
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Temp: ");
  lcd.print(temperature, 1);
  lcd.print(" C");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("pH: ");
  lcd.print(phValue, 2);
  
  lcd.setCursor(0, 3);
  if (menuCursor < 3) {
    lcd.print(menuCursor == 2 ? "> " : "  ");
    lcd.print("Turb: ");
    lcd.print(turbidez, 0);
    lcd.print(" NTU");
  } else {
    lcd.print("> Atras");
  }
}

void displayWebServerMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SERVIDOR WEB:");
  
  lcd.setCursor(0, 1);
  lcd.print("Red: ");
  lcd.print(ap_ssid);
  
  lcd.setCursor(0, 2);
  lcd.print("Pass: ");
  lcd.print(ap_password);

  lcd.setCursor(0, 3);
  lcd.print("IP: 192.168.4.1");
}

void displaySensorPhMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SENSOR pH:");
  
  lcd.setCursor(0, 1);
  lcd.print("Valor actual: ");
  lcd.print(phValue, 2);
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Fijar");
  
  lcd.setCursor(0, 3);
  if (menuCursor == 1) {
    lcd.print("> Calibrar");
  } else if (menuCursor == 2) {
    lcd.print("> Atras");
  } else {
    lcd.print("  Calibrar");
  }
}

void displayPhCalibrationSelect() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRAR pH:");
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Buffer pH 4.0");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Buffer pH 7.0");
  
  lcd.setCursor(0, 3);
  if (menuCursor < 3) {
    lcd.print(menuCursor == 2 ? "> " : "  ");
    lcd.print("Buffer pH 10.0");
  } else {
    lcd.print("> Atras");
  }
}

void displayLedSelectMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SELECCIONAR TIRA:");
  
  int startIndex = 0;
  int displayLines = 3;
  
  if (menuCursor >= 3) {
    startIndex = menuCursor - 2;
  }
  
  for (int i = 0; i < displayLines; i++) {
    int optionIndex = startIndex + i;
    lcd.setCursor(0, i + 1);
    
    if (optionIndex < numLeds) {
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print(ledNames[optionIndex]);
      
      if (ledStates[optionIndex]) {
        lcd.setCursor(10, i + 1);
        if (pwmValues[optionIndex] == 10) {
          lcd.print("[ON]");
        } else {
          lcd.print("[");
          lcd.print(pwmValues[optionIndex]);
          lcd.print("]");
        }
      } else {
        lcd.setCursor(10, i + 1);
        lcd.print("[OFF]");
      }
    } else if (optionIndex == numLeds) {
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print("Atras");
    }
  }
}

void displayOnOffMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TIRA: ");
  lcd.print(ledNames[selectedLed]);
  
  lcd.setCursor(14, 0);
  if (ledStates[selectedLed]) {
    if (pwmValues[selectedLed] == 10) {
      lcd.print("[ON]");
    } else {
      lcd.print("[");
      lcd.print(pwmValues[selectedLed]);
      lcd.print("]");
    }
  } else {
    lcd.print("[OFF]");
  }
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("ON  (Max)");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("OFF");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
}

void displayIntensityMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INTENSIDAD ");
  lcd.print(ledNames[selectedLed]);
  
  lcd.setCursor(0, 1);
  lcd.print("Nivel: ");
  int percentage = menuCursor * 5;  // Pasos de 5%
  lcd.print(percentage);
  lcd.print("%");
  if (percentage == 100) {
    lcd.print(" (MAX)");
  }
  
  lcd.setCursor(0, 2);
  lcd.print("[");
  int barLength = map(menuCursor, 0, 20, 0, 18);  // 20 pasos de 5% = 100%
  for (int i = 0; i < 18; i++) {
    if (i < barLength) {
      lcd.print("=");
    } else {
      lcd.print(" ");
    }
  }
  lcd.print("]");
  
  lcd.setCursor(0, 3);
  lcd.print("Click para guardar");
}

void displaySeqList() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SECUENCIAS [");
  int configuredCount = 0;
  for (int i = 0; i < 10; i++) {
    if (sequences[i].configured) configuredCount++;
  }
  lcd.print(configuredCount);
  lcd.print("/10]:");
  
  int startIndex = 0;
  int displayLines = 3;
  int totalOptions = 12; // Ahora incluye "Borrar todas" y "Atrás"
  
  if (menuCursor <= 1) {
    startIndex = 0;
  } else if (menuCursor >= totalOptions - 2) {
    startIndex = totalOptions - displayLines;
  } else {
    startIndex = menuCursor - 1;
  }
  
  for (int i = 0; i < displayLines; i++) {
    int optionIndex = startIndex + i;
    
    if (optionIndex >= totalOptions) break;
    
    lcd.setCursor(0, i + 1);
    
    if (optionIndex < 10) {
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print("Seq ");
      if (optionIndex < 9) {
        lcd.print(optionIndex + 1);
        lcd.print(" ");
      } else {
        lcd.print(optionIndex + 1);
      }
      
      if (sequences[optionIndex].configured) {
        lcd.print(" [");
        lcd.print(sequences[optionIndex].stepCount);
        lcd.print("p]");
      } else {
        lcd.print(" [--]");
      }
    } else if (optionIndex == 10) {
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print("Borrar todas");
    } else if (optionIndex == 11) {
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print("Atras");
    }
  }
  
  if (totalOptions > displayLines) {
    lcd.setCursor(19, 1);
    if (startIndex > 0) lcd.print("^");
    lcd.setCursor(19, 3);
    if (startIndex + displayLines < totalOptions) lcd.print("v");
  }
}

void displaySeqOptions() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SECUENCIA ");
  lcd.print(selectedSequence + 1);
  if (sequences[selectedSequence].configured) {
    lcd.print(" [");
    lcd.print(sequences[selectedSequence].stepCount);
    lcd.print("p]");
  } else {
    lcd.print(" [--]");
  }
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Realizar");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Configurar");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
}

void displaySeqCantidad() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SECUENCIA ");
  lcd.print(selectedSequence + 1);
  lcd.print(":");
  
  lcd.setCursor(0, 1);
  lcd.print("Cantidad de pasos:");
  
  lcd.setCursor(0, 2);
  lcd.print("> ");
  lcd.print(menuCursor);
  lcd.print(" paso");
  if (menuCursor > 1) lcd.print("s");
  
  lcd.setCursor(0, 3);
  lcd.print("Click para continuar");
}

void displaySeqConfigColor() {
  lcd.clear();
  
  if (currentColorConfig < 4) {
    lcd.setCursor(0, 0);
    lcd.print("Paso ");
    lcd.print(currentConfigStep + 1);
    lcd.print(" [");
    lcd.print(currentColorConfig + 1);
    lcd.print("/4]");
    
    lcd.setCursor(0, 1);
    lcd.print(ledNames[currentColorConfig]);
    lcd.print(": ");
    lcd.print(menuCursor * 5);  // Mostrar directamente el porcentaje
    lcd.print("%");
    
    lcd.print("[");
    int barLength = map(menuCursor, 0, 20, 0, 18);
    for (int i = 0; i < 18; i++) {
      if (i < barLength) {
        lcd.print("=");
      } else {
        lcd.print(" ");
      }
    }
    lcd.print("]");
    
    lcd.setCursor(0, 3);
    if (currentColorConfig > 0) {
      for (int i = 0; i < currentColorConfig; i++) {
        if (sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i] > 0) {
          lcd.print(ledNames[i][0]);
          lcd.print(sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i]);
          lcd.print(" ");
        }
      }
    } else {
      lcd.print("Click siguiente");
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Paso ");
    lcd.print(currentConfigStep + 1);
    lcd.print(" configurado:");
    
    for (int i = 0; i < 4; i++) {
      if (sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i] > 0) {
        lcd.setCursor(0, i < 2 ? 1 : 2);
        if (i == 2) lcd.setCursor(10, 1);
        if (i == 3) lcd.setCursor(10, 2);
        lcd.print(ledNames[i][0]);
        lcd.print(":");
        lcd.print(sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i]);
      }
    }
    
    lcd.setCursor(0, 3);
    lcd.print(menuCursor == 0 ? "> Continuar" : "> Reconfigurar");
  }
}

void displaySeqConfigTime() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Paso ");
  lcd.print(currentConfigStep + 1);
  lcd.print(" - Tiempo:");
  
  String colors = "";
  for (int i = 0; i < 4; i++) {
    if (sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i] > 0) {
      if (colors.length() > 0) colors += "+";
      colors += ledNames[i][0];
    }
  }
  lcd.setCursor(0, 1);
  lcd.print(colors);
  
  if (currentColorConfig == 0) {
    // Horas
    lcd.setCursor(0, 2);
    lcd.print("> ");
    if (menuCursor < 10) lcd.print("0");
    lcd.print(menuCursor);
    lcd.print(" Horas");
  } else if (currentColorConfig == 1) {
    // Minutos
    lcd.setCursor(0, 2);
    lcd.print("  ");
    if (sequences[selectedSequence].steps[currentConfigStep].hours < 10) lcd.print("0");
    lcd.print(sequences[selectedSequence].steps[currentConfigStep].hours);
    lcd.print(" Horas");
    
    lcd.setCursor(0, 3);
    lcd.print("> ");
    if (menuCursor < 10) lcd.print("0");
    lcd.print(menuCursor);
    lcd.print(" Minutos");
  } else {
    // Segundos
    lcd.setCursor(0, 2);
    lcd.print("  ");
    lcd.print(sequences[selectedSequence].steps[currentConfigStep].hours);
    lcd.print("h ");
    lcd.print(sequences[selectedSequence].steps[currentConfigStep].minutes);
    lcd.print("m");
    
    lcd.setCursor(0, 3);
    lcd.print("> ");
    if (menuCursor < 10) lcd.print("0");
    lcd.print(menuCursor);
    lcd.print(" Segundos");
  }
}

void displaySeqConfirmSave() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GUARDAR CAMBIOS?");
  
  lcd.setCursor(0, 1);
  lcd.print("Secuencia ");
  lcd.print(selectedSequence + 1);
  lcd.print(" (");
  lcd.print(sequences[selectedSequence].stepCount);
  lcd.print(" pasos)");
  
  // Mostrar tiempo total de la secuencia
  int totalSeconds = 0;
  for (int i = 0; i < sequences[selectedSequence].stepCount; i++) {
    totalSeconds += sequences[selectedSequence].steps[i].hours * 3600 +
                   sequences[selectedSequence].steps[i].minutes * 60 +
                   sequences[selectedSequence].steps[i].seconds;
  }
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displaySeqRunning() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Seq ");
  lcd.print(selectedSequence + 1);
  lcd.print(" [");
  lcd.print(currentSequenceStep + 1);
  lcd.print("/");
  lcd.print(sequences[selectedSequence].stepCount);
  lcd.print("]");
  if (sequenceLoopMode) {
    lcd.print(" BUCLE");
  }
  
  lcd.setCursor(0, 1);
  String activeColors = "";
  for (int i = 0; i < 4; i++) {
    if (sequences[selectedSequence].steps[currentSequenceStep].colorIntensity[i] > 0) {
      if (activeColors.length() > 0) activeColors += "+";
      activeColors += ledNames[i][0];
      activeColors += String(sequences[selectedSequence].steps[currentSequenceStep].colorIntensity[i]);
    }
  }
  lcd.print(activeColors);
  
  // Calcular tiempo transcurrido
  DateTime now = rtc.now();
  TimeSpan elapsed = now - stepStartTime;
  
  int totalHours = sequences[selectedSequence].steps[currentSequenceStep].hours;
  int totalMinutes = sequences[selectedSequence].steps[currentSequenceStep].minutes;
  int totalSeconds = sequences[selectedSequence].steps[currentSequenceStep].seconds;
  
  // Convertir tiempo transcurrido a segundos totales
  int elapsedTotalSeconds = elapsed.days() * 86400 + elapsed.hours() * 3600 + 
                            elapsed.minutes() * 60 + elapsed.seconds();
  
  // Descomponer en horas, minutos, segundos
  int elapsedHours = elapsedTotalSeconds / 3600;
  int elapsedMinutes = (elapsedTotalSeconds % 3600) / 60;
  int elapsedSeconds = elapsedTotalSeconds % 60;
  
  lcd.setCursor(0, 2);
  lcd.print("Tiempo: ");
  if (elapsedHours < 10) lcd.print("0");
  lcd.print(elapsedHours);
  lcd.print(":");
  if (elapsedMinutes < 10) lcd.print("0");
  lcd.print(elapsedMinutes);
  lcd.print(":");
  if (elapsedSeconds < 10) lcd.print("0");
  lcd.print(elapsedSeconds);
  
  lcd.setCursor(0, 3);
  lcd.print("Total: ");
  if (totalHours < 10) lcd.print("0");
  lcd.print(totalHours);
  lcd.print(":");
  if (totalMinutes < 10) lcd.print("0");
  lcd.print(totalMinutes);
  lcd.print(":");
  if (totalSeconds < 10) lcd.print("0");
  lcd.print(totalSeconds);
  
  // Indicador de progreso en la esquina
  int totalStepSeconds = totalHours * 3600 + totalMinutes * 60 + totalSeconds;
  if (totalStepSeconds > 0) {
    int progress = (elapsedTotalSeconds * 100) / totalStepSeconds;
    if (progress > 100) progress = 100;
    lcd.setCursor(17, 3);
    lcd.print(progress);
    lcd.print("%");
  }
}

void displaySeqStopConfirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DETENER SECUENCIA?");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displaySeqExitConfirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SALIR DE CONFIG?");
  
  lcd.setCursor(0, 1);
  lcd.print("Se perderan cambios");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void updateColorPreview() {
  for (int i = 0; i <= currentColorConfig; i++) {
    int intensity;
    if (i == currentColorConfig) {
      intensity = menuCursor*5;
    } else {
      intensity = sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i]*10;
    }
    
    int pwmValue = map(intensity, 0, 100, 0, 255);
    ledcWrite(ledPins[i], pwmValue);
  }
  
  for (int i = currentColorConfig + 1; i < numLeds; i++) {
    ledcWrite(ledPins[i], 0);
  }
}

void setLED(int index, bool state, int intensity) {
  if (index < 0 || index >= numLeds) return;
  
  ledStates[index] = state;
  pwmValues[index] = intensity / 10; // Convertir de 0-100 a 0-10 para consistencia con LCD
  
  int pwmValue = map(intensity, 0, 100, 0, 255);
  ledcWrite(ledPins[index], state ? pwmValue : 0);
  
  // Actualizar LCD si estamos en el menú correspondiente
  if (currentMenu == MENU_LED_SELECT || currentMenu == MENU_ONOFF || currentMenu == MENU_INTENSITY) {
    updateDisplay();
  }
  
  Serial.print("LED ");
  Serial.print(ledNames[index]);
  Serial.print(": ");
  Serial.print(state ? "ON" : "OFF");
  Serial.print(" - Intensidad: ");
  Serial.println(intensity);
}

void startSequence() {
  sequenceRunning = true;
  currentSequenceStep = 0;
  sequenceStartTime = rtc.now();
  stepStartTime = rtc.now();
  
  for (int i = 0; i < numLeds; i++) {
    ledcWrite(ledPins[i], 0);
  }
  
  applySequenceStep(0);
  currentMenu = MENU_SEQ_RUNNING;
  
  Serial.print("Secuencia ");
  Serial.print(selectedSequence + 1);
  Serial.println(" iniciada");
}

void stopSequence() {
  sequenceRunning = false;
  
  for (int i = 0; i < numLeds; i++) {
    ledcWrite(ledPins[i], 0);
    ledStates[i] = false;
    pwmValues[i] = 0;
  }
  
  Serial.println("Secuencia detenida");
}

void applySequenceStep(int step) {
  for (int i = 0; i < 4; i++) {
    int intensity = sequences[selectedSequence].steps[step].colorIntensity[i];
    int pwmValue = map(intensity, 0, 10, 0, 255);
    ledcWrite(ledPins[i], pwmValue);
    ledStates[i] = intensity > 0;
    pwmValues[i] = intensity;
    
    if (intensity > 0) {
      Serial.print(ledNames[i]);
      Serial.print(":");
      Serial.print(intensity * 10);
      Serial.print("% ");
    }
  }
  Serial.println();
}

void checkSequenceProgress() {
  if (!sequenceRunning) return;
  
  DateTime now = rtc.now();
  TimeSpan elapsed = now - stepStartTime;
  
  int totalSeconds = sequences[selectedSequence].steps[currentSequenceStep].hours * 3600 + 
                     sequences[selectedSequence].steps[currentSequenceStep].minutes * 60 +
                     sequences[selectedSequence].steps[currentSequenceStep].seconds;
  int elapsedSeconds = elapsed.days() * 86400 + elapsed.hours() * 3600 + 
                       elapsed.minutes() * 60 + elapsed.seconds();
  
  if (elapsedSeconds >= totalSeconds) {
    for (int i = 0; i < numLeds; i++) {
      ledcWrite(ledPins[i], 0);
    }
    
    currentSequenceStep++;
    
    if (currentSequenceStep >= sequences[selectedSequence].stepCount) {
      if (sequenceLoopMode) {
        // Reiniciar secuencia
        currentSequenceStep = 0;
        stepStartTime = now;
        applySequenceStep(0);
      } else {
        stopSequence();
        currentMenu = MENU_ACTION;
        menuCursor = 0;
        Serial.println("Secuencia completada");
      }
    } else {
      stepStartTime = now;
      applySequenceStep(currentSequenceStep);
    }
    
    updateDisplay();
  }
}

void saveSequence(int seqIndex) {
  String filename = "/seq_" + String(seqIndex + 1) + ".json";
  
  SD.remove(filename);
  
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Error al crear archivo de secuencia");
    return;
  }
  
  DynamicJsonDocument doc(2048);
  doc["configured"] = true;
  doc["stepCount"] = sequences[seqIndex].stepCount;
  
  JsonArray steps = doc.createNestedArray("steps");
  
  for (int i = 0; i < sequences[seqIndex].stepCount; i++) {
    JsonObject step = steps.createNestedObject();
    JsonArray colors = step.createNestedArray("colors");
    
    for (int j = 0; j < 4; j++) {
      colors.add(sequences[seqIndex].steps[i].colorIntensity[j]);
    }
    
  step["hours"] = sequences[seqIndex].steps[i].hours;
  step["minutes"] = sequences[seqIndex].steps[i].minutes;
  step["seconds"] = sequences[seqIndex].steps[i].seconds;
  }
  
  serializeJson(doc, file);
  file.close();
  
  Serial.print("Secuencia ");
  Serial.print(seqIndex + 1);
  Serial.println(" guardada en SD");
}

void loadSequence(int seqIndex) {
  String filename = "/seq_" + String(seqIndex + 1) + ".json";
  
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    sequences[seqIndex].configured = false;
    return;
  }
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.print("Error al leer secuencia ");
    Serial.println(seqIndex + 1);
    sequences[seqIndex].configured = false;
    return;
  }
  
  sequences[seqIndex].configured = doc["configured"];
  sequences[seqIndex].stepCount = doc["stepCount"];
  
  JsonArray steps = doc["steps"];
  for (int i = 0; i < sequences[seqIndex].stepCount; i++) {
    JsonObject step = steps[i];
    JsonArray colors = step["colors"];
    
    for (int j = 0; j < 4; j++) {
      sequences[seqIndex].steps[i].colorIntensity[j] = colors[j];
    }
    
  sequences[seqIndex].steps[i].hours = step["hours"];
  sequences[seqIndex].steps[i].minutes = step["minutes"];
  sequences[seqIndex].steps[i].seconds = step["seconds"];
  }
  
  Serial.print("Secuencia ");
  Serial.print(seqIndex + 1);
  Serial.println(" cargada desde SD");
}

void loadAllSequences() {
  Serial.println("Cargando secuencias desde SD...");
  for (int i = 0; i < 10; i++) {
    loadSequence(i);
  }
}

void startFilling() {

  if (emergencyActive) return;

  fillingActive = true;
  volumeLlenado = 0.0;
  // Guardar el volumen actual como referencia
  noInterrupts();
  unsigned long pulsos = pulseCount;
  interrupts();
  volumeTotal = pulsos / PULSOS_POR_LITRO;
  
  pcfOutput.digitalWrite(P1, LOW); // Activar bomba
}

void stopFilling() {
  fillingActive = false;
  pcfOutput.digitalWrite(P1, HIGH); // Desactivar bomba
}

void saveVolumeToEEPROM() {
  EEPROM.put(EEPROM_VOLUME_ADDR, volumeTotal);
  EEPROM.commit();
}

void updateFlowMeasurement() {
  if (millis() - lastFlowCheck >= 500) {
    lastFlowCheck = millis();
    
    noInterrupts();
    unsigned long pulsos = pulseCount;
    interrupts();
    
    float litrosActuales = pulsos / PULSOS_POR_LITRO;
    
    if (fillingActive) {
      // Calcular litros llenados desde que empezó el llenado
      volumeLlenado = litrosActuales - volumeTotal;
      
      // Solo detener si hay un límite establecido (no en modo manual)
      if (targetVolume < 9999 && volumeLlenado >= targetVolume) {
        stopFilling();
        currentMenu = MENU_LLENADO;
        menuCursor = 0;
      }
    } else {
      volumeTotal = litrosActuales;
    }
    
    // Actualizar display si estamos en menú llenado o llenado activo
    if (currentMenu == MENU_LLENADO || currentMenu == MENU_LLENADO_ACTIVE) {
      updateDisplay();
    }
    
    // Guardar cada 5 segundos
    static unsigned long lastSave = 0;
    if (millis() - lastSave > 5000) {
      lastSave = millis();
      saveVolumeToEEPROM();
    }
  }
}

void displayLlenadoMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MENU LLENADO");
  
  lcd.setCursor(0, 1);
  lcd.print("Volumen: ");
  lcd.print(volumeTotal, 0);
  lcd.print(" L");
  
  // Si hay 4 o más opciones, necesitamos scroll
  int startIndex = 0;
  if (menuCursor > 2) {
    startIndex = menuCursor - 2;
    if (startIndex > 1) startIndex = 1; // Ajustar para 4 opciones
  }
  
  const char* opciones[] = {"Reiniciar Volumen", "Llenar Tanque", "Encender Bomba", "Atras"};
  
  for (int i = 0; i < 2; i++) { // Solo 2 líneas disponibles
    int optionIndex = startIndex + i;
    if (optionIndex < 4) {
      lcd.setCursor(0, i + 2);
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print(opciones[optionIndex]);
    }
  }
  
  // Indicadores de scroll si es necesario
  if (startIndex > 0) {
    lcd.setCursor(19, 2);
    lcd.print("^");
  }
  if (startIndex < 1) {
    lcd.setCursor(19, 3);
    lcd.print("v");
  }
}

void displaySetVolume() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIGURAR LLENADO:");
  
  lcd.setCursor(0, 1);
  lcd.print("Litros a llenar:");
  
  lcd.setCursor(0, 2);
  lcd.print("> ");
  lcd.print(targetVolume, 0);
  lcd.print(" L");
  
  lcd.setCursor(0, 3);
  lcd.print("Girar:Ajustar OK:Sig");
}

void displayConfirmFilling() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIRMAR LLENADO?");
  
  lcd.setCursor(0, 1);
  lcd.print("Llenar ");
  lcd.print(targetVolume, 0);
  lcd.print(" litros");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displayFillingActive() {
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (targetVolume >= 9999) {
    lcd.print("BOMBA ACTIVA");
  } else {
    lcd.print("LLENANDO TANQUE");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Llenado: ");
  lcd.print(volumeLlenado, 1);
  if (targetVolume < 9999) {
    lcd.print("/");
    lcd.print(targetVolume, 0);
  }
  lcd.print(" L");
  
  lcd.setCursor(0, 2);
  lcd.print("Total: ");
  lcd.print(volumeTotal + volumeLlenado, 1);
  lcd.print(" L");
  
  lcd.setCursor(0, 3);
  if (targetVolume >= 9999) {
    lcd.print("Click para detener");
  } else {
    // Barra de progreso para modo automático
    lcd.print("[");
    int progress = (volumeLlenado / targetVolume) * 14;
    for(int i = 0; i < 14; i++) {
      lcd.print(i < progress ? "=" : " ");
    }
    lcd.print("]");
  }
}

void displayStopConfirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DETENER LLENADO?");
  
  lcd.setCursor(0, 1);
  lcd.print("Llenados: ");
  lcd.print(volumeLlenado, 0);
  lcd.print(" L");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displayResetConfirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("REINICIAR VOLUMEN?");
  
  lcd.setCursor(0, 1);
  lcd.print("Se pondra en 0 L");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displayAireacionMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AIREACION");
  
  lcd.setCursor(0, 1);
  lcd.print("Estado: ");
  lcd.print(aireacionActive ? "Encendido" : "Apagado");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Encender");
  
  lcd.setCursor(0, 3);
  if (menuCursor == 1) {
    lcd.print("> Apagar");
  } else if (menuCursor == 2) {
    lcd.print("> Atras");
  } else {
    lcd.print("  Apagar");
  }
}

void displayTimeConfirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIRMAR TIEMPO?");
  
  lcd.setCursor(0, 1);
  lcd.print("Paso ");
  lcd.print(currentConfigStep + 1);
  lcd.print(": ");
  lcd.print(sequences[selectedSequence].steps[currentConfigStep].hours);
  lcd.print("h ");
  lcd.print(sequences[selectedSequence].steps[currentConfigStep].minutes);
  lcd.print("m ");
  lcd.print(sequences[selectedSequence].steps[currentConfigStep].seconds);
  lcd.print("s");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displayExecutionMode() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MODO DE EJECUCION:");
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Realizar 1 vez");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Realizar en Bucle");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
}

void displayDeleteAllConfirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("BORRAR TODAS?");
  
  lcd.setCursor(0, 1);
  lcd.print("Se borraran las 10");
  lcd.setCursor(0, 2);
  lcd.print("secuencias");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI  " : "  SI  ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void displayCO2Menu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INGRESO CO2");
  
  lcd.setCursor(0, 1);
  lcd.print("Estado: ");
  lcd.print(co2Active ? "Encendido" : "Apagado");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Encender");
  
  lcd.setCursor(0, 3);
  if (menuCursor == 1) {
    lcd.print("> Apagar");
  } else if (menuCursor == 2) {
    lcd.print("> Atras");
  } else {
    lcd.print("  Apagar");
  }
}

void displayPhPanel() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PANEL pH:");
  
  lcd.setCursor(12, 0);
  lcd.print("pH:");
  lcd.print(phValue, 1);
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Auto: ");
  if (phControlActive) {
    lcd.print(phLimitSet, 1);
    // Indicar estado con color simulado
    if (phValue > phLimitSet + 0.2) {
      lcd.print(" ALK"); // Alcalino
    } else if (phValue < phLimitSet - 0.2) {
      lcd.print(" ACD"); // Ácido
    } else {
      lcd.print(" OK"); // Equilibrado
    }
  } else {
    lcd.print("OFF");
  }
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Manual CO2");
  if (co2InjectionActive) {
    lcd.print(" [ON]");
  }
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
}

void displayPhSetLimit() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FIJAR pH LIMITE:");
  
  lcd.setCursor(0, 1);
  lcd.print("pH actual: ");
  lcd.print(phValue, 2);
  
  lcd.setCursor(0, 2);
  float limitValue = menuCursor / 10.0;
  lcd.print("> pH limite: ");
  lcd.print(limitValue, 1);
  
  lcd.setCursor(0, 3);
  lcd.print("OK:Activar ESC:Salir");
}

void displayPhManualCO2() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INYECCION MANUAL:");
  
  lcd.setCursor(0, 1);
  lcd.print("Minutos CO2:");
  
  lcd.setCursor(0, 2);
  lcd.print("> ");
  lcd.print(co2MinutesSet);
  lcd.print(" min");
  
  lcd.setCursor(0, 3);
  lcd.print("OK:Confirmar ESC:Atr");
}

void displayPhManualCO2Confirm() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIRMAR CO2?");
  
  lcd.setCursor(0, 1);
  lcd.print("Inyectar ");
  lcd.print(co2MinutesSet);
  lcd.print(" min");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("SI");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("NO");
}

void displayPhManualCO2Active() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CO2 ACTIVO");
  
  lcd.setCursor(0, 1);
  lcd.print("Tiempo: ");
  updateCO2Time();
  lcd.print(co2MinutesRemaining);
  lcd.print(":");
  int seconds = 60 - ((millis() - co2StartTime) / 1000) % 60;
  if (seconds < 10) lcd.print("0");
  lcd.print(seconds);
  
  lcd.setCursor(0, 2);
  lcd.print("Total: ");
  lcd.print(co2MinutesSet);
  lcd.print(" min");
  
  lcd.setCursor(0, 3);
  lcd.print("Click para detener");
}

void startCO2Injection() {
  co2InjectionActive = true;
  co2MinutesRemaining = co2MinutesSet;
  co2StartTime = millis();
  pcfOutput.digitalWrite(P2, LOW); // Activar CO2
}

void stopCO2Injection() {
  co2InjectionActive = false;
  co2MinutesRemaining = 0;
  pcfOutput.digitalWrite(P2, HIGH); // Desactivar CO2
}

void updateCO2Time() {
  if (co2InjectionActive) {
    unsigned long elapsed = (millis() - co2StartTime) / 60000; // minutos
    co2MinutesRemaining = co2MinutesSet - elapsed;
    
    if (co2MinutesRemaining <= 0) {
      stopCO2Injection();
      currentMenu = MENU_PH_PANEL;
      menuCursor = 1;
      updateDisplay();
    }
  }
}

void checkPhControl() {
  if (phControlActive) {
    if (phValue > phLimitSet + 0.2) {
      // pH alcalino - activar CO2
      if (!co2Active) {
        co2Active = true;
        pcfOutput.digitalWrite(P2, LOW);
      }
    } else if (phValue <= phLimitSet) {
      // pH en rango - desactivar CO2
      if (co2Active) {
        co2Active = false;
        pcfOutput.digitalWrite(P2, HIGH);
      }
    }
  }
}

void handleEmergencyState() {
  static bool lastEmergencyState = false;
  bool currentEmergencyState = digitalRead(EMERGENCY_PIN) == HIGH;  // HIGH = presionado
  
  // Detectar cuando se presiona el botón (transición de LOW a HIGH)
  if (currentEmergencyState && !lastEmergencyState) {
    emergencyActive = true;
    
    // Apagar todas las salidas
    for (int i = 0; i < 4; i++) {
      pcfOutput.digitalWrite(i, LOW);  // P0 a P3
      ledcWrite(ledPins[i], 0);        // LEDs PWM
    }
    
    // Detener llenado y aireación si están activos
    fillingActive = false;
    aireacionActive = false;
    co2Active = false;
    
    // Detener secuencias si están corriendo
    if (sequenceRunning) {
      stopSequence();
    }
    
    // Mostrar mensaje de emergencia
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("**  EMERGENCIA  **");
    lcd.setCursor(0, 1);
    lcd.print("BOTON PRESIONADO");
    lcd.setCursor(0, 2);
    lcd.print("Todas las salidas");
    lcd.setCursor(0, 3);
    lcd.print("DESACTIVADAS");
  }
  
  // Detectar cuando se suelta el botón (transición de HIGH a LOW)
  else if (!currentEmergencyState && lastEmergencyState && emergencyActive) {
    emergencyActive = false;
    
    // Volver al menú principal
    currentMenu = MENU_MAIN;
    menuCursor = 0;
    
    // Mostrar mensaje temporal
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Emergencia");
    lcd.setCursor(0, 2);
    lcd.print("Desactivada");
    delay(1500);
    
    updateDisplay();  // Mostrar menú principal
  }
  
  // Actualizar estado anterior
  lastEmergencyState = currentEmergencyState;
}