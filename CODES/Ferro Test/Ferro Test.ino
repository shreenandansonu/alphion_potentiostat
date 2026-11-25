
#include <Wire.h>
#include <math.h>

// =====================================================
// FERRI / FERROCYANIDE CYCLIC VOLTAMMETRY
//
// Hardware:
// ESP32 + MCP4725 + MCP3421 + ADG704
// OPA2320 control amplifier
// LMP7721 transimpedance amplifier
//
// Solution:
// 2.5 mM K3[Fe(CN)6]
// 2.5 mM K4[Fe(CN)6]
// 0.1 M KCl
//
// Electrodes:
// WE = Carbon
// RE = Ag/AgCl (assumed)
// CE = Platinum
//
// WE is maintained at 1.65 V virtual ground.
// DAC controls RE potential.
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
// EXPERIMENT SETTINGS
// =====================================================

// WE virtual ground relative to circuit GND
const float WE_MIDRAIL_V = 1.650f;

// DAC supply
const float DAC_SUPPLY = 3.300f;

// Electrochemical potential WE relative to RE
const float CV_MIN_V = -0.200f;
const float CV_MAX_V =  0.600f;

// Scan rate
const float SCAN_RATE_V_S = 0.050f;

// DAC increment
const uint16_t DAC_STEP = 2;

// Number of CV cycles
const uint8_t TOTAL_CYCLES = 3;

// TIA feedback resistor
const float R_FEEDBACK = 10000.0f;

// Settling time
const uint32_t SETTLE_US = 1000;

// MCP3421 12-bit, 240 SPS, PGA x1
const uint8_t ADC_CONFIG = 0x80;

// ADC input safety thresholds
const float ADC_HIGH_LIMIT = 2.000f;
const float ADC_LOW_LIMIT  = 0.050f;

// Calibration
const uint8_t CAL_SAMPLES = 32;

// =====================================================
// GLOBAL VARIABLES
// =====================================================

float tiaOffset = WE_MIDRAIL_V;
float stepPeriodUs = 0.0f;

uint16_t startCode = 0;
uint16_t endCode = 0;

uint32_t totalMeasurements = 0;

bool experimentStarted = false;
bool experimentFinished = false;

// =====================================================
// POTENTIAL CONVERSION
// =====================================================

// E_WE-RE = WE_MIDRAIL - DAC voltage
// DAC voltage = WE_MIDRAIL - E_WE-RE

float potentialToDAC(float potential)
{
    return WE_MIDRAIL_V - potential;
}

uint16_t voltageToDAC(float voltage)
{
    voltage = constrain(voltage, 0.0f, DAC_SUPPLY);

    return (uint16_t)round(
        voltage * 4095.0f / DAC_SUPPLY
    );
}

float dacToVoltage(uint16_t code)
{
    return (float)code * DAC_SUPPLY / 4095.0f;
}

// =====================================================
// MCP4725 DAC
// =====================================================

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
// ADG704 FEEDBACK MUX
// Address 00 selects S1
// =====================================================

void selectFeedback()
{
    digitalWrite(MUX_EN, HIGH);

    digitalWrite(MUX_0, LOW);
    digitalWrite(MUX_1, LOW);

    delay(5);
}

// =====================================================
// MCP3421 ADC
// =====================================================

