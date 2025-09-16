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
#include <EEPROM.h>
#include <PCF8574.h>

// === Configuración WiFi ===
const char* ap_ssid = "Bioreactor2";
const char* ap_password = "bioreactor2";

// === LCD 20x4 ===
LiquidCrystal_I2C lcd(0x27, 20, 4);

// === RTC DS3231 ===
RTC_DS3231 rtc;

// === Sensores ===
Adafruit_MAX31865 thermo = Adafruit_MAX31865(5, 23, 19, 18); // CS, MOSI, MISO, CLK
Adafruit_ADS1115 ads;

#define RREF      430.0
#define RNOMINAL  100.0

// === Pines del encoder ===
#define ENCODER_CLK 34
#define ENCODER_DT  35
#define ENCODER_SW  32

// === Pin del Boton Emergencia ===
#define EMERGENCY_PIN 39

#define BUZZER_PIN 33  // Pin PWM para el buzzer

// === Pin SQW del RTC ===
#define SQW_PIN 17

// Flujometro
#define FLOW_SENSOR_PIN 36
#define EEPROM_VOLUME_ADDR 4  // Dirección para guardar volumen
#define PULSOS_POR_LITRO 450

// Variables para control de alarmas
bool alarmActive = false;
bool tempAlarm = false;
bool phAlarm = false;
bool emergencyAlarm = false;
//unsigned long lastAlarmCheck = 0;
//const unsigned long alarmCheckInterval = 500; // Verificar cada 500ms
bool alarmSilenced = false;
bool tempWasNormal = false;
bool phWasNormal = false;
bool alarmWasSilenced = false;
// Flags para evitar registro duplicado de alarmas
bool tempAlarmLogged = false;  // Indica si ya se registró alarma de temperatura
bool phAlarmLogged = false;    // Indica si ya se registró alarma de pH
bool emergencyAlarmLogged = false;

#define MAX_ALARM_HISTORY 100
#define ALARM_LOG_FILE "Log/alarm_log.txt"
struct AlarmRecord {
  char timestamp[20];
  char type[30];
  float value;
};

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
const unsigned long debounceDelay = 500;

// Instancias PCF8574
PCF8574 pcfInput(0x20);
//PCF8574 pcfOutput(0x21);

// Direcciones EEPROM para turbidez
#define EEPROM_TURB_MUESTRA1_V 100  // Float (4 bytes)
#define EEPROM_TURB_MUESTRA1_C 104  // Float (4 bytes)
#define EEPROM_TURB_MUESTRA2_V 108  // Float (4 bytes)
#define EEPROM_TURB_MUESTRA2_C 112  // Float (4 bytes)
#define EEPROM_TURB_MUESTRA3_V 116  // Float (4 bytes)
#define EEPROM_TURB_MUESTRA3_C 120  // Float (4 bytes)
#define EEPROM_TURB_COEF_A 124      // Float (4 bytes)
#define EEPROM_TURB_COEF_B 128      // Float (4 bytes)
#define EEPROM_TURB_COEF_C 132      // Float (4 bytes)

// Direcciones EEPROM para pH
#define EEPROM_PH_MUESTRA1_V 140  // Float (4 bytes)
#define EEPROM_PH_MUESTRA1_PH 144 // Float (4 bytes)
#define EEPROM_PH_MUESTRA2_V 148  // Float (4 bytes)
#define EEPROM_PH_MUESTRA2_PH 152 // Float (4 bytes)
#define EEPROM_PH_COEF_M 156       // Float (4 bytes) - pendiente
#define EEPROM_PH_COEF_B 160       // Float (4 bytes) - intercepto

// Direcciones EEPROM para estado del sistema
#define EEPROM_SYSTEM_STATE 300      // 1 byte - flags de estado
#define EEPROM_LED_STATES 301        // 4 bytes - estados de LEDs
#define EEPROM_LED_PWM 305           // 4 bytes - valores PWM
#define EEPROM_SEQUENCE_RUNNING 309  // 1 byte - secuencia activa
#define EEPROM_SEQUENCE_ID 310       // 1 byte - ID de secuencia
#define EEPROM_SEQUENCE_STEP 311     // 1 byte - paso actual
#define EEPROM_AIREACION 312         // 1 byte - estado aireación
#define EEPROM_CO2 313               // 1 byte - estado CO2

// Agregar direcciones EEPROM para límites de temperatura
#define EEPROM_TEMP_MIN 170  // Float (4 bytes)
#define EEPROM_TEMP_MAX 174  // Float (4 bytes)

#define EEPROM_PH_LIMIT_MIN 178  // float (4 bytes)

#define EEPROM_CO2_MINUTES         180  // uint16_t (2 bytes) -> 180–181
#define EEPROM_CO2_TIMES           182  // uint16_t (2 bytes) -> 182–183
#define EEPROM_CO2_BUCLE           184  // uint8_t  (1 byte)  -> 184
#define EEPROM_CO2_ACTIVE          185  // uint8_t  (1 byte)  -> 185
#define EEPROM_CO2_INJECTIONS_DONE 186  // uint16_t (2 bytes) -> 186–187
#define EEPROM_CO2_REMAINING_SEC   188  // uint32_t (4 bytes) -> 188–191
#define EEPROM_INIT_FLAG           192  // uint8_t  (1 byte)  -> 192
#define EEPROM_MAGIC_NUMBER        0xAA // (constante, NO es dirección)

// Variables para calibración de turbidez
float turbMuestra1V = 0.0, turbMuestra1C = 0.0;
float turbMuestra2V = 0.0, turbMuestra2C = 0.0;
float turbMuestra3V = 0.0, turbMuestra3C = 0.0;
float turbCoefA = 0.0, turbCoefB = 0.0, turbCoefC = 0.0;
int turbCalibValue = 0;  // Valor temporal para calibración
int selectedMuestra = 0; // 0=Muestra1, 1=Muestra2, 2=Muestra3
float tempVoltageReading = 0.0; // Voltaje temporal actual
int tempConcentrationValue = 0; // Concentración temporal para editar

// Variables para calibración de pH
float phMuestra1V = 0.0, phMuestra1pH = 0.0;
float phMuestra2V = 0.0, phMuestra2pH = 0.0;
float phCoefM = 0.0, phCoefB = 0.0;
int phCalibValue = 7;  // Valor temporal para calibración (iniciar en pH neutro)
int selectedPhMuestra = 0; // 0=Muestra1, 1=Muestra2

// Variables para almacenamiento de datos
bool dataLogging[4] = {false, false, false, false}; // Estado de logging para cada tipo
unsigned long lastLogTime[4] = {0, 0, 0, 0}; // Último tiempo de logging para cada tipo
unsigned long logInterval = 5000; // 5 segundos en milisegundos (configurable)
int selectedDataType = 0; // Tipo seleccionado (0-3 para Tipo 1-4)

// Variables para límites de temperatura
float tempLimitMin = 18.0;  // Valor por defecto
float tempLimitMax = 28.0;  // Valor por defecto
float tempEditValue = 0.0;  // Valor temporal para edición
bool editingMin = true;      // Flag para saber qué límite se está editando

// Direcciones EEPROM para configuración de logging
#define EEPROM_LOG_INTERVAL 200  // unsigned long (4 bytes)

// === Estados del menú ===
enum MenuState {
  MENU_MAIN,           
  MENU_SENSORS,
  MENU_TEMP_LIMITS,
  MENU_TEMP_SET_MIN,
  MENU_TEMP_SET_MAX,
  MENU_TEMP_CONFIRM_SAVE,        
  MENU_SENSOR_PH,      
  MENU_PH_CALIBRATION_MENU,
  MENU_PH_SET_MUESTRA,
  MENU_PH_CONFIRM_MUESTRA,
  MENU_PH_CALIBRATING,
  MENU_PH_PANEL,              // Panel principal de pH
  MENU_PH_SET_LIMIT,          // Configurar pH límite
  MENU_SENSOR_TURBIDEZ,
  MENU_TURB_CALIBRATION,
  MENU_TURB_SET_MUESTRA,
  MENU_TURB_CONFIRM_MUESTRA,
  MENU_TURB_CALIBRATING,
  MENU_TURB_MUESTRA_DETAIL,
  MENU_TURB_SET_VOLTAGE,
  MENU_TURB_CONFIRM_VOLTAGE,
  MENU_TURB_SET_CONCENTRATION,
  MENU_TURB_CONFIRM_CONCENTRATION,
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
  MENU_SEQ_RUNNING_OPTIONS,
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
  MENU_POTENCIA,
  MENU_WEBSERVER,
  MENU_ALMACENAR,
  MENU_ALMACENAR_TYPE,
  MENU_ALMACENAR_CONFIRM_START,
  MENU_ALMACENAR_CONFIRM_STOP,
  MENU_ALMACENAR_CONFIRM_DELETE,
};

MenuState currentMenu = MENU_MAIN;
int menuCursor = 0;

// === Variables de sensores ===
float temperature = 0.0;
float phValue = 0.0;
float turbidityMCmL = 0.0;
unsigned long lastSensorRead = 0;
const unsigned long sensorReadInterval = 1000;

// === Variables para calibración pH ===
float calibrationValue = 0.0;
int calibrationStep = 0;

float phLimitSet = 7.0;      // pH límite establecido
float phLimitMin = 4.0;
bool phControlActive = false; // Control automático activo
bool co2InjectionActive = false; // Inyección manual activa
int co2MinutesSet = 0;       // Minutos de CO2 a inyectar
int co2MinutesRemaining = 0; // Minutos restantes
unsigned long co2StartTime = 0;
int co2TimesPerDay = 0;
int co2TimesSet = 0;
int co2InjectionsCompleted = 0;
bool co2BucleMode = false;
unsigned long co2NextInjectionTime = 0;
unsigned long co2IntervalMs = 0;
bool co2ScheduleActive = false;
unsigned long co2DailyStartTime = 0;

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

// Variables para encoder con interrupciones
volatile int encoderPos = 0;
volatile uint8_t encoderLastState = 0;
volatile bool encoderChanged = false;

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
  EEPROM.begin(512);
  loadTemperatureLimits();
  loadpHLimit();
  loadTurbidityCalibration();
  loadPhCalibration(); 
  
  EEPROM.get(EEPROM_VOLUME_ADDR, volumeTotal);
  if (isnan(volumeTotal) || volumeTotal < 0 || volumeTotal > 1000) {
  volumeTotal = 0.0;
  }
  
  EEPROM.get(EEPROM_LOG_INTERVAL, logInterval);
  if (logInterval == 0 || logInterval > 3600000) { // Máximo 1 hora
    logInterval = 5000; // 5 minutos por defecto
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
  //attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), readEncoderSimple, FALLING);
  pinMode(ENCODER_DT, INPUT);
  //attachInterrupt(digitalPinToInterrupt(ENCODER_DT), readEncoder, CHANGE);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(SQW_PIN, INPUT_PULLUP);
  pinMode(FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), contarPulso, FALLING);
  pinMode(EMERGENCY_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_PIN), handleEmergency, RISING);

  // Configurar PCF8574
  pcfInput.pinMode(P0, INPUT);
  pcfInput.pinMode(P1, INPUT_PULLUP);
  pcfInput.begin();

  /*
  pcfOutput.pinMode(P1, OUTPUT);  // Bomba de Agua
  pcfOutput.pinMode(P2, OUTPUT);  // Aireacion
  pcfOutput.pinMode(P3, OUTPUT);  // Solenoide
  pcfOutput.pinMode(P4, OUTPUT);  // LEd
  pcfOutput.digitalWrite(P1, HIGH);
  pcfOutput.digitalWrite(P2, HIGH);
  pcfOutput.digitalWrite(P3, HIGH);
  pcfOutput.digitalWrite(P4, HIGH);
  pcfOutput.begin();
  */

  // Configurar PWM para cada LED
  for (int i = 0; i < numLeds; i++) {
    ledcAttach(ledPins[i], pwmFreq, pwmResolution);
    ledcWrite(ledPins[i], 0);
  }
  
  //Buzzer
  ledcAttach(BUZZER_PIN, 2000, pwmResolution); // 2kHz frecuencia, 8 bits resolución
  ledcWrite(BUZZER_PIN, 0); // Inicialmente apagado

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
  
  // ADS1115
  /*
  ads.setGain(GAIN_TWOTHIRDS);
  if (!ads.begin()) {
    Serial.println("Error: No se detectó el ADS1115.");
    lcd.setCursor(10, 2);
    lcd.print("ADC Error!");
  } else {
    lcd.setCursor(10, 2);
    lcd.print("ADC OK");
  }
  */

  //delay(15000);

  // Inicializar SD
  lcd.setCursor(0, 3);
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(3000);
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
  
  // Verificar y reanudar logging si estaba activo
  for (int i = 0; i < 4; i++) {
    // Leer flag de logging desde archivo de configuración
    String configFile = "/config/config_tipo" + String(i + 1) + ".txt";
    if (SD.exists(configFile)) {
      File file = SD.open(configFile, FILE_READ);
      if (file) {
        String state = file.readStringUntil('\n');
        state.trim();
        if (state.equals("LOGGING")) {
          dataLogging[i] = true;
          lastLogTime[i] = millis();
          Serial.print("Reanudando logging Tipo ");
          Serial.println(i + 1);
        }
        file.close();
      }
    }
  }

  delay(1000);
  
  loadSystemState();

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
    // Verificar alarmas después de leer sensores
    checkAlarms();

    
    // Actualizar display si estamos en el menú de sensores
    if (currentMenu == MENU_SENSORS) {
      updateDisplay();
    }
  }
  
  // Actualizar medición de flujo continuamente
  updateFlowMeasurement();
  updateCO2Time();
  checkPhControl();
  checkDataLogging();
  
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

