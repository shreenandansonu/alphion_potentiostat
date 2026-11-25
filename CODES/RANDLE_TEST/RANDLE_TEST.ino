#include <Wire.h>
#include <math.h>

// =====================================================
// RANDLES CIRCUIT CV TEST
// ESP32 + MCP4725 + MCP3421 + ADG704
//
// Dummy cell: Rs = 1k, Rct = 100k || Cdl = 100nF
// Wiring: WE -> free end of Rct||Cdl
//         RE + CE -> free end of Rs
//
// Sweep: -0.5 V -> +0.5 V -> -0.5 V at 100 mV/s
// Expected: straight line, ~9.9 uA/V (about +/-5 uA at +/-0.5 V)
// (Staircase sampling several ms after each step, so Cdl will
//  NOT be visible. This is a resistor + wiring check.)
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

const float DAC_SUPPLY = 3.30f;
const float V_OFFSET   = 1.65f;     // nominal virtual ground

// TIA feedback resistor selected by ADG704 S1
const float R_FEEDBACK = 10000.0f;

// CV potential window (relative to V_OFFSET)
const float CV_MIN_V = -0.30f;
const float CV_MAX_V =  0.30f;

// DAC step size (codes)
const uint16_t DAC_STEP = 4;

// Scan rate
const float TARGET_SCAN_RATE_V_S = 0.1f;   // 100 mV/s

// Short settle time after each DAC update before ADC starts
const uint32_t SETTLE_US = 1000;

// MCP3421 12-bit (240 SPS), PGA x1, one-shot. LSB = 1 mV
const uint8_t ADC_START_CONFIG = 0x80;

// ADC input range is +/-2.048 V. Flag when close to the limit
const float ADC_SAT_V = 2.040f;

// Minimum current for resistance calculation
const float MIN_CURRENT_uA = 2.0f;

// Number of ADC readings averaged for offset calibration
const uint8_t CAL_SAMPLES = 16;

// =====================================================
// GLOBALS
// =====================================================

float computedStepPeriodUs = 0.0f;
float tiaOffset = V_OFFSET;       // measured TIA voltage at 0 V applied
uint32_t cycleNumber = 0;

// =====================================================
// DAC FUNCTIONS
// =====================================================

uint16_t voltageToDAC(float voltage)
{
    voltage = constrain(voltage, 0.0f, DAC_SUPPLY);
    return (uint16_t)round(voltage * 4095.0f / DAC_SUPPLY);
}