bool readADC(float &voltage)
{
    ADC_Wire.beginTransmission(ADC_ADDR);
    ADC_Wire.write(ADC_CONFIG);

    if (ADC_Wire.endTransmission() != 0)
        return false;

    uint32_t startTime = millis();

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

            if ((config & 0x80) == 0)
            {
                int16_t raw =
                    ((uint16_t)msb << 8) | lsb;

                // Sign extension for 12-bit data
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
// TIA OFFSET CALIBRATION
//
// WE external electrode disconnected.
// TIA maintains its input at virtual midrail.
// =====================================================

void calibrateTIA()
{
    Serial.println("# TIA calibration started");
    Serial.println("# Disconnect WE from the electrochemical cell");

    // Set initial potential command
    float initialDAC = potentialToDAC(CV_MIN_V);

    if (!writeDAC(voltageToDAC(initialDAC)))
    {
        Serial.println("# ERROR: DAC calibration write failed");

        while (true)
            delay(1000);
    }

    delay(500);

    float sum = 0.0f;
    uint8_t validSamples = 0;

    for (uint8_t i = 0; i < CAL_SAMPLES; i++)
    {
        float voltage;

        if (readADC(voltage))
        {
            sum += voltage;
            validSamples++;
        }

        delay(10);
    }

    if (validSamples < 10)
    {
        Serial.println("# ERROR: ADC calibration failed");

        while (true)
            delay(1000);
    }

    tiaOffset = sum / validSamples;

    Serial.print("# TIA zero-current offset: ");
    Serial.print(tiaOffset, 5);
    Serial.println(" V");

    if (tiaOffset < ADC_LOW_LIMIT ||
        tiaOffset > ADC_HIGH_LIMIT)
    {
        Serial.println("# ERROR: TIA offset outside safe ADC range");

        while (true)
            delay(1000);
    }

    Serial.println("# TIA calibration complete");
}

// =====================================================
// TAKE MEASUREMENT
//
// direction +1 = increasing WE-RE potential
// direction -1 = decreasing WE-RE potential
//
// WE remains at 1.65 V virtual ground.
// =====================================================

void takeMeasurement(
    uint16_t dacCode,
    uint32_t cycle,
    int direction
)
{
    uint32_t t0 = micros();

    if (!writeDAC(dacCode))
    {
        Serial.println("# ERROR: DAC_WRITE");
        return;
    }

    delayMicroseconds(SETTLE_US);

    float tiaVoltage;

    if (!readADC(tiaVoltage))
    {
        Serial.println("# ERROR: ADC_READ");
        return;
    }

    float dacVoltage = dacToVoltage(dacCode);

    // Actual electrochemical potential setpoint
    float appliedPotential =
        WE_MIDRAIL_V - dacVoltage;

    // Current from TIA
    float current_uA =
        ((tiaOffset - tiaVoltage) /
         R_FEEDBACK) * 1000000.0f;

    // ADC saturation detection
    int saturated =
        (tiaVoltage >= ADC_HIGH_LIMIT ||
         tiaVoltage <= ADC_LOW_LIMIT) ? 1 : 0;

    totalMeasurements++;

    // CSV:
    // TIME_US,CYCLE,DIRECTION,E_WE_RE_V,
    // WE_MIDRAIL_V,DAC_SIG_V,TIA_V,CURRENT_uA,SAT

    Serial.print(t0);
    Serial.print(",");

    Serial.print(cycle);
    Serial.print(",");

    Serial.print(direction);
    Serial.print(",");

    Serial.print(appliedPotential, 5);
    Serial.print(",");

    Serial.print(WE_MIDRAIL_V, 5);
    Serial.print(",");

    Serial.print(dacVoltage, 5);
    Serial.print(",");

    Serial.print(tiaVoltage, 5);
    Serial.print(",");

    Serial.print(current_uA, 3);
    Serial.print(",");

    Serial.println(saturated);

    // Maintain scan rate
    while ((uint32_t)(micros() - t0) <
           (uint32_t)stepPeriodUs)
    {
        // Wait
    }
}

// =====================================================
// FORWARD SWEEP
// -0.2 V TO +0.6 V
// =====================================================

void forwardSweep(uint32_t cycle)
{
    Serial.print("# Forward sweep, cycle ");
    Serial.println(cycle);

    int32_t code = startCode;

    while (code > (int32_t)endCode)
    {
        takeMeasurement(
            (uint16_t)code,
            cycle,
            +1
        );

        code -= DAC_STEP;
    }

    takeMeasurement(endCode, cycle, +1);
}

// =====================================================
// REVERSE SWEEP
// +0.6 V TO -0.2 V
// =====================================================

void reverseSweep(uint32_t cycle)
{
    Serial.print("# Reverse sweep, cycle ");
    Serial.println(cycle);

    int32_t code = (int32_t)endCode + DAC_STEP;

    while (code < (int32_t)startCode)
    {
        takeMeasurement(
            (uint16_t)code,
            cycle,
            -1
        );

        code += DAC_STEP;
    }

    takeMeasurement(startCode, cycle, -1);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(230400);
    delay(1500);

    pinMode(MUX_0, OUTPUT);
    pinMode(MUX_1, OUTPUT);
    pinMode(MUX_EN, OUTPUT);

    digitalWrite(MUX_EN, LOW);

    DAC_Wire.begin(DAC_SDA, DAC_SCL);
    ADC_Wire.begin(ADC_SDA, ADC_SCL);

    DAC_Wire.setClock(400000);
    ADC_Wire.setClock(400000);

    selectFeedback();

    // Potential endpoints:
    // -0.2 V -> DAC 1.85 V
    // +0.6 V -> DAC 1.05 V

    startCode = voltageToDAC(
        potentialToDAC(CV_MIN_V)
    );

    endCode = voltageToDAC(
        potentialToDAC(CV_MAX_V)
    );

    float voltagePerStep =
        DAC_SUPPLY * DAC_STEP / 4095.0f;

    stepPeriodUs =
        (voltagePerStep / SCAN_RATE_V_S)
        * 1000000.0f;

    Serial.println();
    Serial.println("# FERRI/FERROCYANIDE CV SYSTEM");
    Serial.println("# ESP32 POTENTIOSTAT");

    Serial.print("# WE virtual ground: ");
    Serial.print(WE_MIDRAIL_V, 3);
    Serial.println(" V");

    Serial.print("# CV window: ");
    Serial.print(CV_MIN_V, 3);
    Serial.print(" to ");
    Serial.print(CV_MAX_V, 3);
    Serial.println(" V");

    Serial.print("# Scan rate: ");
    Serial.print(SCAN_RATE_V_S * 1000.0f);
    Serial.println(" mV/s");

    Serial.print("# DAC step: ");
    Serial.println(DAC_STEP);

    Serial.print("# Step period: ");
    Serial.print(stepPeriodUs / 1000.0f, 3);
    Serial.println(" ms");

    Serial.print("# Cycles: ");
    Serial.println(TOTAL_CYCLES);

    Serial.println("# Feedback resistor: 10 kOhm");

    calibrateTIA();

    Serial.println(
        "TIME_US,CYCLE,DIRECTION,E_WE_RE_V,WE_MIDRAIL_V,DAC_SIG_V,TIA_V,CURRENT_uA,SAT"
    );

    Serial.println("# Reconnect WE to the electrochemical cell");
    Serial.println("# Send S to start the experiment");

    Serial.println("READY_TO_START");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
    if (experimentFinished)
    {
        delay(1000);
        return;
    }

    if (!experimentStarted && Serial.available())
    {
        char command = Serial.read();

        if (command == 's' || command == 'S')
        {
            experimentStarted = true;

            Serial.println("# CV EXPERIMENT STARTED");

            for (uint32_t cycle = 1;
                 cycle <= TOTAL_CYCLES;
                 cycle++)
            {
                Serial.print("# START CYCLE ");
                Serial.println(cycle);

                forwardSweep(cycle);

                reverseSweep(cycle);

                Serial.print("# END CYCLE ");
                Serial.println(cycle);
            }

            Serial.println("# CV_COMPLETE");

            experimentFinished = true;
        }
    }
}