void saveSystemState() {
  // Guardar estados de LEDs
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_LED_STATES + i, ledStates[i] ? 1 : 0);
    EEPROM.write(EEPROM_LED_PWM + i, pwmValues[i]);
  }
  
  // Guardar estado de secuencia
  EEPROM.write(EEPROM_SEQUENCE_RUNNING, sequenceRunning ? 1 : 0);
  EEPROM.write(EEPROM_SEQUENCE_ID, selectedSequence);
  EEPROM.write(EEPROM_SEQUENCE_STEP, currentSequenceStep);
  
  // Guardar estados de aireación y CO2
  EEPROM.write(EEPROM_AIREACION, aireacionActive ? 1 : 0);
  EEPROM.write(EEPROM_CO2, co2Active ? 1 : 0);
  
  // Guardar estado de inyeccion manual de CO2
  saveCO2ToEEPROM();

  EEPROM.commit();
}

void loadSystemState() {
  // Cargar estados de LEDs
  for (int i = 0; i < 4; i++) {
    ledStates[i] = EEPROM.read(EEPROM_LED_STATES + i) == 1;
    pwmValues[i] = EEPROM.read(EEPROM_LED_PWM + i);
    
    // Aplicar valores a los LEDs
    if (ledStates[i]) {
      int pwmValue = map(pwmValues[i] * 5, 0, 100, 0, 255);
      ledcWrite(ledPins[i], pwmValue);
    }
  }
  
  // Cargar estado de secuencia
  bool wasSequenceRunning = EEPROM.read(EEPROM_SEQUENCE_RUNNING) == 1;
  if (wasSequenceRunning) {
    selectedSequence = EEPROM.read(EEPROM_SEQUENCE_ID);
    currentSequenceStep = EEPROM.read(EEPROM_SEQUENCE_STEP);
    
    // Verificar si la secuencia es válida antes de reanudar
    if (selectedSequence < 10 && sequences[selectedSequence].configured) {
      sequenceRunning = true;
      stepStartTime = rtc.now();
      applySequenceStep(currentSequenceStep);
      
      Serial.println("Reanudando secuencia tras reinicio");
    }
  }
  
  // Cargar y aplicar estados de aireación y CO2
  aireacionActive = EEPROM.read(EEPROM_AIREACION) == 1;
  if (aireacionActive) {
    //pcfOutput.digitalWrite(P2, LOW);
    Serial.println("Aireación reactivada tras reinicio");
  }
  
  co2Active = EEPROM.read(EEPROM_CO2) == 1;
  if (co2Active) {
    //pcfOutput.digitalWrite(P3, LOW);
    Serial.println("CO2 reactivado tras reinicio");
  }

  loadCO2FromEEPROM();

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
  IPAddress local_IP(192, 168, 4, 2);
  IPAddress gateway(192, 168, 4, 2);
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
    request->send(SD, "/Webserver/index.html", "text/html");
  });
  
  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/Webserver/styles.css", "text/css");
  });

  server.on("/chart.umd.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/Webserver/chart.umd.min.js", "text/js");
  });   

  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/Webserver/app.js", "text/js");
  });  
  
  // API para sensores
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"ph\":" + String(phValue, 2) + ",";
    json += "\"turbidity\":" + String(turbidityMCmL, 0);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Obtener límites de temperatura
  server.on("/api/temp/load/limits", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"min\":" + String(tempLimitMin, 1) + ",";
    json += "\"max\":" + String(tempLimitMax, 1);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Establecer límites de temperatura
  server.on("/api/temp/save/limits", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len == total) {
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error) {
          tempLimitMin = doc["min"];
          tempLimitMax = doc["max"];
          saveTemperatureLimits();
          request->send(200, "text/plain", "OK");
        } else {
          request->send(400, "text/plain", "Invalid JSON");
        }
      }
  });

  server.on("/api/ph/load/limit", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"min\":" + String(phLimitMin, 1);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Establecer límite mínimo de pH
  server.on("/api/ph/save/limit", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len == total) {
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error) {
          phLimitMin = doc["min"];
          savePhLimit();
          request->send(200, "text/plain", "OK");
        } else {
          request->send(400, "text/plain", "Invalid JSON");
        }
      }
  });

  // Estado de alarmas mejorado
  server.on("/api/alarm/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"active\":" + String(alarmActive ? "true" : "false") + ",";
    json += "\"tempHigh\":" + String((temperature > tempLimitMax && tempAlarm) ? "true" : "false") + ",";
    json += "\"tempLow\":" + String((temperature < tempLimitMin && tempAlarm) ? "true" : "false") + ",";
    json += "\"phLow\":" + String(phAlarm ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Obtener historial de alarmas
  server.on("/api/alarms/history", HTTP_GET, [](AsyncWebServerRequest *request){
    File alarmFile = SD.open(ALARM_LOG_FILE, FILE_READ);
    String json = "[";
    
    if (alarmFile) {
      boolean first = true;
      while (alarmFile.available()) {
        String line = alarmFile.readStringUntil('\n');
        if (line.length() > 0) {
          int firstComma = line.indexOf(',');
          int secondComma = line.indexOf(',', firstComma + 1);
          
          if (firstComma > 0 && secondComma > 0) {
            if (!first) json += ",";
            json += "{";
            json += "\"timestamp\":\"" + line.substring(0, firstComma) + "\",";
            json += "\"type\":\"" + line.substring(firstComma + 1, secondComma) + "\",";
            json += "\"value\":" + line.substring(secondComma + 1);
            json += "}";
            first = false;
          }
        }
      }
      alarmFile.close();
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // Borrar historial de alarmas
  server.on("/api/alarms/clear", HTTP_POST, [](AsyncWebServerRequest *request){
    SD.remove(ALARM_LOG_FILE);
    File alarmFile = SD.open(ALARM_LOG_FILE, FILE_WRITE);
    if (alarmFile) {
      alarmFile.close();
    }
    request->send(200, "text/plain", "OK");
  });

  // Silenciar alarma temporalmente
  server.on("/api/alarm/silence", HTTP_POST, [](AsyncWebServerRequest *request){
    silenceAlarmTemporary();
    request->send(200, "text/plain", "OK");
  });

  // Estado del control de pH
  server.on("/api/ph/status", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"phValue\":" + String(phValue, 2) + ",";
    json += "\"phLimit\":" + String(phLimitSet, 1) + ",";
    json += "\"autoControl\":" + String(phControlActive ? "true" : "false") + ",";
    json += "\"co2Active\":" + String(co2Active ? "true" : "false") + ",";
    json += "\"co2InjectionActive\":" + String(co2InjectionActive ? "true" : "false") + ",";
    json += "\"co2MinutesRemaining\":" + String(co2MinutesRemaining);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Activar control automático de pH
  server.on("/api/ph/auto/set", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len == total) {
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error) {
          phLimitSet = doc["limit"];
          phControlActive = true;
          request->send(200, "text/plain", "OK");
        } else {
          request->send(400, "text/plain", "Invalid JSON");
        }
      }
    });

  // Desactivar control automático
  server.on("/api/ph/auto/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    phControlActive = false;
    if (co2Active && !co2InjectionActive) {
      co2Active = false;
      //pcfOutput.digitalWrite(P3, HIGH);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/co2/manual/start", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len == total) {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (!error) {
          co2MinutesSet = doc["minutes"];
          co2MinutesRemaining = co2MinutesSet;
          co2TimesSet = doc["times"] | 0;
          co2TimesPerDay = co2TimesSet;
          co2BucleMode = doc["bucle"] | false;
          co2InjectionsCompleted = 0;
          co2DailyStartTime = millis();
          
          if (co2TimesPerDay > 0) {
            // Calcular intervalo entre inyecciones
            co2IntervalMs = (24UL * 3600 * 1000) / co2TimesPerDay;
            co2ScheduleActive = true;
            co2NextInjectionTime = millis() + co2IntervalMs;
            
            // Iniciar primera inyección
            startCO2InjectionCycle();
          } else {
            // Inyección única
            co2InjectionActive = true;
            co2StartTime = millis();
            //pcfOutput.digitalWrite(P3, LOW);
          }
          
          // Guardar en EEPROM
          saveCO2ToEEPROM();
          
          request->send(200, "text/plain", "OK");
        } else {
          request->send(400, "text/plain", "Invalid JSON");
        }
      }
  });

  // Pausar inyección de CO2
  server.on("/api/co2/manual/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    stopCO2Injection();
    co2ScheduleActive = false;
    co2TimesPerDay = 0;
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/co2/manual/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    co2InjectionActive = false;
    co2MinutesRemaining = 0;
    co2MinutesSet = 0;
    co2TimesPerDay = 0;
    co2TimesSet = 0;
    co2InjectionsCompleted = 0;
    co2BucleMode = false;
    co2ScheduleActive = false;
    //pcfOutput.digitalWrite(P3, HIGH);
    
    // Limpiar EEPROM
    clearCO2FromEEPROM();
    
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/co2/manual/complete", HTTP_POST, [](AsyncWebServerRequest *request){
  // Llamado cuando una inyección individual termina
  co2InjectionActive = false;
  //pcfOutput.digitalWrite(P3, HIGH);
  request->send(200, "text/plain", "OK");
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
      json += "\"intensity\":" + String(pwmValues[i] * 5); // Convertir de 0-10 a 0-100
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
  
  server.on("/api/sequence/*/start", HTTP_POST, [](AsyncWebServerRequest *request){
    String path = request->url();
    int startPos = path.indexOf("/sequence/") + 10;
    int endPos = path.indexOf("/start");
    int seqId = path.substring(startPos, endPos).toInt();
    
    if (seqId >= 0 && seqId < 10 && sequences[seqId].configured) {
      selectedSequence = seqId;
      sequenceLoopMode = false; // AGREGAR: Configurar modo por defecto
      startSequence();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Invalid sequence or not configured");
    }
  });

  // Iniciar secuencia en bucle
  server.on("/api/sequence/*/start/loop", HTTP_POST, [](AsyncWebServerRequest *request){
    String path = request->url();
    int startPos = path.indexOf("/sequence/") + 10;
    int endPos = path.indexOf("/start");
    int seqId = path.substring(startPos, endPos).toInt();
    
    if (seqId >= 0 && seqId < 10 && sequences[seqId].configured) {
      selectedSequence = seqId;
      sequenceLoopMode = true;
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
      json += ",\"loopMode\":" + String(sequenceLoopMode ? "true" : "false");
      
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
    //pcfOutput.digitalWrite(P2, LOW);
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/aireacion/off", HTTP_GET, [](AsyncWebServerRequest *request){
    aireacionActive = false;
    //pcfOutput.digitalWrite(P2, HIGH);
    request->send(200, "text/plain", "OK");
  });

  // Control de CO2
  server.on("/api/co2/on", HTTP_GET, [](AsyncWebServerRequest *request){
    co2Active = true;
    //pcfOutput.digitalWrite(P3, LOW);
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/co2/off", HTTP_GET, [](AsyncWebServerRequest *request){
    co2Active = false;
    //pcfOutput.digitalWrite(P3, HIGH);
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
    //pcfOutput.digitalWrite(P1, LOW);
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
  turbidityMCmL = getTurbidityConcentration();
  
  // Leer pH
  phValue = getPhValueCalibrated();
}

void handleEncoder() {
    static int lastClkState = HIGH;
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 5; // 5ms
    
    int clkState = digitalRead(ENCODER_CLK);
    
    if (clkState != lastClkState && (millis() - lastDebounceTime) > debounceDelay) {
        lastDebounceTime = millis();
        
        if (clkState == LOW) {
            if (digitalRead(ENCODER_DT) != clkState) {
                incrementCursor();
            } else {
                decrementCursor();
            }
            updateDisplay();
        }
        
        lastClkState = clkState;
    }
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
  if (pcfInput.digitalRead(P1) == 0) {
    if (millis() - lastExtraButtonPress > debounceDelay) {
      lastExtraButtonPress = millis();
    }
    handleExtraButton();
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
      
    case MENU_POTENCIA:
      currentMenu = MENU_MAIN;
      menuCursor = 5;
      updateDisplay();
      break;

    case MENU_ALMACENAR:
      currentMenu = MENU_MAIN;
      menuCursor = 6;
      updateDisplay();
      break;

    case MENU_WEBSERVER:
      currentMenu = MENU_MAIN;
      menuCursor = 7;
      updateDisplay();
      break;

    // Submenús de sensores
    case MENU_TEMP_LIMITS:
      currentMenu = MENU_SENSORS;
      menuCursor = 0;
      updateDisplay();
      break;

    case MENU_SENSOR_PH:
      currentMenu = MENU_SENSORS;
      menuCursor = 1;
      updateDisplay();
      break;

    case MENU_SENSOR_TURBIDEZ:
      currentMenu = MENU_SENSORS;
      menuCursor = 1;
      updateDisplay();
      break;    

    //Submenus de pH
    case MENU_PH_PANEL:
      currentMenu = MENU_SENSOR_PH;
      menuCursor = 0;
      updateDisplay();
      break;

    case MENU_PH_SET_LIMIT:
      phControlActive = false; 
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
      currentMenu = MENU_PH_PANEL;
      menuCursor = 1;
      updateDisplay();
      break;   

    case MENU_PH_CALIBRATION_MENU:
      currentMenu = MENU_SENSOR_PH;
      menuCursor = 0;
      updateDisplay();
      break;

    //Submenus de Turbidez
    case MENU_TURB_CALIBRATION:
      currentMenu = MENU_SENSOR_TURBIDEZ;
      menuCursor = 0;
      updateDisplay();
      break;

    case MENU_TURB_MUESTRA_DETAIL:
      currentMenu = MENU_SENSOR_TURBIDEZ;
      menuCursor = 1;
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
      
    case MENU_SEQ_CONFIG_CANTIDAD:
    case MENU_SEQ_CONFIG_COLOR:
    case MENU_SEQ_CONFIG_TIME:
    case MENU_SEQ_CONFIG_TIME_CONFIRM:
      currentMenu = MENU_SEQ_EXIT_CONFIG_CONFIRM;
      menuCursor = 1;
      updateDisplay();
      break;

    case MENU_SEQ_CONFIRM_SAVE:
      currentMenu = MENU_SEQ_LIST;
      menuCursor = selectedSequence;
      updateDisplay();
      break;

    case MENU_SEQ_RUNNING:
      currentMenu = MENU_SEQ_STOP_CONFIRM;
      menuCursor = 1;
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
  
    case MENU_ALMACENAR_TYPE:
      currentMenu = MENU_ALMACENAR;
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
      if (menuCursor > 7) menuCursor = 0; // Ahora tenemos 3 opciones
      break;
      
    case MENU_SENSORS:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0;
      break;

    case MENU_TEMP_LIMITS:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0; // 4 opciones
      break;

    case MENU_TEMP_SET_MIN:
    case MENU_TEMP_SET_MAX:
      if (tempEditValue < 30.0) {
        tempEditValue += 0.5; // Incrementos de 0.5°C
      }
      break;

    case MENU_TEMP_CONFIRM_SAVE:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_SENSOR_PH:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // Fijar, Calibrar, Atrás
      break;

    case MENU_PH_CALIBRATION_MENU:
      menuCursor++;
      if (menuCursor > 3) menuCursor = 0; // 4 opciones
      break;

    case MENU_PH_SET_MUESTRA:
      if (phCalibValue < 14) phCalibValue++;
      break;

    case MENU_PH_CONFIRM_MUESTRA:
      menuCursor = (menuCursor == 0) ? 1 : 0;
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
      
    case MENU_SENSOR_TURBIDEZ:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // 3 opciones
      break;

    case MENU_TURB_CALIBRATION:
      menuCursor++;
      if (menuCursor > 4) menuCursor = 0; // 5 opciones
      break;

    case MENU_TURB_SET_MUESTRA:
      if (turbCalibValue < 100) turbCalibValue++;
      break;

    case MENU_TURB_MUESTRA_DETAIL:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // 3 opciones
      break;

    case MENU_TURB_SET_VOLTAGE:
      // No se puede navegar, solo visualizar
      break;

    case MENU_TURB_SET_CONCENTRATION:
      if (tempConcentrationValue < 100) tempConcentrationValue++;
      break;

    case MENU_TURB_CONFIRM_VOLTAGE:
    case MENU_TURB_CONFIRM_CONCENTRATION:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_TURB_CONFIRM_MUESTRA:
      menuCursor = (menuCursor == 0) ? 1 : 0;
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
      menuCursor = constrain(menuCursor, 0, 20);
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

    case MENU_SEQ_RUNNING_OPTIONS:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_CO2:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // 3 opciones
      break;  
    
    case MENU_ALMACENAR:
      menuCursor++;
      if (menuCursor > 4) menuCursor = 0; // 5 opciones (4 tipos + atrás)
      break;

    case MENU_ALMACENAR_TYPE:
      menuCursor++;
      if (menuCursor > 2) menuCursor = 0; // 3 opciones
      break;

    case MENU_ALMACENAR_CONFIRM_START:
    case MENU_ALMACENAR_CONFIRM_STOP:
    case MENU_ALMACENAR_CONFIRM_DELETE:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

  }
}

void decrementCursor() {
  switch (currentMenu) {
    case MENU_MAIN:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 6;
      break;
      
    case MENU_SENSORS:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 3;
      break;

    case MENU_TEMP_LIMITS:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 3;
      break;

    case MENU_TEMP_SET_MIN:
    case MENU_TEMP_SET_MAX:
      if (tempEditValue > 10.0) {
        tempEditValue -= 0.5; // Decrementos de 0.5°C
      }
      break;

    case MENU_TEMP_CONFIRM_SAVE:
      menuCursor = (menuCursor == 0) ? 1 : 0;
      break;

    case MENU_SENSOR_PH:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2; // Ahora son 3 opciones: Fijar, Calibrar, Atrás
      break;
      
    case MENU_PH_CALIBRATION_MENU:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 3;
      break;

    case MENU_PH_SET_MUESTRA:
      if (phCalibValue > 0) phCalibValue--;
      break;

    case MENU_PH_CONFIRM_MUESTRA:
      menuCursor = (menuCursor == 0) ? 1 : 0;
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

    case MENU_SENSOR_TURBIDEZ:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2;
      break;

    case MENU_TURB_CALIBRATION:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 4;
      break;

    case MENU_TURB_SET_MUESTRA:
      if (turbCalibValue > 0) turbCalibValue--;
      break;

    case MENU_TURB_MUESTRA_DETAIL:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2;
      break;

    case MENU_TURB_SET_CONCENTRATION:
      if (tempConcentrationValue > 0) tempConcentrationValue--;
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
      menuCursor = constrain(menuCursor, 0, 20);
      if (menuCursor >= 1) {
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

    case MENU_SEQ_RUNNING_OPTIONS:
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

    case MENU_ALMACENAR:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 4; // 5 opciones (4 tipos + atrás)
      break;

    case MENU_ALMACENAR_TYPE:
      menuCursor--;
      if (menuCursor < 0) menuCursor = 2; // 3 opciones (Almacenar, Detener, Borrar)
      break;

    case MENU_ALMACENAR_CONFIRM_START:
    case MENU_ALMACENAR_CONFIRM_STOP:
    case MENU_ALMACENAR_CONFIRM_DELETE:
      menuCursor = (menuCursor == 0) ? 1 : 0;
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
      } else if (menuCursor == 5) {
        currentMenu = MENU_POTENCIA;
        menuCursor = 0;      
      } else if (menuCursor == 6) {
        currentMenu = MENU_ALMACENAR;
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
        // Temperatura - ir a fijar límites
        currentMenu = MENU_TEMP_LIMITS;
        menuCursor = 0;
      } else if (menuCursor == 1) {
        // pH - entrar a submenú
        currentMenu = MENU_SENSOR_PH;
        menuCursor = 0;
      } else if (menuCursor == 2) {
        // Turbidez - entrar a submenú
        currentMenu = MENU_SENSOR_TURBIDEZ;
        menuCursor = 0;
      } else {
        // Atrás
        currentMenu = MENU_MAIN;
        menuCursor = 0;
      }
      break;

    case MENU_TEMP_LIMITS:
      if (menuCursor == 0) {
        // Editar límite mínimo
        editingMin = true;
        tempEditValue = tempLimitMin;
        currentMenu = MENU_TEMP_SET_MIN;
      } else if (menuCursor == 1) {
        // Editar límite máximo
        editingMin = false;
        tempEditValue = tempLimitMax;
        currentMenu = MENU_TEMP_SET_MAX;
      } else if (menuCursor == 2) {
        // Guardar
        currentMenu = MENU_TEMP_CONFIRM_SAVE;
        menuCursor = 1; // Por defecto en NO
      } else {
        // Atrás
        currentMenu = MENU_SENSORS;
        menuCursor = 0;
      }
      break;

    case MENU_TEMP_SET_MIN:
      // Validar que mínimo sea menor que máximo
      if (tempEditValue >= tempLimitMax) {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Error: Min debe");
        lcd.setCursor(0, 2);
        lcd.print("ser menor que Max");
        delay(2000);
      } else {
        tempLimitMin = tempEditValue;
        currentMenu = MENU_TEMP_LIMITS;
        menuCursor = 0;
      }
      break;

    case MENU_TEMP_SET_MAX:
      // Validar que máximo sea mayor que mínimo
      if (tempEditValue <= tempLimitMin) {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Error: Max debe");
        lcd.setCursor(0, 2);
        lcd.print("ser mayor que Min");
        delay(2000);
      } else {
        tempLimitMax = tempEditValue;
        currentMenu = MENU_TEMP_LIMITS;
        menuCursor = 1;
      }
      break;

    case MENU_TEMP_CONFIRM_SAVE:
      if (menuCursor == 0) {
        // SI - Guardar en EEPROM
        saveTemperatureLimits();
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Limites guardados!");
        delay(1500);
        currentMenu = MENU_TEMP_LIMITS;
        menuCursor = 2;
      } else {
        // NO - Volver sin guardar
        currentMenu = MENU_TEMP_LIMITS;
        menuCursor = 2;
      }
      break;

    case MENU_SENSOR_PH:
      if (menuCursor == 0) {
        // Fijar - ir al panel de pH
        currentMenu = MENU_PH_PANEL;
        menuCursor = 0;
      } else if (menuCursor == 1) {
        // Calibración nueva
        currentMenu = MENU_PH_CALIBRATION_MENU;
        menuCursor = 0;
        loadPhCalibration(); // Cargar datos de EEPROM
      } else {
        // Atrás
        currentMenu = MENU_SENSORS;
        menuCursor = 1;
      }
      break;
      
    case MENU_PH_CALIBRATION_MENU:
      if (menuCursor < 2) {
        // Muestra 1 o 2
        selectedPhMuestra = menuCursor;
        currentMenu = MENU_PH_SET_MUESTRA;
        phCalibValue = 7; // Valor inicial pH neutro
      } else if (menuCursor == 2) {
        // Calibrar
        currentMenu = MENU_PH_CALIBRATING;
        performPhCalibration();
      } else {
        // Atrás
        currentMenu = MENU_SENSOR_PH;
        menuCursor = 1;
      }
      break;

    case MENU_PH_SET_MUESTRA:
      currentMenu = MENU_PH_CONFIRM_MUESTRA;
      menuCursor = 1; // Por defecto en NO
      break;

    case MENU_PH_CONFIRM_MUESTRA:
      if (menuCursor == 0) {
        // SI - Guardar
        savePhMuestra(selectedPhMuestra);
        currentMenu = MENU_PH_CALIBRATION_MENU;
        menuCursor = selectedPhMuestra;
      } else {
        // NO - Volver a editar
        currentMenu = MENU_PH_SET_MUESTRA;
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

    case MENU_SENSOR_TURBIDEZ:
      if (menuCursor == 0) {
        // Ver valor actual
        currentMenu = MENU_SENSORS;
        menuCursor = 2;
      } else if (menuCursor == 1) {
        // Calibrar
        currentMenu = MENU_TURB_CALIBRATION;
        menuCursor = 0;
        loadTurbidityCalibration(); // Cargar datos de EEPROM
      } else {
        // Atrás
        currentMenu = MENU_SENSORS;
        menuCursor = 2;
      }
      break;

    case MENU_TURB_CALIBRATION:
      if (menuCursor < 3) {
        // Muestra 1, 2 o 3 - ahora va a detalle
        selectedMuestra = menuCursor;
        currentMenu = MENU_TURB_MUESTRA_DETAIL;
        menuCursor = 0;
      } else if (menuCursor == 3) {
        // Calibrar
        currentMenu = MENU_TURB_CALIBRATING;
        performTurbidityCalibration();
      } else {
        // Atrás
        currentMenu = MENU_SENSOR_TURBIDEZ;
        menuCursor = 1;
      }
      break;

    case MENU_TURB_SET_MUESTRA:
      currentMenu = MENU_TURB_CONFIRM_MUESTRA;
      menuCursor = 1; // Por defecto en NO
      break;

    case MENU_TURB_CONFIRM_MUESTRA:
      if (menuCursor == 0) {
        // SI - Guardar
        saveTurbidityMuestra(selectedMuestra);
        currentMenu = MENU_TURB_CALIBRATION;
        menuCursor = selectedMuestra;
      } else {
        // NO - Volver a editar
        currentMenu = MENU_TURB_SET_MUESTRA;
      }
      break;

    case MENU_TURB_MUESTRA_DETAIL:
      if (menuCursor == 0) {
        // Editar Voltaje
        currentMenu = MENU_TURB_SET_VOLTAGE;
        tempVoltageReading = getTurbidityVoltage();
      } else if (menuCursor == 1) {
        // Editar Concentración
        currentMenu = MENU_TURB_SET_CONCENTRATION;
        // Cargar valor actual si existe
        if (selectedMuestra == 0) tempConcentrationValue = (int)turbMuestra1C;
        else if (selectedMuestra == 1) tempConcentrationValue = (int)turbMuestra2C;
        else if (selectedMuestra == 2) tempConcentrationValue = (int)turbMuestra3C;
      } else {
        // Atrás
        currentMenu = MENU_TURB_CALIBRATION;
        menuCursor = selectedMuestra;
      }
      break;

    case MENU_TURB_SET_VOLTAGE:
      currentMenu = MENU_TURB_CONFIRM_VOLTAGE;
      menuCursor = 1; // Por defecto en NO
      break;

    case MENU_TURB_CONFIRM_VOLTAGE:
      if (menuCursor == 0) {
        // SI - Guardar voltaje actual
        if (selectedMuestra == 0) {
          turbMuestra1V = tempVoltageReading;
          EEPROM.put(EEPROM_TURB_MUESTRA1_V, turbMuestra1V);
        } else if (selectedMuestra == 1) {
          turbMuestra2V = tempVoltageReading;
          EEPROM.put(EEPROM_TURB_MUESTRA2_V, turbMuestra2V);
        } else if (selectedMuestra == 2) {
          turbMuestra3V = tempVoltageReading;
          EEPROM.put(EEPROM_TURB_MUESTRA3_V, turbMuestra3V);
        }
        EEPROM.commit();
      }
      currentMenu = MENU_TURB_MUESTRA_DETAIL;
      menuCursor = 0;
      break;

    case MENU_TURB_SET_CONCENTRATION:
      currentMenu = MENU_TURB_CONFIRM_CONCENTRATION;
      menuCursor = 1; // Por defecto en NO
      break;

    case MENU_TURB_CONFIRM_CONCENTRATION:
      if (menuCursor == 0) {
        // SI - Guardar concentración
        if (selectedMuestra == 0) {
          turbMuestra1C = (float)tempConcentrationValue;
          EEPROM.put(EEPROM_TURB_MUESTRA1_C, turbMuestra1C);
        } else if (selectedMuestra == 1) {
          turbMuestra2C = (float)tempConcentrationValue;
          EEPROM.put(EEPROM_TURB_MUESTRA2_C, turbMuestra2C);
        } else if (selectedMuestra == 2) {
          turbMuestra3C = (float)tempConcentrationValue;
          EEPROM.put(EEPROM_TURB_MUESTRA3_C, turbMuestra3C);
        }
        EEPROM.commit();
      }
      currentMenu = MENU_TURB_MUESTRA_DETAIL;
      menuCursor = 1;
      break;

    // Modificar en handleSelection(), caso MENU_ACTION:
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
        // Secuencias - verificar si hay una activa
        if (sequenceRunning) {
          // Si hay secuencia activa, ir directamente a mostrarla
          currentMenu = MENU_SEQ_RUNNING;
        } else {
          currentMenu = MENU_SEQ_LIST;
          menuCursor = 0;
        }
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
          menuCursor = pwmValues[selectedLed];
        }
      }
      break;
      
    case MENU_ONOFF:
      if (menuCursor == 0) {
        // ON - Cambiar estas líneas
        ledStates[selectedLed] = true;
        pwmValues[selectedLed] = 20;  // AGREGAR ESTA LÍNEA
        ledcWrite(ledPins[selectedLed], 255);  // CAMBIAR de 'pwmValues[selectedAction]' a '255'
      } else {
        // OFF
        ledStates[selectedLed] = false;
        pwmValues[selectedLed] = 0;
        ledcWrite(ledPins[selectedLed], 0);
      }
      currentMenu = MENU_ACTION;
      menuCursor = 0;
      break;
      
    case MENU_INTENSITY:
      pwmValues[selectedLed] = menuCursor;
      ledStates[selectedLed] = (menuCursor > 0);
      //int pwmValue = map(intensity, 0, 100, 0, 255);
      ledcWrite(ledPins[selectedLed], map(menuCursor*5, 0, 100, 0, 255));
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
        sequences[selectedSequence].steps[currentConfigStep].colorIntensity[currentColorConfig] = menuCursor*5;        
        currentColorConfig++;
        if (currentColorConfig < 4) {
          menuCursor = sequences[selectedSequence].steps[currentConfigStep].colorIntensity[currentColorConfig]/5;
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
          menuCursor = sequences[selectedSequence].steps[currentConfigStep].colorIntensity[currentColorConfig]/5;
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
      currentMenu = MENU_SEQ_RUNNING_OPTIONS;
      menuCursor = 0;
      break;

    case MENU_SEQ_RUNNING_OPTIONS:
      if (menuCursor == 0) {
        // Detener Secuencia
        currentMenu = MENU_SEQ_STOP_CONFIRM;
        menuCursor = 1;
      } else {
        // Ir al Menú Principal sin detener la secuencia
        currentMenu = MENU_MAIN;
        menuCursor = 1;  // Cursor en "LEDs"
        // La secuencia continúa ejecutándose en segundo plano
      }
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
        //pcfOutput.digitalWrite(P1, LOW);
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
        //pcfOutput.digitalWrite(P2, LOW);
      } else if (menuCursor == 1) {
        // Apagar
        aireacionActive = false;
        //pcfOutput.digitalWrite(P2, HIGH);
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
      //pcfOutput.digitalWrite(P3, LOW);
    } else if (menuCursor == 1) {
      // Apagar
      co2Active = false;
      //pcfOutput.digitalWrite(P3, HIGH);
    } else {
      // Atrás
      currentMenu = MENU_MAIN;
      menuCursor = 4;
    }
    break;

  case MENU_POTENCIA:
    // Volver al menú principal
    currentMenu = MENU_MAIN;
    menuCursor = 5;
    break;

  case MENU_ALMACENAR:
  if (menuCursor < 4) {
    // Tipo 1-4
    selectedDataType = menuCursor;
    currentMenu = MENU_ALMACENAR_TYPE;
    menuCursor = 0;
  } else {
    // Atrás
    currentMenu = MENU_MAIN;
    menuCursor = 6;
  }
  break;

  case MENU_ALMACENAR_TYPE:
    if (menuCursor == 0) {
      // Almacenar
      if (!dataLogging[selectedDataType]) {
        currentMenu = MENU_ALMACENAR_CONFIRM_START;
        menuCursor = 1; // Por defecto en NO
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("Ya esta");
        lcd.setCursor(0, 2);
        lcd.print("almacenando!");
        delay(1500);
      }
    } else if (menuCursor == 1) {
      // Detener
      if (dataLogging[selectedDataType]) {
        currentMenu = MENU_ALMACENAR_CONFIRM_STOP;
        menuCursor = 1;
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("No hay datos");
        lcd.setCursor(0, 2);
        lcd.print("almacenandose");
        delay(1500);
      }
    } else {
      // Borrar
      currentMenu = MENU_ALMACENAR_CONFIRM_DELETE;
      menuCursor = 1;
    }
    break;

  case MENU_ALMACENAR_CONFIRM_START:
    if (menuCursor == 0) {
      // SI - Iniciar almacenamiento
      startDataLogging(selectedDataType);
      currentMenu = MENU_MAIN;
      menuCursor = 0;
    } else {
      // NO
      currentMenu = MENU_ALMACENAR_TYPE;
      menuCursor = 0;
    }
    break;

  case MENU_ALMACENAR_CONFIRM_STOP:
    if (menuCursor == 0) {
      // SI - Detener almacenamiento
      stopDataLogging(selectedDataType);
      currentMenu = MENU_ALMACENAR_TYPE;
      menuCursor = 1;
    } else {
      // NO
      currentMenu = MENU_ALMACENAR_TYPE;
      menuCursor = 1;
    }
    break;

  case MENU_ALMACENAR_CONFIRM_DELETE:
    if (menuCursor == 0) {
      // SI - Borrar datos
      deleteDataLog(selectedDataType);
      currentMenu = MENU_ALMACENAR_TYPE;
      menuCursor = 2;
    } else {
      // NO
      currentMenu = MENU_ALMACENAR_TYPE;
      menuCursor = 2;
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
    case MENU_TEMP_LIMITS:
      displayTempLimitsMenu();
      break;
    case MENU_TEMP_SET_MIN:
      displayTempSetLimit(true);
      break;
    case MENU_TEMP_SET_MAX:
      displayTempSetLimit(false);
      break;
    case MENU_TEMP_CONFIRM_SAVE:
      displayTempConfirmSave();
      break;
    case MENU_SENSOR_PH:
      displaySensorPhMenu();
      break;
    case MENU_PH_CALIBRATION_MENU:
      displayPhCalibrationMenu();
      break;
    case MENU_PH_SET_MUESTRA:
      displayPhSetMuestra();
      break;
    case MENU_PH_CONFIRM_MUESTRA:
      displayPhConfirmMuestra();
      break;
    case MENU_PH_CALIBRATING:
      displayPhCalibrating();
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
    case MENU_SENSOR_TURBIDEZ:
      displaySensorTurbidezMenu();
      break;
    case MENU_TURB_CALIBRATION:
      displayTurbCalibrationMenu();
      break;
    case MENU_TURB_SET_MUESTRA:
      displayTurbSetMuestra();
      break;
    case MENU_TURB_CONFIRM_MUESTRA:
      displayTurbConfirmMuestra();
      break;
    case MENU_TURB_CALIBRATING:
      displayTurbCalibrating();
      break;
    case MENU_TURB_MUESTRA_DETAIL:
      displayTurbMuestraDetail();
      break;
    case MENU_TURB_SET_VOLTAGE:
      displayTurbSetVoltage();
      break;
    case MENU_TURB_CONFIRM_VOLTAGE:
      displayTurbConfirmVoltage();
      break;
    case MENU_TURB_SET_CONCENTRATION:
      displayTurbSetConcentration();
      break;
    case MENU_TURB_CONFIRM_CONCENTRATION:
      displayTurbConfirmConcentration();
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
    case MENU_SEQ_RUNNING_OPTIONS:
      displaySeqRunningOptions();
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
    case MENU_POTENCIA:
      displayPotenciaMenu();
      break;
    case MENU_ALMACENAR:
      displayAlmacenarMenu();
      break;
    case MENU_ALMACENAR_TYPE:
      displayAlmacenarTypeMenu();
      break;
    case MENU_ALMACENAR_CONFIRM_START:
      displayAlmacenarConfirmStart();
      break;
    case MENU_ALMACENAR_CONFIRM_STOP:
      displayAlmacenarConfirmStop();
      break;
    case MENU_ALMACENAR_CONFIRM_DELETE:
      displayAlmacenarConfirmDelete();
      break;
  }
}

void displayMainMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEMA PRINCIPAL:");
  
  if (sequenceRunning) {
    lcd.setCursor(15, 0);
    lcd.print("[SEQ]");
  }

  // Con 6 opciones, necesitamos scroll
  int startIndex = 0;
  if (menuCursor > 2) {
    startIndex = menuCursor - 2;
    if (startIndex > 4) startIndex = 4; // Max 3 para mostrar las últimas 3
  }
  
  const char* opciones[] = {"Sensores", "LEDs", "Llenado", "Aireacion", "CO2", "Potencia", "Almacenar","WebServer"};
  
  for (int i = 0; i < 3; i++) {
    int optionIndex = startIndex + i;
    if (optionIndex < 9) {
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
  
  // Indicador de secuencia activa
  if (sequenceRunning) {
    lcd.setCursor(14, 0);
    lcd.print("[ACT]");
  }

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
      if (sequenceRunning) {
      lcd.print(" *");  // Indicador de que hay una activa
      }
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
  
  // Indicador de alarma activa
  if (alarmActive) {
    lcd.setCursor(12, 0);
    lcd.print("[ALARMA]");
  }

  // Temperatura con alerta
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Temp: ");
  lcd.print(temperature, 1);
  lcd.print(" C");
  
  // Mostrar alerta si está fuera de límites
  if (temperature < tempLimitMin) {
    lcd.setCursor(15, 1);
    lcd.print("[BAJ]");
  } else if (temperature > tempLimitMax) {
    lcd.setCursor(15, 1);
    lcd.print("[ALT]");
  }
  
  // pH
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("pH: ");
  lcd.print(phValue, 2);
  
  if (phAlarm) {
  lcd.setCursor(14, 2);
  lcd.print("[ALM]");
  }

  // Turbidez
  lcd.setCursor(0, 3);
  if (menuCursor < 3) {
    lcd.print(menuCursor == 2 ? "> " : "  ");
    lcd.print("Turb: ");
    if (turbidityMCmL > 0.0) {
      lcd.print(turbidityMCmL, 1);
      lcd.print(" MC/mL");
    } else {
      lcd.print("Sin Cal");
    }
  } else {
    lcd.print("> Atras");
  }
}

void displayTempLimitsMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LIMITES TEMP:");
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Min: ");
  lcd.print(tempLimitMin, 1);
  lcd.print(" C");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Max: ");
  lcd.print(tempLimitMax, 1);
  lcd.print(" C");
  
  lcd.setCursor(0, 3);
  if (menuCursor == 2) {
    lcd.print("> Guardar");
  } else if (menuCursor == 3) {
    lcd.print("> Atras");
  } else {
    lcd.print("  ");
    if (menuCursor < 2) {
      lcd.print("Guardar");
    }
  }
  
  // Mostrar temperatura actual
  lcd.setCursor(14, 0);
  lcd.print("(");
  lcd.print(temperature, 1);
  lcd.print(")");
}

void displayTempSetLimit(bool isMin) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(isMin ? "FIJAR TEMP MIN:" : "FIJAR TEMP MAX:");
  
  // Mostrar valor actual siendo editado
  lcd.setCursor(0, 1);
  lcd.print("Valor: ");
  lcd.print(tempEditValue, 1);
  lcd.print(" C");
  
  // Mostrar barra visual
  lcd.setCursor(0, 2);
  lcd.print("[");
  int barPos = map(tempEditValue * 10, 100, 300, 0, 16);
  for (int i = 0; i < 16; i++) {
    if (i == barPos) {
      lcd.print("|");
    } else if (i == 8) { // Marca en 20°C
      lcd.print("-");
    } else {
      lcd.print(" ");
    }
  }
  lcd.print("]");
  
  // Mostrar rango
  lcd.setCursor(0, 3);
  lcd.print("Rango: 10-30 C");
  
  // Mostrar límite opuesto como referencia
  lcd.setCursor(15, 3);
  if (isMin) {
    lcd.print("M:");
    lcd.print((int)tempLimitMax);
  } else {
    lcd.print("m:");
    lcd.print((int)tempLimitMin);
  }
}

void displayTempConfirmSave() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GUARDAR LIMITES?");
  
  lcd.setCursor(0, 1);
  lcd.print("Min: ");
  lcd.print(tempLimitMin, 1);
  lcd.print(" C");
  
  lcd.setCursor(0, 2);
  lcd.print("Max: ");
  lcd.print(tempLimitMax, 1);
  lcd.print(" C");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
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
  lcd.print("IP: ");
  lcd.print(WiFi.softAPIP());
}

void displaySensorPhMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SENSOR pH:");
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Fijar");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Calibracion");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
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
        if (pwmValues[optionIndex] == 20) {
          lcd.print("[ON]");
        } else {
          lcd.print("[");
          lcd.print(pwmValues[optionIndex]*5);
          lcd.print("%]");
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
    if (pwmValues[selectedLed] == 20) {
      lcd.print("[ON]");
    } else {
      lcd.print("[");
      lcd.print(pwmValues[selectedLed]*5);
      lcd.print("%]");
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
  menuCursor = constrain(menuCursor, 0, 20);
  int percentage = menuCursor * 5;  // Pasos de 5%
  lcd.print(percentage);
  lcd.print("%");
  if (percentage == 100) {
    lcd.print(" (MAX)");
  }
  
  lcd.setCursor(0, 2);
  lcd.print("[");
  int barLength = map(percentage, 0, 100, 0, 18);  // 20 pasos de 5% = 100%
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
    
    lcd.setCursor(0, 2);
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
      intensity = sequences[selectedSequence].steps[currentConfigStep].colorIntensity[i];
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
  pwmValues[index] = intensity / 5; // Convertir de 0-100 a 0-10 para consistencia con LCD
  
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
  saveSystemState();
}

void stopSequence() {
  sequenceRunning = false;
  
  for (int i = 0; i < numLeds; i++) {
    ledcWrite(ledPins[i], 0);
    ledStates[i] = false;
    pwmValues[i] = 0;
  }
  
  Serial.println("Secuencia detenida");
  saveSystemState();
}

void applySequenceStep(int step) {
  for (int i = 0; i < 4; i++) {
    int intensity = sequences[selectedSequence].steps[step].colorIntensity[i];
    int pwmValue = map(intensity, 0, 100, 0, 255);
    ledcWrite(ledPins[i], pwmValue);
    ledStates[i] = intensity > 0;
    pwmValues[i] = intensity/5;
    
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
        delay(50);
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

void saveTemperatureLimits() {
  EEPROM.put(EEPROM_TEMP_MIN, tempLimitMin);
  EEPROM.put(EEPROM_TEMP_MAX, tempLimitMax);
  EEPROM.commit();
  
  Serial.print("Límites temperatura guardados - Min: ");
  Serial.print(tempLimitMin);
  Serial.print(" Max: ");
  Serial.println(tempLimitMax);
}

void savePhLimit() {
  EEPROM.put(EEPROM_PH_LIMIT_MIN, phLimitMin);
  EEPROM.commit();
  Serial.println("Límite mínimo de pH guardado: " + String(phLimitMin));
}

void loadTemperatureLimits() {
  EEPROM.get(EEPROM_TEMP_MIN, tempLimitMin);
  EEPROM.get(EEPROM_TEMP_MAX, tempLimitMax);
  
  // Validar valores leídos
  if (isnan(tempLimitMin) || tempLimitMin < 10.0 || tempLimitMin > 30.0) {
    tempLimitMin = 18.0; // Valor por defecto
  }
  if (isnan(tempLimitMax) || tempLimitMax < 10.0 || tempLimitMax > 30.0) {
    tempLimitMax = 28.0; // Valor por defecto
  }
  
  // Asegurar que min < max
  if (tempLimitMin >= tempLimitMax) {
    tempLimitMin = 18.0;
    tempLimitMax = 28.0;
  }
  
  Serial.print("Límites temperatura cargados - Min: ");
  Serial.print(tempLimitMin);
  Serial.print(" Max: ");
  Serial.println(tempLimitMax);
}

void loadpHLimit() {
  EEPROM.get(EEPROM_PH_LIMIT_MIN, phLimitMin);
  if (isnan(phLimitMin) || phLimitMin < 0 || phLimitMin > 14) {
    phLimitMin = 6.0;  // Valor por defecto
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

void displaySeqRunningOptions() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SECUENCIA ACTIVA");
  
  // Mostrar info de la secuencia actual
  lcd.setCursor(0, 1);
  lcd.print("Seq");
  lcd.print(selectedSequence + 1);
  lcd.print(" Paso:");
  lcd.print(currentSequenceStep + 1);
  lcd.print("/");
  lcd.print(sequences[selectedSequence].stepCount);
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Detener Secuencia");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Menu Principal");
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
  
  //pcfOutput.digitalWrite(P1, LOW); // Activar bomba
}

void stopFilling() {
  fillingActive = false;
  //pcfOutput.digitalWrite(P1, HIGH); // Desactivar bomba
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
  
  // Lógica de scroll corregida para 4 opciones en 2 líneas
  int startIndex = 0;
  if (menuCursor == 0 || menuCursor == 1) {
    startIndex = 0;  // Mostrar opciones 0 y 1
  } else if (menuCursor == 2 || menuCursor == 3) {
    startIndex = 2;  // Mostrar opciones 2 y 3
  }
  
  const char* opciones[] = {"Reiniciar Volumen", "Llenar Tanque", "Encender Bomba", "Atras"};
  
  for (int i = 0; i < 2; i++) { // Solo 2 líneas disponibles
    int optionIndex = startIndex + i;
    if (optionIndex < 4) {
      lcd.setCursor(0, i + 2);
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      
      // Ajustar texto para que quepa en pantalla
      if (optionIndex == 0) {
        lcd.print("Reiniciar Vol.");
      } else {
        lcd.print(opciones[optionIndex]);
      }
    }
  }
  
  // Indicadores de scroll
  if (startIndex > 0) {
    lcd.setCursor(19, 2);
    lcd.print("^");
  }
  if (startIndex < 2) {
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
  //pcfOutput.digitalWrite(P3, LOW); // Activar CO2
}

void stopCO2Injection() {
  co2InjectionActive = false;
  co2MinutesRemaining = 0;
  if (!phControlActive || phValue <= phLimitSet) {
    co2Active = false;
    //pcfOutput.digitalWrite(P3, HIGH); // Desactivar CO2
  }
}

void startCO2InjectionCycle() {
  if (co2InjectionsCompleted < co2TimesPerDay) {
    co2InjectionActive = true;
    co2MinutesRemaining = co2MinutesSet;
    co2StartTime = millis();
    co2InjectionsCompleted++;
    //pcfOutput.digitalWrite(P3, LOW);
    
    Serial.print("Iniciando inyección ");
    Serial.print(co2InjectionsCompleted);
    Serial.print(" de ");
    Serial.println(co2TimesPerDay);
  }
}

void checkCO2Schedule() {
  // Verificar si es tiempo de la siguiente inyección
  if (co2ScheduleActive && !co2InjectionActive && millis() >= co2NextInjectionTime) {
    if (co2InjectionsCompleted < co2TimesPerDay) {
      startCO2InjectionCycle();
      co2NextInjectionTime = millis() + co2IntervalMs;
    } else if (co2BucleMode) {
      // Reiniciar ciclo en modo bucle
      co2InjectionsCompleted = 0;
      co2DailyStartTime = millis();
      startCO2InjectionCycle();
      co2NextInjectionTime = millis() + co2IntervalMs;
    } else {
      // Ciclo diario completado sin bucle
      co2ScheduleActive = false;
    }
  }
  
  // Verificar si han pasado 24 horas sin bucle
  if (co2ScheduleActive && !co2BucleMode && 
      (millis() - co2DailyStartTime) > (24UL * 3600 * 1000)) {
    stopCO2Injection();
    co2ScheduleActive = false;
  }
}

void saveCO2ToEEPROM() {
  EEPROM.write(EEPROM_CO2_MINUTES, co2MinutesSet);
  EEPROM.write(EEPROM_CO2_TIMES, co2TimesSet);
  EEPROM.write(EEPROM_CO2_BUCLE, co2BucleMode ? 1 : 0);
  EEPROM.write(EEPROM_CO2_ACTIVE, co2InjectionActive ? 1 : 0);
  EEPROM.write(EEPROM_CO2_INJECTIONS_DONE, co2InjectionsCompleted);
  
  // Guardar tiempo restante (2 bytes)
  int remainingSeconds = co2MinutesRemaining * 60;
  EEPROM.write(EEPROM_CO2_REMAINING_SEC, remainingSeconds & 0xFF);
  EEPROM.write(EEPROM_CO2_REMAINING_SEC + 1, (remainingSeconds >> 8) & 0xFF);
  
  EEPROM.write(EEPROM_INIT_FLAG, EEPROM_MAGIC_NUMBER);
  EEPROM.commit();
}

void clearCO2FromEEPROM() {
  for (int i = EEPROM_CO2_MINUTES; i <= EEPROM_CO2_REMAINING_SEC + 1; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

void loadCO2FromEEPROM() {
  if (EEPROM.read(EEPROM_INIT_FLAG) == EEPROM_MAGIC_NUMBER) {
    co2MinutesSet = EEPROM.read(EEPROM_CO2_MINUTES);
    co2TimesSet = EEPROM.read(EEPROM_CO2_TIMES);
    co2TimesPerDay = co2TimesSet;
    co2BucleMode = EEPROM.read(EEPROM_CO2_BUCLE) == 1;
    co2InjectionsCompleted = EEPROM.read(EEPROM_CO2_INJECTIONS_DONE);
    
    bool wasActive = EEPROM.read(EEPROM_CO2_ACTIVE) == 1;
    
    if (wasActive) {
      // Recuperar tiempo restante
      int remainingSeconds = EEPROM.read(EEPROM_CO2_REMAINING_SEC) | 
                           (EEPROM.read(EEPROM_CO2_REMAINING_SEC + 1) << 8);
      co2MinutesRemaining = remainingSeconds / 60;
      
      if (co2MinutesRemaining > 0) {
        // Reanudar inyección
        co2InjectionActive = true;
        co2StartTime = millis();
        //pcfOutput.digitalWrite(P3, LOW);
        
        if (co2TimesPerDay > 0) {
          co2IntervalMs = (24UL * 3600 * 1000) / co2TimesPerDay;
          co2ScheduleActive = true;
          co2NextInjectionTime = millis() + co2IntervalMs;
        }
      }
    }
    
    Serial.println("CO2 configuración recuperada de EEPROM");
  }
}

void updateCO2Time() {
  if (co2InjectionActive) {
    unsigned long elapsed = (millis() - co2StartTime) / 60000; // minutos
    co2MinutesRemaining = co2MinutesSet - elapsed;
    
    if (co2MinutesRemaining <= 0) {
      co2InjectionActive = false;
      //pcfOutput.digitalWrite(P3, HIGH);
      
      // Si hay programación activa, se manejará en checkCO2Schedule()
      if (!co2ScheduleActive) {
        stopCO2Injection();
      }
      
      if (currentMenu == MENU_PH_MANUAL_CO2_ACTIVE) {
        currentMenu = MENU_PH_PANEL;
        menuCursor = 1;
        updateDisplay();
      }
    }
  }
  
  // Verificar programación
  checkCO2Schedule();
}

void checkPhControl() {
  if (phControlActive) {
    // Usar histéresis para evitar oscilaciones
    static bool co2WasActive = false;
    
    if (phValue > phLimitSet + 0.2) {
      // pH alcalino - activar CO2
      if (!co2Active && !co2InjectionActive) {
        co2Active = true;
        co2WasActive = true;
        //pcfOutput.digitalWrite(P3, LOW);
        Serial.println("pH alto - Activando CO2");
      }
    } else if (phValue <= phLimitSet - 0.1) {
      // pH en rango o ácido - desactivar CO2
      if (co2Active && co2WasActive && !co2InjectionActive) {
        co2Active = false;
        co2WasActive = false;
        //pcfOutput.digitalWrite(P3, HIGH);
        Serial.println("pH en rango - Desactivando CO2");
      }
    }
  }
}

// Funciones vacías para obtener los valores
float getVoltage() {
    // TODO: Implementar lectura de voltaje
    return 0.0;
}

float getCurrent() {
    // TODO: Implementar lectura de corriente
    return 0.0;
}

float getPower() {
    // TODO: Calcular potencia (V * I)
    return getVoltage() * getCurrent();
}

float getConsumption() {
    // TODO: Calcular consumo acumulado en kWh
    static float totalKwh = 0.0;
    return totalKwh;
}

void displayPotenciaMenu() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MEDIDOR POTENCIA:");
    
    // Mostrar voltaje
    lcd.setCursor(0, 1);
    lcd.print("V: ");
    lcd.print(getVoltage(), 1);
    lcd.print("V");
    
    // Mostrar corriente
    lcd.setCursor(10, 1);
    lcd.print("I: ");
    lcd.print(getCurrent(), 2);
    lcd.print("A");
    
    // Mostrar potencia
    lcd.setCursor(0, 2);
    lcd.print("P: ");
    lcd.print(getPower(), 1);
    lcd.print("W");
    
    // Mostrar consumo
    lcd.setCursor(0, 3);
    lcd.print("Consumo: ");
    lcd.print(getConsumption(), 3);
    lcd.print(" kWh");
    
    // Indicador para volver
    lcd.setCursor(15, 3);
    lcd.print("<Back");
}

void displaySensorTurbidezMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SENSOR TURBIDEZ:");
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Ver valor");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Calibracion");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
}

void displayTurbCalibrationMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRAR TURBIDEZ:");
  
  int startIndex = 0;
  if (menuCursor > 2) startIndex = menuCursor - 2;
  if (startIndex > 2) startIndex = 2;
  
  const char* opciones[] = {"Muestra 1", "Muestra 2", "Muestra 3", "Calibrar", "Atras"};
  
  for (int i = 0; i < 3; i++) {
    int optionIndex = startIndex + i;
    if (optionIndex < 5) {
      lcd.setCursor(0, i + 1);
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      lcd.print(opciones[optionIndex]);
      
      // Mostrar si hay datos completos
      if (optionIndex < 3) {
        float v = 0, c = 0;
        if (optionIndex == 0) { v = turbMuestra1V; c = turbMuestra1C; }
        else if (optionIndex == 1) { v = turbMuestra2V; c = turbMuestra2C; }
        else if (optionIndex == 2) { v = turbMuestra3V; c = turbMuestra3C; }
        
        lcd.setCursor(14, i + 1);
        if (v > 0 && c > 0) {
          lcd.print("[OK]");
        } else if (v > 0 || c > 0) {
          lcd.print("[..]");
        }
      }
    }
  }
}

void displayTurbSetMuestra() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MUESTRA ");
  lcd.print(selectedMuestra + 1);
  lcd.print(":");
  
  // Mostrar voltaje actual
  float voltage = getTurbidityVoltage();
  lcd.setCursor(0, 1);
  lcd.print("Voltaje: ");
  lcd.print(voltage, 3);
  lcd.print("V");
  
  // Configurar concentración
  lcd.setCursor(0, 2);
  lcd.print("MC/mL: ");
  lcd.print(turbCalibValue);
  lcd.print("  ");
  
  lcd.setCursor(0, 3);
  lcd.print("Click para guardar");
}

void displayTurbConfirmMuestra() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIRMAR MUESTRA?");
  
  lcd.setCursor(0, 1);
  lcd.print("V: ");
  lcd.print(getTurbidityVoltage(), 3);
  lcd.print("V");
  
  lcd.setCursor(0, 2);
  lcd.print("C: ");
  lcd.print(turbCalibValue);
  lcd.print(" MC/mL");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void displayTurbCalibrating() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRANDO...");
  
  lcd.setCursor(0, 1);
  lcd.print("Calculando");
  lcd.setCursor(0, 2);
  lcd.print("regresion...");
  
  delay(1500);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRACION OK!");
  lcd.setCursor(0, 1);
  lcd.print("a=");
  lcd.print(turbCoefA, 2);
  lcd.setCursor(0, 2);
  lcd.print("b=");
  lcd.print(turbCoefB, 2);
  lcd.setCursor(0, 3);
  lcd.print("c=");
  lcd.print(turbCoefC, 2);
  
  delay(2000);
  currentMenu = MENU_TURB_CALIBRATION;
  menuCursor = 3;
}

float getTurbidityVoltage() {
  // TODO: Leer del sensor de turbidez conectado al ADC
  //int16_t adc = ads.readADC_SingleEnded(0); // Canal A3 del ADS1115
  //float voltage = ads.computeVolts(adc);
  float voltage = 0.0;
  return voltage;
}

void saveTurbidityMuestra(int muestra) {
  float voltage = getTurbidityVoltage();
  float concentration = (float)turbCalibValue;
  
  if (muestra == 0) {
    turbMuestra1V = voltage;
    turbMuestra1C = concentration;
    EEPROM.put(EEPROM_TURB_MUESTRA1_V, voltage);
    EEPROM.put(EEPROM_TURB_MUESTRA1_C, concentration);
  } else if (muestra == 1) {
    turbMuestra2V = voltage;
    turbMuestra2C = concentration;
    EEPROM.put(EEPROM_TURB_MUESTRA2_V, voltage);
    EEPROM.put(EEPROM_TURB_MUESTRA2_C, concentration);
  } else if (muestra == 2) {
    turbMuestra3V = voltage;
    turbMuestra3C = concentration;
    EEPROM.put(EEPROM_TURB_MUESTRA3_V, voltage);
    EEPROM.put(EEPROM_TURB_MUESTRA3_C, concentration);
  }
  EEPROM.commit();
}

void loadTurbidityCalibration() {
  EEPROM.get(EEPROM_TURB_MUESTRA1_V, turbMuestra1V);
  EEPROM.get(EEPROM_TURB_MUESTRA1_C, turbMuestra1C);
  EEPROM.get(EEPROM_TURB_MUESTRA2_V, turbMuestra2V);
  EEPROM.get(EEPROM_TURB_MUESTRA2_C, turbMuestra2C);
  EEPROM.get(EEPROM_TURB_MUESTRA3_V, turbMuestra3V);
  EEPROM.get(EEPROM_TURB_MUESTRA3_C, turbMuestra3C);
  EEPROM.get(EEPROM_TURB_COEF_A, turbCoefA);
  EEPROM.get(EEPROM_TURB_COEF_B, turbCoefB);
  EEPROM.get(EEPROM_TURB_COEF_C, turbCoefC);
}

void performTurbidityCalibration() {
  // Regresión polinomial de 2do grado: C = a*V^2 + b*V + c
  // Usando mínimos cuadrados con 3 puntos
  
  float x1 = turbMuestra1V, y1 = turbMuestra1C;
  float x2 = turbMuestra2V, y2 = turbMuestra2C;
  float x3 = turbMuestra3V, y3 = turbMuestra3C;
  
  // Matriz para resolver sistema de ecuaciones
  float denom = (x1 - x2) * (x1 - x3) * (x2 - x3);
  
  if (abs(denom) > 0.001) { // Evitar división por cero
    turbCoefA = (x3*(y2-y1) + x2*(y1-y3) + x1*(y3-y2)) / denom;
    turbCoefB = (x3*x3*(y1-y2) + x2*x2*(y3-y1) + x1*x1*(y2-y3)) / denom;
    turbCoefC = (x2*x3*(x2-x3)*y1 + x3*x1*(x3-x1)*y2 + x1*x2*(x1-x2)*y3) / denom;
    
    // Guardar en EEPROM
    EEPROM.put(EEPROM_TURB_COEF_A, turbCoefA);
    EEPROM.put(EEPROM_TURB_COEF_B, turbCoefB);
    EEPROM.put(EEPROM_TURB_COEF_C, turbCoefC);
    EEPROM.commit();
  }
}

float getTurbidityConcentration() {
  // Si no hay calibración, retornar -1
  if (turbCoefA == 0 && turbCoefB == 0 && turbCoefC == 0) {
    return 0.0;
  }
  
  float voltage = getTurbidityVoltage();

  float concentration = turbCoefA * voltage * voltage + turbCoefB * voltage + turbCoefC;
  
  if (concentration < 0) concentration = 0;
  if (concentration > 100) concentration = 100;

  if (!isfinite(concentration)) {            
    return 0.0;
  } else {
    return concentration;
  }

}

void displayPhCalibrationMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRAR pH:");
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Muestra 1");
  if (phMuestra1pH > 0) {
    lcd.setCursor(14, 1);
    lcd.print("[OK]");
  }
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Muestra 2");
  if (phMuestra2pH > 0) {
    lcd.setCursor(14, 2);
    lcd.print("[OK]");
  }
  
  lcd.setCursor(0, 3);
  if (menuCursor == 2) {
    lcd.print("> Calibrar");
  } else if (menuCursor == 3) {
    lcd.print("> Atras");
  } else {
    lcd.print("  ");
    lcd.print(menuCursor == 2 ? "Calibrar" : "Atras");
  }
}

