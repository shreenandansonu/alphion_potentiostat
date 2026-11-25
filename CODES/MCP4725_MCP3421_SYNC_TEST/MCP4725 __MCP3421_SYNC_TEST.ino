
#include <Wire.h>

// =====================================================
// MCP4725 DAC + MCP3421 ADC SYNCHRONISED TEST
// =====================================================

// DAC I2C bus
#define DAC_SDA       21
#define DAC_SCL       22
#define DAC_ADDR      0x60

// ADC I2C bus
#define ADC_SDA       32
#define ADC_SCL       33
#define ADC_ADDR      0x68

// I2C instances
TwoWire DAC_Wire = TwoWire(0);
TwoWire ADC_Wire = TwoWire(1);

// =====================================================
// TEST SETTINGS
// =====================================================

const float DAC_VDD = 3.30;       // Nominal DAC supply
const float MAX_DAC_VOLTAGE = 1.90;

const int DAC_MAX_CODE =
    (int)((MAX_DAC_VOLTAGE / DAC_VDD) * 4095.0);

const int DAC_STEP = 16;

const unsigned long ADC_TIMEOUT_MS = 100;

// MCP3421 single-shot, 12-bit, PGA x1
const uint8_t ADC_CONFIG = 0x00;

// =====================================================
// DAC FUNCTION
// =====================================================

bool writeDAC(uint16_t value)
{
    value = constrain(value, 0, 4095);

    DAC_Wire.beginTransmission(DAC_ADDR);

    // MCP4725: write DAC register
    DAC_Wire.write(0x40);

    DAC_Wire.write(value >> 4);
    DAC_Wire.write((value & 0x0F) << 4);

    return DAC_Wire.endTransmission() == 0;
}

// =====================================================
// ADC CONVERSION FUNCTION
// =====================================================

bool readADC(float &voltage_mV)
{
    // Start a new single-shot conversion
    ADC_Wire.beginTransmission(ADC_ADDR);
    ADC_Wire.write(ADC_CONFIG);

    if (ADC_Wire.endTransmission() != 0)
    {
        return false;
    }

    unsigned long startTime = millis();

    while (millis() - startTime < ADC_TIMEOUT_MS)
    {
        // Read two data bytes and one configuration byte
        uint8_t received =
            ADC_Wire.requestFrom((uint8_t)ADC_ADDR, (uint8_t)3);

        if (received == 3)
        {
            uint8_t msb = ADC_Wire.read();
            uint8_t lsb = ADC_Wire.read();
            uint8_t config = ADC_Wire.read();

            // RDY = 0 means conversion is complete
            if ((config & 0x80) == 0)
            {
                int16_t raw =
                    ((uint16_t)msb << 8) | lsb;

                // Extract signed 12-bit result
                raw &= 0x0FFF;

                if (raw & 0x0800)
                {
                    raw |= 0xF000;
                }

                // MCP3421 12-bit, PGA x1:
                // 1 mV per ADC count
                voltage_mV = (float)raw;

                return true;
            }
        }

        delay(1);
    }

    return false;
}

// =====================================================
// SINGLE SYNCHRONISED SAMPLE
// =====================================================

void takeSample(uint16_t dacCode)
{
    float adc_mV = 0;

    // Write DAC output
    if (!writeDAC(dacCode))
    {
        Serial.println("ERROR,DAC_WRITE");
        return;
    }

    // Allow DAC output to settle
    delay(3);

    // Trigger and read ADC conversion
    if (!readADC(adc_mV))
    {
        Serial.println("ERROR,ADC_READ");
        return;
    }

    float expectedVoltage =
        (dacCode / 4095.0) * DAC_VDD;

    float measuredVoltage = adc_mV / 1000.0;

    float error_mV =
        (measuredVoltage - expectedVoltage) * 1000.0;

    Serial.print(millis());
    Serial.print(",");

    Serial.print(dacCode);
    Serial.print(",");

    Serial.print(expectedVoltage, 5);
    Serial.print(",");

    Serial.print(measuredVoltage, 5);
    Serial.print(",");

    Serial.println(error_mV, 2);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(230400);

    delay(1000);

    DAC_Wire.begin(DAC_SDA, DAC_SCL);
    ADC_Wire.begin(ADC_SDA, ADC_SCL);

    DAC_Wire.setClock(100000);
    ADC_Wire.setClock(100000);

    Serial.println("TIME_MS,DAC_CODE,EXPECTED_V,ADC_V,ERROR_mV");

    // Start at zero volts
    writeDAC(0);

    delay(100);
}

// =====================================================
// MAIN SWEEP
// =====================================================

void loop()
{
    // Sweep upwards: 0 V to 1.90 V
    for (int code = 0; code <= DAC_MAX_CODE; code += DAC_STEP)
    {
        takeSample(code);
    }

    // Ensure the maximum point is included
    takeSample(DAC_MAX_CODE);

    // Sweep downwards: 1.90 V to 0 V
    for (int code = DAC_MAX_CODE - DAC_STEP;
         code >= 0;
         code -= DAC_STEP)
    {
        takeSample(code);
    }

    // Ensure zero is included
    takeSample(0);

    // Pause before repeating
    delay(1000);
}
