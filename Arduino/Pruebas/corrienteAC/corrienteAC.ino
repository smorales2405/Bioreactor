#include <MeanFilterLib.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;  /* Use this for the 16-bit version */

int16_t adc3 = 0;
float volts3 = 0.0;
float volts3_f = 0.0;

float corriente = 0.0;
float corriente_f = 0.0;
float Sensibilidad=0.185; //sensibilidad en Voltios/Amperio para sensor de 5A

MeanFilter<float> meanFilter1(50);
MeanFilter<float> meanFilter2(50);

void setup() {
  
  Serial.begin(115200);
  ads.setGain(GAIN_TWOTHIRDS);

  if (!ads.begin()) {
  Serial.println("Failed to initialize ADS.");
  while (1);
  }
}

void loop() {

  adc3 = ads.readADC_SingleEnded(3);
  volts3 = ads.computeVolts(adc3);
 
  corriente=(volts3-2.5)/Sensibilidad; //Ecuación  para obtener la corriente
  volts3_f = meanFilter1.AddValue(volts3);
  corriente_f = meanFilter2.AddValue(corriente);
  
  Serial.print("Voltaje: ");
  Serial.print(volts3_f,3);
  Serial.println(" V"); 
  Serial.print("Corriente: ");
  Serial.print(corriente_f,3);
  Serial.println(" A"); 
  delay(200);     
}