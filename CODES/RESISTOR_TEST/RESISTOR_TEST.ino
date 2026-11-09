
#include <Wire.h>
#include <math.h>

// =====================================================
// 10K RESISTOR POTENTIOSTAT TEST
// ESP32 + MCP4725 + MCP3421 + ADG704
// =====================================================

// ---------------- DAC ----------------
#define DAC_SDA       21
#define DAC_SCL       22
#define DAC_ADDR      0x60

// ---------------- ADC ----------------
#define ADC_SDA       32
#define ADC_SCL       33
#define ADC_ADDR      0x68

// ---------------- MUX ----------------
#define MUX_0         16
#define MUX_1         17
#define MUX_EN        18

TwoWire DAC_Wire = TwoWire(0);
TwoWire ADC_Wire = TwoWire(1);

// =====================================================
// USER SETTINGS
// =====================================================

// Update these to measured board values if available.
const float DAC_SUPPLY = 3.30;
const float V_OFFSET   = 1.65;

// Feedback resistor selected by MUX S1
const float R_FEEDBACK = 10000.0;

// Test sweep
const float DAC_MIN_V = 1.45;
const float DAC_MAX_V = 1.85;

// DAC increment in 12-bit counts
const int DAC_STEP = 4;

// Delay after changing DAC
const int SETTLE_DELAY_MS = 5;

// Ignore resistance calculations near zero current
const float MIN_CURRENT_uA = 2.0;
const float MIN_DRIVE_V = 0.02;

// MCP3421 single-shot, 12-bit, PGA x1
// RDY=1 starts conversion; MD=0; S1:S0=00; PGA=00
const uint8_t ADC_START_CONFIG = 0x80;

// =====================================================
// DAC FUNCTIONS
// =====================================================

uint16_t voltageToDAC(float voltage)
{
    voltage = constrain(voltage, 0.0, DAC_SUPPLY);

    return (uint16_t)round(
        voltage * 4095.0 / DAC_SUPPLY
    );
}

bool writeDAC(uint16_t value)
{
    value = constrain(value, 0, 4095);

    DAC_Wire.beginTransmission(DAC_ADDR);
    DAC_Wire.write(0x40);
    DAC_Wire.write(value >> 4);
    DAC_Wire.write((value & 0x0F) << 4);

    return DAC_Wire.endTransmission() == 0;
}

// =====================================================
// MUX CONTROL
// ADG704: A1=0, A0=0 selects S1
// EN is active HIGH
// =====================================================

void select10KFeedback()
{
    digitalWrite(MUX_EN, LOW);
    delay(2);

    digitalWrite(MUX_0, LOW);
    digitalWrite(MUX_1, LOW);

    delay(2);

    digitalWrite(MUX_EN, HIGH);
    delay(10);
}

// =====================================================
// MCP3421 ADC
// 12-bit, single-shot, PGA x1
// LSB = 1mV
// =====================================================

bool readADC(float &voltage)
{
    // Start conversion
    ADC_Wire.beginTransmission(ADC_ADDR);
    ADC_Wire.write(ADC_START_CONFIG);

    if (ADC_Wire.endTransmission() != 0)
        return false;

    unsigned long startTime = millis();

    while (millis() - startTime < 100)
    {
        uint8_t count = ADC_Wire.requestFrom(
            (uint8_t)ADC_ADDR,
            (uint8_t)3
        );

        if (count == 3)
        {
            uint8_t msb = ADC_Wire.read();
            uint8_t lsb = ADC_Wire.read();
            uint8_t config = ADC_Wire.read();

            // RDY = 0 means conversion completed
            if ((config & 0x80) == 0)
            {
                int16_t raw =
                    ((uint16_t)msb << 8) | lsb;

                // Extract signed 12-bit result
                raw &= 0x0FFF;

                if (raw & 0x0800)
                    raw |= 0xF000;

                voltage = raw * 0.001f;

                return true;
            }
        }

        delay(1);
    }

    return false;
}

// =====================================================
// MEASUREMENT
// =====================================================

void takeMeasurement(uint16_t dacCode)
{
    float tiaVoltage = NAN;

    float dacVoltage =
        (dacCode / 4095.0f) * DAC_SUPPLY;

    // Update DAC
    if (!writeDAC(dacCode))
    {
        Serial.println("ERROR,DAC_WRITE");
        return;
    }

    // Allow analog circuit to settle
    delay(SETTLE_DELAY_MS);

    // Read TIA output
    if (!readADC(tiaVoltage))
    {
        Serial.println("ERROR,ADC_READ");
        return;
    }

    // TIA current:
    // I = (V_OFFSET - TIA_OUT) / R_FEEDBACK
    float current_uA =
        ((V_OFFSET - tiaVoltage) / R_FEEDBACK)
        * 1000000.0f;

    // Assumed voltage across the test resistor
    float driveVoltage = dacVoltage - V_OFFSET;

    // Calculate resistance only away from zero crossing
    float estimatedResistance = NAN;

    if (fabs(driveVoltage) >= MIN_DRIVE_V &&
        fabs(current_uA) >= MIN_CURRENT_uA)
    {
        estimatedResistance =
            fabs(driveVoltage / (current_uA / 1000000.0f));
    }

    // CSV:
    // TIME_MS,DAC_CODE,DAC_V,TIA_V,CURRENT_uA,EST_R_OHM

    Serial.print(millis());
    Serial.print(",");

    Serial.print(dacCode);
    Serial.print(",");

    Serial.print(dacVoltage, 5);
    Serial.print(",");

    Serial.print(tiaVoltage, 5);
    Serial.print(",");

    Serial.print(current_uA, 3);
    Serial.print(",");

    if (isnan(estimatedResistance))
        Serial.println("nan");
    else
        Serial.println(estimatedResistance, 1);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(230400);
    delay(1000);

    pinMode(MUX_0, OUTPUT);
    pinMode(MUX_1, OUTPUT);
    pinMode(MUX_EN, OUTPUT);

    digitalWrite(MUX_EN, LOW);

    DAC_Wire.begin(DAC_SDA, DAC_SCL);
    ADC_Wire.begin(ADC_SDA, ADC_SCL);

    DAC_Wire.setClock(100000);
    ADC_Wire.setClock(100000);

    select10KFeedback();

    // Start at offset
    writeDAC(voltageToDAC(V_OFFSET));

    delay(100);

    Serial.println(
        "TIME_MS,DAC_CODE,DAC_V,TIA_V,CURRENT_uA,EST_R_OHM"
    );
}

// =====================================================
// TRIANGULAR SWEEP
// =====================================================

void loop()
{
    uint16_t minCode = voltageToDAC(DAC_MIN_V);
    uint16_t maxCode = voltageToDAC(DAC_MAX_V);

    // Rising sweep
    for (uint16_t code = minCode;
         code <= maxCode;
         code += DAC_STEP)
    {
        takeMeasurement(code);
    }

    takeMeasurement(maxCode);

    // Falling sweep
    int32_t code = (int32_t)maxCode - DAC_STEP;

    while (code >= minCode)
    {
        takeMeasurement((uint16_t)code);
        code -= DAC_STEP;
    }

    takeMeasurement(minCode);
}
