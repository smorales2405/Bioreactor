/**
 * Programa de calibración para ZMPT101B con ESP32 y ADS1115
 * 
 * Este programa encuentra el valor de sensibilidad correcto para el sensor
 * barriendo desde el valor más bajo hasta el más alto.
 * 
 * Conexiones:
 * - ZMPT101B OUT -> ADS1115 A2
 * - ADS1115 SDA -> ESP32 GPIO21
 * - ADS1115 SCL -> ESP32 GPIO22
 */

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "ZMPT101B_ADS1115.h"  // Asegúrate de tener este archivo en tu proyecto

#define ACTUAL_VOLTAGE 233.0f  // Cambia esto basado en el voltaje real medido con multímetro

#define START_VALUE 0.0f     // Valor inicial de sensibilidad
#define STOP_VALUE 1000.0f     // Valor máximo de sensibilidad
#define STEP_VALUE 0.25f        // Incremento en cada paso
#define TOLERANCE 1.0f         // Tolerancia en voltios

#define MAX_TOLERANCE_VOLTAGE (ACTUAL_VOLTAGE + TOLERANCE)
#define MIN_TOLERANCE_VOLTAGE (ACTUAL_VOLTAGE - TOLERANCE)

// Configuración del ADS1115
Adafruit_ADS1115 ads;

// ZMPT101B conectado al canal A2 del ADS1115
// Frecuencia de 50 Hz (cambiar a 60 si estás en países con 60Hz)
ZMPT101B_ADS1115 voltageSensor(&ads, 2, 50);  // Canal 2 = A2

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=================================================");
  Serial.println("Calibración del Sensor de Voltaje ZMPT101B");
  Serial.println("con ESP32 y ADS1115");
  Serial.println("=================================================");
  
  // Inicializar I2C con pines específicos del ESP32
  Wire.begin(21, 22);  // SDA=21, SCL=22 (pines estándar del ESP32)
  
  // Inicializar ADS1115
  if (!ads.begin()) {
    Serial.println("Error: No se pudo inicializar el ADS1115!");
    Serial.println("Verifica las conexiones I2C");
    while (1);
  }
  
  // Configurar ganancia para rango de ±6.144V (máximo rango)
  ads.setGain(GAIN_TWOTHIRDS);
  voltageSensor.setGain(GAIN_TWOTHIRDS);
  
  Serial.println("ADS1115 inicializado correctamente");
  Serial.println();
  Serial.print("Voltaje objetivo: ");
  Serial.print(ACTUAL_VOLTAGE);
  Serial.println(" V");
  Serial.print("Rango de tolerancia: ");
  Serial.print(MIN_TOLERANCE_VOLTAGE);
  Serial.print(" - ");
  Serial.print(MAX_TOLERANCE_VOLTAGE);
  Serial.println(" V");
  Serial.println();
  
  // Esperar un momento para estabilizar
  delay(2000);
  
  Serial.println("Iniciando calibración...");
  Serial.println("Asegúrate de que el sensor está conectado a la red eléctrica");
  Serial.println();
  delay(3000);
  
  float sensitivityValue = START_VALUE;
  voltageSensor.setSensitivity(sensitivityValue);
  
  Serial.println("Sensibilidad => Voltaje medido");
  Serial.println("--------------------------------");
  
  float voltageNow = voltageSensor.getRmsVoltage(3);  // Promedio de 3 lecturas
  Serial.print(sensitivityValue);
  Serial.print(" => ");
  Serial.print(voltageNow);
  Serial.println(" V");
  
  // Búsqueda del valor de sensibilidad correcto
  while (voltageNow > MAX_TOLERANCE_VOLTAGE || voltageNow < MIN_TOLERANCE_VOLTAGE) {
    if (sensitivityValue < STOP_VALUE) {
      sensitivityValue += STEP_VALUE;
      voltageSensor.setSensitivity(sensitivityValue);
      voltageNow = voltageSensor.getRmsVoltage(3);  // Promedio de 3 lecturas
      
      Serial.print(sensitivityValue);
      Serial.print(" => ");
      Serial.print(voltageNow);
      Serial.println(" V");
      
      // Pequeña pausa para no saturar el Serial
      delay(100);
    } else {
      Serial.println();
      Serial.println("=================================================");
      Serial.println("ERROR: No se pudo determinar el valor de sensibilidad");
      Serial.println("Posibles causas:");
      Serial.println("1. El sensor no está conectado correctamente");
      Serial.println("2. No hay voltaje AC presente");
      Serial.println("3. El rango de búsqueda es inadecuado");
      Serial.println("=================================================");
      return;
    }
  }
  
  Serial.println();
  Serial.println("=================================================");
  Serial.println("¡CALIBRACIÓN EXITOSA!");
  Serial.println("=================================================");
  Serial.print("Voltaje más cercano dentro de tolerancia: ");
  Serial.print(voltageNow);
  Serial.println(" V");
  Serial.print("VALOR DE SENSIBILIDAD ENCONTRADO: ");
  Serial.println(sensitivityValue, 2);
  Serial.println();
  Serial.println("Guarda este valor de sensibilidad para usarlo");
  Serial.println("en tu código de medición de voltaje");
  Serial.println("=================================================");
}

void loop() {
  // Mostrar el voltaje continuamente después de la calibración
  float voltage = voltageSensor.getRmsVoltage();
  Serial.print("Voltaje actual: ");
  Serial.print(voltage);
  Serial.println(" V");
  delay(1000);
}