void displayPhSetMuestra() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("pH MUESTRA ");
  lcd.print(selectedPhMuestra + 1);
  lcd.print(":");
  
  // Mostrar voltaje actual
  float voltage = getPhVoltage();
  lcd.setCursor(0, 1);
  lcd.print("Voltaje: ");
  lcd.print(voltage, 3);
  lcd.print("V");
  
  // Configurar pH
  lcd.setCursor(0, 2);
  lcd.print("pH: ");
  if (phCalibValue < 10) lcd.print(" ");
  lcd.print(phCalibValue);
  lcd.print(".0");
  
  // Barra visual del pH
  lcd.setCursor(0, 3);
  lcd.print("[");
  int barPos = map(phCalibValue, 0, 14, 0, 14);
  for (int i = 0; i < 14; i++) {
    if (i == barPos) lcd.print("|");
    else if (i == 7) lcd.print("-");
    else lcd.print(" ");
  }
  lcd.print("]");
}

void displayPhConfirmMuestra() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIRMAR pH?");
  
  lcd.setCursor(0, 1);
  lcd.print("V: ");
  lcd.print(getPhVoltage(), 3);
  lcd.print("V");
  
  lcd.setCursor(0, 2);
  lcd.print("pH: ");
  lcd.print(phCalibValue);
  lcd.print(".0");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void displayPhCalibrating() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CALIBRANDO pH...");
  
  lcd.setCursor(0, 1);
  lcd.print("Calculando");
  lcd.setCursor(0, 2);
  lcd.print("regresion lineal...");
  
  delay(1500);
  
  // Verificar si la calibración fue exitosa
  if (phCoefM != 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CALIBRACION OK!");
    lcd.setCursor(0, 1);
    lcd.print("m = ");
    lcd.print(phCoefM, 3);
    lcd.setCursor(0, 2);
    lcd.print("b = ");
    lcd.print(phCoefB, 3);
    lcd.setCursor(0, 3);
    lcd.print("pH = m*V + b");
  } else {
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Error: Faltan");
    lcd.setCursor(0, 2);
    lcd.print("muestras!");
  }
  
  delay(2000);
  currentMenu = MENU_PH_CALIBRATION_MENU;
  menuCursor = 2;
}