float dacToVoltage(uint16_t code)
{
    return (code / 4095.0f) * DAC_SUPPLY;
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
// MUX CONTROL (ADG704, address 00 selects S1)
// =====================================================

void selectFeedback()
{
    digitalWrite(MUX_EN, HIGH);
    delay(2);

    digitalWrite(MUX_0, LOW);
    digitalWrite(MUX_1, LOW);

    delay(2);

    digitalWrite(MUX_EN, HIGH);
    delay(10);
}

// =====================================================
// MCP3421 ADC
// =====================================================

bool readADC(float &voltage)
{
    ADC_Wire.beginTransmission(ADC_ADDR);
    ADC_Wire.write(ADC_START_CONFIG);

    if (ADC_Wire.endTransmission() != 0)
        return false;

    unsigned long startTime = millis();

    while (millis() - startTime < 100)
    {
        uint8_t count = ADC_Wire.requestFrom((uint8_t)ADC_ADDR, (uint8_t)3);

        if (count == 3)
        {
            uint8_t msb    = ADC_Wire.read();
            uint8_t lsb    = ADC_Wire.read();
            uint8_t config = ADC_Wire.read();

            if ((config & 0x80) == 0)
            {
                int16_t raw = ((uint16_t)msb << 8) | lsb;

                // Sign extend 12-bit data
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
// OFFSET CALIBRATION
// Applies 0 V (DAC at offset) and averages the TIA output.
// Cell can stay connected: zero applied volts -> zero current.
// =====================================================

void calibrateOffset()
{
    writeDAC(voltageToDAC(V_OFFSET));
    delay(300);

    float sum = 0.0f;
    uint8_t n = 0;

    for (uint8_t i = 0; i < CAL_SAMPLES; i++)
    {
        float v;
        if (readADC(v))
        {
            sum += v;
            n++;
        }
    }

    if (n > 0)
        tiaOffset = sum / n;

    Serial.print("# Measured TIA offset: ");
    Serial.print(tiaOffset, 5);
    Serial.print(" V (nominal ");
    Serial.print(V_OFFSET, 3);
    Serial.println(" V)");
}

// =====================================================
// MEASUREMENT
// direction: +1 forward, -1 reverse
// =====================================================

void takeMeasurement(uint16_t dacCode, uint32_t cycle, int direction)
{
    float tiaVoltage = NAN;

    float dacVoltage     = dacToVoltage(dacCode);
    float appliedVoltage = dacVoltage - V_OFFSET;

    uint32_t t0 = micros();

    if (!writeDAC(dacCode))
    {
        Serial.println("ERROR,DAC_WRITE");
        return;
    }

    delayMicroseconds(SETTLE_US);

    if (!readADC(tiaVoltage))
    {
        Serial.println("ERROR,ADC_READ");
        return;
    }

    float current_uA = ((tiaOffset - tiaVoltage) / R_FEEDBACK) * 1000000.0f;

    float estimatedResistance = NAN;
    if (fabs(current_uA) >= MIN_CURRENT_uA)
    {
        estimatedResistance =
            fabs(appliedVoltage / (current_uA / 1000000.0f));
    }

    int saturated = (tiaVoltage > ADC_SAT_V || tiaVoltage < -ADC_SAT_V) ? 1 : 0;

    // CSV: TIME_US,CYCLE,DIRECTION,DAC_CODE,DAC_V,TIA_V,CURRENT_uA,EST_R_OHM,SAT
    Serial.print(t0);                 Serial.print(",");
    Serial.print(cycle);              Serial.print(",");
    Serial.print(direction);          Serial.print(",");
    Serial.print(dacCode);            Serial.print(",");
    Serial.print(dacVoltage, 5);      Serial.print(",");
    Serial.print(tiaVoltage, 5);      Serial.print(",");
    Serial.print(current_uA, 3);      Serial.print(",");

    if (isnan(estimatedResistance))
        Serial.print("nan");
    else
        Serial.print(estimatedResistance, 1);

    Serial.print(",");
    Serial.println(saturated);

    // Hold until the full step period has elapsed (real scan-rate control)
    while ((uint32_t)(micros() - t0) < (uint32_t)computedStepPeriodUs)
    {
        // wait
    }
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

    DAC_Wire.setClock(400000);
    ADC_Wire.setClock(400000);

    selectFeedback();

    float voltsPerStep = (DAC_SUPPLY / 4095.0f) * DAC_STEP;
    computedStepPeriodUs = (voltsPerStep / TARGET_SCAN_RATE_V_S) * 1000000.0f;

    Serial.println(
        "TIME_US,CYCLE,DIRECTION,DAC_CODE,DAC_V,TIA_V,CURRENT_uA,EST_R_OHM,SAT"
    );

    Serial.print("# Target scan rate: ");
    Serial.print(TARGET_SCAN_RATE_V_S * 1000.0f);
    Serial.println(" mV/s");

    Serial.print("# Step period: ");
    Serial.print(computedStepPeriodUs / 1000.0f, 3);
    Serial.println(" ms");

    Serial.println("# MUX: S1 selected");

    calibrateOffset();
}

// =====================================================
// MAIN CV SWEEP
// =====================================================

void loop()
{
    cycleNumber++;

    uint16_t minCode = voltageToDAC(V_OFFSET + CV_MIN_V);
    uint16_t maxCode = voltageToDAC(V_OFFSET + CV_MAX_V);

    // ---------------- FORWARD: min -> max ----------------
    uint32_t code;
    for (code = minCode; code <= maxCode; code += DAC_STEP)
    {
        takeMeasurement((uint16_t)code, cycleNumber, +1);
    }

    // Exact upper endpoint only if the stepping missed it
    if ((code - DAC_STEP) != maxCode)
    {
        takeMeasurement(maxCode, cycleNumber, +1);
    }

    // ---------------- REVERSE: max -> min ----------------
    int32_t rcode = (int32_t)maxCode - DAC_STEP;

    while (rcode > (int32_t)minCode)
    {
        takeMeasurement((uint16_t)rcode, cycleNumber, -1);
        rcode -= DAC_STEP;
    }

    // Exact lower endpoint
    takeMeasurement(minCode, cycleNumber, -1);
}
