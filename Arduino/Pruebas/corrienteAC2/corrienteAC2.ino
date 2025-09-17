#include <Filters.h>            //This library  does a huge work check its .cpp file
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads; 

float testFrequency = 60;                     // test signal frequency  (Hz)
float windowLength = 40.0/testFrequency;     // how long to average the  signal, for statistist

float intercept = 0; // to be adjusted based on calibration  testing
float slope = 0.0752; // to be adjusted based on calibration testing
                      //Please check the ACS712 Tutorial video by SurtrTech to see  how to get them because it depends on your sensor, or look below

float Amps_TRMS;  
float ACS_Value;

unsigned long printPeriod = 1000; 
unsigned long  previousMillis = 0;

void setup() {
  Serial.begin(115200);
  ads.setGain(GAIN_TWOTHIRDS);

  if (!ads.begin()) {
  Serial.println("Failed to initialize ADS.");
  while (1);
  }

}

void loop() {
  RunningStatistics inputStats;                 // create statistics to look at the raw test signal
  inputStats.setWindowSecs(  windowLength );
   
  while( true ) {   
    ACS_Value = ads.readADC_SingleEnded(3);  // read the analog in value:
    inputStats.input(ACS_Value);  // log to Stats  function
        
    if((unsigned long)(millis() - previousMillis) >= printPeriod)  { //Do the calculations every 1s
      previousMillis = millis();   // update  time
      
      Amps_TRMS = intercept + slope * inputStats.sigma();

      Serial.print("I: ");
      Serial.print(inputStats.sigma());
      Serial.println("  A");

      Serial.print("Ic: ");
      Serial.print(Amps_TRMS); 
      Serial.println("  A");
      Serial.println(" ")
    }
  }
}

/* About the slope  and intercept
 * First you need to know that all the TRMS calucations are done  by functions from the library, it's the "inputStats.sigma()" value
 * At first  you can display that "inputStats.sigma()" as your TRMS value, then try to measure  using it when the input is 0.00A
 * If the measured value is 0 like I got you  can keep the intercept as 0, otherwise you'll need to add or substract to make that  value equal to 0
 * In other words " remove the offset"
 * Then turn on  the power to a known value, for example use a bulb or a led that ou know its power  and you already know your voltage, so a little math you'll get the theoritical amps
  * you divide that theory value by the measured value and here you got the slope,  now place them or modify them
 */