float getPhVoltage() {
  // Leer del sensor de pH conectado al ADC
  //int16_t adc = ads.readADC_SingleEnded(1); // Canal A1 del ADS1115 para pH
  //float voltage = ads.computeVolts(adc);
  float voltage = 0.0;
  return voltage;
}

void savePhMuestra(int muestra) {
  float voltage = getPhVoltage();
  float ph = (float)phCalibValue;
  
  if (muestra == 0) {
    phMuestra1V = voltage;
    phMuestra1pH = ph;
    EEPROM.put(EEPROM_PH_MUESTRA1_V, voltage);
    EEPROM.put(EEPROM_PH_MUESTRA1_PH, ph);
  } else if (muestra == 1) {
    phMuestra2V = voltage;
    phMuestra2pH = ph;
    EEPROM.put(EEPROM_PH_MUESTRA2_V, voltage);
    EEPROM.put(EEPROM_PH_MUESTRA2_PH, ph);
  }
  EEPROM.commit();
}

void loadPhCalibration() {
  EEPROM.get(EEPROM_PH_MUESTRA1_V, phMuestra1V);
  EEPROM.get(EEPROM_PH_MUESTRA1_PH, phMuestra1pH);
  EEPROM.get(EEPROM_PH_MUESTRA2_V, phMuestra2V);
  EEPROM.get(EEPROM_PH_MUESTRA2_PH, phMuestra2pH);
  EEPROM.get(EEPROM_PH_COEF_M, phCoefM);
  EEPROM.get(EEPROM_PH_COEF_B, phCoefB);
}

