#ifndef ZMPT101B_ADS1115_h
#define ZMPT101B_ADS1115_h

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

#define DEFAULT_FREQUENCY 50.0f
#define DEFAULT_SENSITIVITY 500.0f

// Para ADS1115 con ganancia de 2/3x (±6.144V)
#define ADC_SCALE 32767.0f  // ADS1115 es de 16 bits con signo
#define VREF 6.144f         // Rango de ±6.144V con GAIN_TWOTHIRDS

class ZMPT101B_ADS1115
{
public:
    ZMPT101B_ADS1115(Adafruit_ADS1115* adsInstance, uint8_t channel, uint16_t frequency = DEFAULT_FREQUENCY);
    void setSensitivity(float value);
    float getRmsVoltage(uint8_t loopCount = 1);
    void setGain(adsGain_t gain);

private:
    Adafruit_ADS1115* ads;
    uint8_t channel;
    uint32_t period;
    float sensitivity = DEFAULT_SENSITIVITY;
    adsGain_t currentGain = GAIN_TWOTHIRDS;
    int16_t getZeroPoint();
    float getVoltageReference();
};

// Implementación inline en el header para simplicidad

inline ZMPT101B_ADS1115::ZMPT101B_ADS1115(Adafruit_ADS1115* adsInstance, uint8_t channel, uint16_t frequency)
{
    this->ads = adsInstance;
    this->channel = channel;
    period = 1000000 / frequency;
}

inline void ZMPT101B_ADS1115::setSensitivity(float value)
{
    sensitivity = value;
}

inline void ZMPT101B_ADS1115::setGain(adsGain_t gain)
{
    currentGain = gain;
    ads->setGain(gain);
}

inline float ZMPT101B_ADS1115::getVoltageReference()
{
    switch(currentGain) {
        case GAIN_TWOTHIRDS: return 6.144f;
        case GAIN_ONE: return 4.096f;
        case GAIN_TWO: return 2.048f;
        case GAIN_FOUR: return 1.024f;
        case GAIN_EIGHT: return 0.512f;
        case GAIN_SIXTEEN: return 0.256f;
        default: return 6.144f;
    }
}

inline int16_t ZMPT101B_ADS1115::getZeroPoint()
{
    int32_t Vsum = 0;
    uint32_t measurements_count = 0;
    uint32_t t_start = micros();

    while (micros() - t_start < period)
    {
        Vsum += ads->readADC_SingleEnded(channel);
        measurements_count++;
    }

    return Vsum / measurements_count;
}

inline float ZMPT101B_ADS1115::getRmsVoltage(uint8_t loopCount)
{
    double readingVoltage = 0.0f;
    float vref = getVoltageReference();

    for (uint8_t i = 0; i < loopCount; i++)
    {
        int16_t zeroPoint = this->getZeroPoint();

        int32_t Vnow = 0;
        uint64_t Vsum = 0;
        uint32_t measurements_count = 0;
        uint32_t t_start = micros();

        while (micros() - t_start < period)
        {
            Vnow = ads->readADC_SingleEnded(channel) - zeroPoint;
            Vsum += (Vnow * Vnow);
            measurements_count++;
        }

        readingVoltage += sqrt((double)Vsum / measurements_count) / ADC_SCALE * vref * sensitivity;
    }

    return readingVoltage / loopCount;
}

#endif