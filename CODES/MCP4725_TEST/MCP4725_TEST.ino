
#include <Wire.h>

// ======================================
// MCP4725 CONFIGURATION
// ======================================

#define DAC_SDA 21
#define DAC_SCL 22
#define DAC_ADDR 0x60

// ESP32 INTERNAL ADC
#define ADC_PIN 35

// ======================================
// SWEEP SETTINGS
// ======================================

const int DAC_MIN = 0;
const int DAC_MAX = 3600;
const int DAC_STEP = 12;

const float DAC_VREF = 3.3;

// Update every 3 milliseconds
const unsigned long SAMPLE_INTERVAL_US = 3000;

int dacValue = DAC_MIN;
int direction = 1;

unsigned long nextSample = 0;

// ======================================
// INITIALIZE DAC
// ======================================

bool initDAC() {

  Wire.begin(DAC_SDA, DAC_SCL);
  Wire.setClock(400000);

  Wire.beginTransmission(DAC_ADDR);

  return Wire.endTransmission() == 0;
}

// ======================================
// WRITE MCP4725
// ======================================

bool writeDAC(uint16_t value) {

  value = constrain(value, 0, 4095);

  Wire.beginTransmission(DAC_ADDR);

  Wire.write(0x40);
  Wire.write(value >> 4);
  Wire.write((value & 0x0F) << 4);

  return Wire.endTransmission() == 0;
}

// ======================================
// READ ESP32 INTERNAL ADC
// ======================================

float readInternalADC() {

  const int samples = 4;

  uint32_t total = 0;

  for (int i = 0; i < samples; i++) {

    total += analogReadMilliVolts(ADC_PIN);
  }

  return (total / (float)samples) / 1000.0;
}

// ======================================
// SETUP
// ======================================

void setup() {

  Serial.begin(230400);

  analogReadResolution(12);

  analogSetPinAttenuation(
    ADC_PIN,
    ADC_11db
  );

  if (!initDAC()) {

    Serial.println("# DAC NOT FOUND");

    while (true) {
      delay(1000);
    }
  }

  writeDAC(0);

  delay(300);

  Serial.println(
    "TIME_MS,DAC_VALUE,EXPECTED_V,ADC_V"
  );

  nextSample = micros();
}

// ======================================
// CONTINUOUS TRIANGULAR SWEEP
// ======================================

void loop() {

  // Fixed update interval
  unsigned long now = micros();

  if ((long)(now - nextSample) < 0) {
    return;
  }

  nextSample += SAMPLE_INTERVAL_US;

  // Write DAC
  if (!writeDAC(dacValue)) {
    return;
  }

  // Read ADC
  float adcVoltage = readInternalADC();

  // Ideal DAC voltage
  float expectedVoltage =
    (dacValue / 4095.0) * DAC_VREF;

  // Send CSV data
  Serial.print(millis());
  Serial.print(",");

  Serial.print(dacValue);
  Serial.print(",");

  Serial.print(expectedVoltage, 4);
  Serial.print(",");

  Serial.println(adcVoltage, 4);

  // Update triangular waveform
  dacValue += direction * DAC_STEP;

  if (dacValue >= DAC_MAX) {

    dacValue = DAC_MAX;
    direction = -1;
  }

  if (dacValue <= DAC_MIN) {

    dacValue = DAC_MIN;
    direction = 1;
  }
}