void performPhCalibration() {
  // Regresión lineal: pH = m*V + b
  // Usando 2 puntos
  
  if (phMuestra1V != phMuestra2V && phMuestra1pH > 0 && phMuestra2pH > 0) {
    // Calcular pendiente
    phCoefM = (phMuestra2pH - phMuestra1pH) / (phMuestra2V - phMuestra1V);
    
    // Calcular intercepto
    phCoefB = phMuestra1pH - phCoefM * phMuestra1V;
    
    // Guardar en EEPROM
    EEPROM.put(EEPROM_PH_COEF_M, phCoefM);
    EEPROM.put(EEPROM_PH_COEF_B, phCoefB);
    EEPROM.commit();
  } else {
    // Error - voltajes iguales o faltan muestras
    phCoefM = 0;
    phCoefB = 0;
  }
}

float getPhValueCalibrated() {
  // Si no hay calibración, retornar valor por defecto
  if (phCoefM == 0) {
    return 7.0; // pH neutro por defecto
  }
  
  float voltage = getPhVoltage();

  float ph = phCoefM * voltage + phCoefB;
  
  // Limitar el rango de pH entre 0 y 14
  if (ph < 0) ph = 0;
  if (ph > 14) ph = 14;
  
  if (!isfinite(ph)) {            
    return 0.0;
  } else {
    return ph;
  }

}

void displayAlmacenarMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ALMACENAR DATOS:");
  
  int startIndex = 0;
  if (menuCursor > 2) startIndex = menuCursor - 2;
  if (startIndex > 2) startIndex = 2;
  
  for (int i = 0; i < 3; i++) {
    int optionIndex = startIndex + i;
    if (optionIndex < 5) {
      lcd.setCursor(0, i + 1);
      lcd.print(menuCursor == optionIndex ? "> " : "  ");
      
      if (optionIndex < 4) {
        lcd.print("Tipo ");
        lcd.print(optionIndex + 1);
        
        // Mostrar si está activo
        if (dataLogging[optionIndex]) {
          lcd.setCursor(14, i + 1);
          lcd.print("[REC]");
        }
      } else {
        lcd.print("Atras");
      }
    }
  }
}

void displayAlmacenarTypeMenu() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TIPO ");
  lcd.print(selectedDataType + 1);
  lcd.print(":");
  
  if (dataLogging[selectedDataType]) {
    lcd.print(" [REC]");
  }
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("Almacenar");
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("Detener");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Borrar");
}

void displayAlmacenarConfirmStart() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INICIAR GUARDADO?");
  
  lcd.setCursor(0, 1);
  lcd.print("Tipo ");
  lcd.print(selectedDataType + 1);
  
  lcd.setCursor(0, 2);
  lcd.print("Intervalo: ");
  lcd.print(logInterval / 1000);
  lcd.print(" seg");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void displayAlmacenarConfirmStop() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DETENER GUARDADO?");
  
  lcd.setCursor(0, 1);
  lcd.print("Tipo ");
  lcd.print(selectedDataType + 1);
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void displayAlmacenarConfirmDelete() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("BORRAR DATOS?");
  
  lcd.setCursor(0, 1);
  lcd.print("Tipo ");
  lcd.print(selectedDataType + 1);
  
  lcd.setCursor(0, 2);
  lcd.print("IRREVERSIBLE!");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void startDataLogging(int type) {
  dataLogging[type] = true;
  lastLogTime[type] = millis();
  
  // Crear archivo de configuración para mantener estado
  String configFile = "/config/config_tipo" + String(type + 1) + ".txt";
  File file = SD.open(configFile, FILE_WRITE);
  if (file) {
    file.println("LOGGING");
    file.close();
  }
  
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Iniciando");
  lcd.setCursor(0, 2);
  lcd.print("almacenamiento...");
  delay(1500);
}

void stopDataLogging(int type) {
  dataLogging[type] = false;
  
  // Eliminar archivo de configuración
  String configFile = "/config/config_tipo" + String(type + 1) + ".txt";
  SD.remove(configFile);
  
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Almacenamiento");
  lcd.setCursor(0, 2);
  lcd.print("detenido");
  delay(1500);
}

void deleteDataLog(int type) {
  String filename = "/Datos/datos_tipo" + String(type + 1) + ".txt";
  
  if (SD.exists(filename)) {
    SD.remove(filename);
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Datos borrados");
    Serial.print("Archivo eliminado: ");
    Serial.println(filename);
  } else {
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("No hay datos");
  }
  delay(1500);
}

void saveDataToSD(int type) {
  String filename = "/Datos/datos_tipo" + String(type + 1) + ".txt";
  
  // Verificar si el archivo existe, si no, crear con encabezados
  bool fileExists = SD.exists(filename);
  
  File dataFile = SD.open(filename, FILE_APPEND);
  
  if (dataFile) {
    // Si es un archivo nuevo, agregar encabezados
    if (!fileExists) {
      dataFile.println("Fecha;Hora;Tipo;Temperatura_C;Turbidez_MCmL;pH;Volumen_L;Aireacion;CO2;LED_Blanco;LED_Rojo;LED_Verde;LED_Azul;Secuencia_Activa;Secuencia_ID;Paso_Actual;Total_Pasos;Tiempo_Paso_seg;Modo_Bucle");
    }
    
    // Obtener fecha y hora
    DateTime now = rtc.now();
    
    // Escribir fecha
    dataFile.print(now.year());
    dataFile.print("/");
    if (now.month() < 10) dataFile.print("0");
    dataFile.print(now.month());
    dataFile.print("/");
    if (now.day() < 10) dataFile.print("0");
    dataFile.print(now.day());
    dataFile.print(";");
    
    // Escribir hora
    if (now.hour() < 10) dataFile.print("0");
    dataFile.print(now.hour());
    dataFile.print(":");
    if (now.minute() < 10) dataFile.print("0");
    dataFile.print(now.minute());
    dataFile.print(":");
    if (now.second() < 10) dataFile.print("0");
    dataFile.print(now.second());
    dataFile.print(";");
    
    // Tipo de experimento
    dataFile.print(type + 1);
    dataFile.print(";");
    
    // Sensores
    dataFile.print(temperature, 2);
    dataFile.print(";");
    
    float turbidityValue = getTurbidityConcentration();
    if (turbidityValue >= 0) {
      dataFile.print(turbidityValue, 2);
    } else {
      dataFile.print("N/A");
    }
    dataFile.print(";");
    
    dataFile.print(phValue, 2);
    dataFile.print(";");
    
    // Volumen total
    dataFile.print(volumeTotal, 2);
    dataFile.print(";");
    
    // Estados de aireación y CO2
    dataFile.print(aireacionActive ? "ON" : "OFF");
    dataFile.print(";");
    dataFile.print(co2Active ? "ON" : "OFF");
    dataFile.print(";");
    
    // Intensidad de LEDs (en porcentaje)
    dataFile.print(pwmValues[0] * 5); // Blanco
    dataFile.print(";");
    dataFile.print(pwmValues[1] * 5); // Rojo
    dataFile.print(";");
    dataFile.print(pwmValues[2] * 5); // Verde
    dataFile.print(";");
    dataFile.print(pwmValues[3] * 5); // Azul
    dataFile.print(";");
    
    // Información de secuencia
    dataFile.print(sequenceRunning ? "SI" : "NO");
    dataFile.print(";");
    
    if (sequenceRunning) {
      dataFile.print(selectedSequence + 1);
      dataFile.print(";");
      dataFile.print(currentSequenceStep + 1);
      dataFile.print(";");
      dataFile.print(sequences[selectedSequence].stepCount);
      dataFile.print(";");
      
      // Calcular tiempo transcurrido en el paso actual
      DateTime now = rtc.now();
      TimeSpan elapsed = now - stepStartTime;
      int elapsedSeconds = elapsed.days() * 86400 + elapsed.hours() * 3600 + 
                          elapsed.minutes() * 60 + elapsed.seconds();
      dataFile.print(elapsedSeconds);
      dataFile.print(";");
      dataFile.print(sequenceLoopMode ? "SI" : "NO");
    } else {
      dataFile.print("0;0;0;0;NO");
    }
    
    dataFile.println(); // Nueva línea al final
    dataFile.close();
    
    Serial.print("Datos guardados en: ");
    Serial.println(filename);
  } else {
    Serial.println("Error al abrir archivo de datos");
  }
}

void checkDataLogging() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < 4; i++) {
    if (dataLogging[i]) {
      if (currentTime - lastLogTime[i] >= logInterval) {
        saveDataToSD(i);
        lastLogTime[i] = currentTime;
      }
    }
  }
}

void displayTurbMuestraDetail() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MUESTRA ");
  lcd.print(selectedMuestra + 1);
  lcd.print(":");
  
  // Obtener valores guardados
  float storedV = 0, storedC = 0;
  if (selectedMuestra == 0) {
    storedV = turbMuestra1V;
    storedC = turbMuestra1C;
  } else if (selectedMuestra == 1) {
    storedV = turbMuestra2V;
    storedC = turbMuestra2C;
  } else if (selectedMuestra == 2) {
    storedV = turbMuestra3V;
    storedC = turbMuestra3C;
  }
  
  lcd.setCursor(0, 1);
  lcd.print(menuCursor == 0 ? "> " : "  ");
  lcd.print("V: ");
  if (storedV > 0) {
    lcd.print(storedV, 3);
    lcd.print("V");
  } else {
    lcd.print("---");
  }
  
  lcd.setCursor(0, 2);
  lcd.print(menuCursor == 1 ? "> " : "  ");
  lcd.print("C: ");
  if (storedC > 0) {
    lcd.print((int)storedC);
    lcd.print(" MC/mL");
  } else {
    lcd.print("---");
  }
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 2 ? "> " : "  ");
  lcd.print("Atras");
}

void displayTurbSetVoltage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("VOLTAJE M");
  lcd.print(selectedMuestra + 1);
  lcd.print(":");
  
  // Valor almacenado
  float storedV = 0;
  if (selectedMuestra == 0) storedV = turbMuestra1V;
  else if (selectedMuestra == 1) storedV = turbMuestra2V;
  else if (selectedMuestra == 2) storedV = turbMuestra3V;
  
  lcd.setCursor(0, 1);
  lcd.print("Guardado: ");
  if (storedV > 0) {
    lcd.print(storedV, 3);
    lcd.print("V");
  } else {
    lcd.print("---");
  }
  
  // Valor actual
  lcd.setCursor(0, 2);
  lcd.print("Actual: ");
  lcd.print(tempVoltageReading, 3);
  lcd.print("V");
  
  lcd.setCursor(0, 3);
  lcd.print("Click para guardar");
}

void displayTurbConfirmVoltage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GUARDAR VOLTAJE?");
  
  lcd.setCursor(0, 1);
  lcd.print("Muestra ");
  lcd.print(selectedMuestra + 1);
  
  lcd.setCursor(0, 2);
  lcd.print("V: ");
  lcd.print(tempVoltageReading, 3);
  lcd.print("V");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void displayTurbSetConcentration() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONCENTR. M");
  lcd.print(selectedMuestra + 1);
  lcd.print(":");
  
  // Valor almacenado
  float storedC = 0;
  if (selectedMuestra == 0) storedC = turbMuestra1C;
  else if (selectedMuestra == 1) storedC = turbMuestra2C;
  else if (selectedMuestra == 2) storedC = turbMuestra3C;
  
  lcd.setCursor(0, 1);
  lcd.print("Guardado: ");
  if (storedC > 0) {
    lcd.print((int)storedC);
    lcd.print(" MC/mL");
  } else {
    lcd.print("---");
  }
  
  // Valor a configurar
  lcd.setCursor(0, 2);
  lcd.print("Nuevo: ");
  lcd.print(tempConcentrationValue);
  lcd.print(" MC/mL");
  
  // Barra visual
  lcd.setCursor(0, 3);
  lcd.print("[");
  int barLength = map(tempConcentrationValue, 0, 100, 0, 16);
  for (int i = 0; i < 16; i++) {
    if (i < barLength) lcd.print("=");
    else lcd.print(" ");
  }
  lcd.print("]");
}

void displayTurbConfirmConcentration() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GUARDAR CONC.?");
  
  lcd.setCursor(0, 1);
  lcd.print("Muestra ");
  lcd.print(selectedMuestra + 1);
  
  lcd.setCursor(0, 2);
  lcd.print("C: ");
  lcd.print(tempConcentrationValue);
  lcd.print(" MC/mL");
  
  lcd.setCursor(0, 3);
  lcd.print(menuCursor == 0 ? "> SI    " : "  SI    ");
  lcd.print(menuCursor == 1 ? "> NO" : "  NO");
}

void silenceAlarmTemporary() {
  alarmSilenced = true;
  alarmWasSilenced = true;  // Marcar que fue silenciada manualmente
  
  // Apagar buzzer y LED temporalmente
  ledcWrite(BUZZER_PIN, 0);
  //pcfOutput.digitalWrite(P4, HIGH);
  
  Serial.println("Alarma silenciada manualmente");
}

void checkAlarms() {
  bool shouldActivateAlarm = false;
  
  // Verificar si los sensores están en condiciones normales actualmente
  bool tempIsNormal = (temperature >= tempLimitMin && temperature <= tempLimitMax);
  bool phIsNormal = (co2Active || co2InjectionActive || phValue >= phLimitMin);
  
  // Verificar alarma de temperatura
  tempAlarm = false;
  if (temperature >= 10.0 && temperature <= 40.0) {
    if (temperature < tempLimitMin || temperature > tempLimitMax) {
      // Solo activar si:
      // 1. No fue silenciada, O
      // 2. Fue silenciada PERO el sensor volvió a normal y luego salió de límites nuevamente
      if (!alarmWasSilenced || (alarmWasSilenced && tempWasNormal)) {
        tempAlarm = true;
        shouldActivateAlarm = true;
        
        // Solo registrar en SD si no se ha registrado previamente
        if (!tempAlarmLogged) {
          if (temperature < tempLimitMin) {
            logAlarmToSD("Temperatura Baja", temperature);
          } else {
            logAlarmToSD("Temperatura Alta", temperature);
          }
          tempAlarmLogged = true;  // Marcar como registrada
          Serial.println("ALARMA: Temperatura fuera de límites - Registrada en SD");
        } else {
          Serial.println("ALARMA: Temperatura fuera de límites - Ya registrada");
        }
      }
    }
  }
  
  // Si la temperatura vuelve a normal, resetear el flag de logging
  if (tempIsNormal && tempAlarmLogged) {
    tempAlarmLogged = false;
    Serial.println("Temperatura normalizada - Flag de registro reseteado");
  }
  
  // Verificar alarma de pH
  phAlarm = false;
  if (!co2Active && !co2InjectionActive && phValue < phLimitMin) {
    // Solo activar si:
    // 1. No fue silenciada, O
    // 2. Fue silenciada PERO el sensor volvió a normal y luego salió de límites nuevamente
    if (!alarmWasSilenced || (alarmWasSilenced && phWasNormal)) {
      phAlarm = true;
      shouldActivateAlarm = true;
      
      // Solo registrar en SD si no se ha registrado previamente
      if (!phAlarmLogged) {
        logAlarmToSD("pH Bajo", phValue);
        phAlarmLogged = true;  // Marcar como registrada
        Serial.println("ALARMA: pH bajo el límite - Registrada en SD");
      } else {
        Serial.println("ALARMA: pH bajo el límite - Ya registrada");
      }
    }
  }
  
  // Si el pH vuelve a normal, resetear el flag de logging
  if (phIsNormal && phAlarmLogged) {
    phAlarmLogged = false;
    Serial.println("pH normalizado - Flag de registro reseteado");
  }
  
  // Si todos los sensores vuelven a normal, resetear el flag de silenciado
  if (tempIsNormal && phIsNormal && alarmWasSilenced) {
    alarmWasSilenced = false;
    alarmSilenced = false;
    Serial.println("Sensores en condiciones normales - Alarmas rearmadas");
  }
  
  // Actualizar estados previos
  tempWasNormal = tempIsNormal;
  phWasNormal = phIsNormal;
  
  // Activar o desactivar alarmas según corresponda
  if (shouldActivateAlarm && !alarmActive) {
    if (!alarmSilenced) {
      activateAlarm();
    }
  } else if (!shouldActivateAlarm && alarmActive && !emergencyAlarm) {
    deactivateAlarm();
  }
}

void activateAlarm() {
  alarmActive = true;
  
  // Activar buzzer al 100%
  ledcWrite(BUZZER_PIN, 255); // 100% PWM
  
  // Activar LED rojo de alarma
  //pcfOutput.digitalWrite(P4, LOW); // LOW = encendido (verificar lógica)
  
  Serial.println("*** ALARMA ACTIVADA ***");
}

void deactivateAlarm() {
  alarmActive = false;
  
  // Desactivar buzzer
  ledcWrite(BUZZER_PIN, 0);
  
  // Desactivar LED de alarma
  //pcfOutput.digitalWrite(P4, HIGH); // HIGH = apagado (verificar lógica)
  
  Serial.println("Alarma desactivada");
}

void logAlarmToSD(const char* alarmType, float value) {
  File alarmFile = SD.open(ALARM_LOG_FILE, FILE_APPEND);
  if (alarmFile) {
    // Obtener fecha y hora
    DateTime now = rtc.now();
    
    // Escribir fecha
    alarmFile.print(now.year());
    alarmFile.print("/");
    if (now.month() < 10) alarmFile.print("0");
    alarmFile.print(now.month());
    alarmFile.print("/");
    if (now.day() < 10) alarmFile.print("0");
    alarmFile.print(now.day());
    alarmFile.print(";");
    
    // Escribir hora
    if (now.hour() < 10) alarmFile.print("0");
    alarmFile.print(now.hour());
    alarmFile.print(":");
    if (now.minute() < 10) alarmFile.print("0");
    alarmFile.print(now.minute());
    alarmFile.print(":");
    if (now.second() < 10) alarmFile.print("0");
    alarmFile.print(now.second());
    alarmFile.print(",");
    alarmFile.print(alarmType);
    alarmFile.print(",");
    alarmFile.println(value);
    alarmFile.close();
    
    Serial.print("Alarma registrada: ");
    Serial.println(alarmType);
  }
}

void handleEmergencyState() {
  static bool lastEmergencyState = false;
  bool currentEmergencyState = digitalRead(EMERGENCY_PIN) == HIGH;  // HIGH = presionado
  
  // Detectar cuando se presiona el botón (transición de LOW a HIGH)
  if (currentEmergencyState && !lastEmergencyState) {
    emergencyActive = true;
    emergencyAlarm = true;

    // Registrar emergencia en SD (solo una vez por evento)
    if (!emergencyAlarmLogged) {
      logAlarmToSD("EMERGENCIA", 1.0);  // Valor 0 porque no es un sensor
      emergencyAlarmLogged = true;
      Serial.println("EMERGENCIA: Botón presionado - Registrada en SD");
    }

    // Activar alarma de emergencia inmediatamente
    activateAlarm();

    // Apagar todas las salidas
    for (int i = 0; i < 4; i++) {
      //pcfOutput.digitalWrite(i, HIGH);  // P0 a P3
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
    emergencyAlarm = false;
    
    // Resetear el flag para permitir registro de futuras emergencias
    emergencyAlarmLogged = false;
    Serial.println("Emergencia desactivada - Flag de registro reseteado");
    
    // Desactivar alarma de emergencia
    deactivateAlarm();

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