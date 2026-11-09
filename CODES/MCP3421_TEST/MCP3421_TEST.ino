
#include <Wire.h>

// =====================================
// PIN CONFIGURATION
// =====================================

#define DAC_PIN 25

#define ADC_SDA 32
#define ADC_SCL 33

#define ADC_ADDR 0x68

// =====================================
// MCP3421 CONFIGURATION
// =====================================

// Continuous conversion
// 12-bit resolution
// PGA gain = 1

#define ADC_CONFIG 0x10

// 12-bit LSB at PGA x1 = 1 mV
#define ADC_LSB 0.001

// =====================================
// DAC TRIANGULAR WAVEFORM
// =====================================

const int DAC_MIN = 0;
const int DAC_MAX = 150;
const int DAC_STEP = 2;

int dacValue = DAC_MIN;
int direction = 1;

// =====================================
// ADC INITIALIZATION
// =====================================

bool initADC() {

  Wire1.begin(ADC_SDA, ADC_SCL);
  Wire1.setClock(100000);

  Wire1.beginTransmission(ADC_ADDR);
  Wire1.write(ADC_CONFIG);

  byte error = Wire1.endTransmission();

  if (error == 0) {
    Serial.println("# MCP3421 detected");
    Serial.println("# Continuous 12-bit mode");
    return true;
  }

  return false;
}

// =====================================
// ADC READING
// =====================================

bool readADC(float &voltage, int16_t &rawValue) {

  unsigned long start = millis();

  while (millis() - start < 30) {

    if (Wire1.requestFrom(
          (uint8_t)ADC_ADDR,
          (uint8_t)3) == 3) {

      uint8_t highByte = Wire1.read();
      uint8_t lowByte  = Wire1.read();
      uint8_t config   = Wire1.read();

      // Check conversion ready
      if ((config & 0x80) == 0) {

        // Combine ADC data
        uint16_t data =
          ((uint16_t)highByte << 8) | lowByte;

        // Extract 12-bit result
        int16_t raw = data & 0x0FFF;

        // Convert 12-bit two's complement
        if (raw & 0x0800) {
          raw -= 4096;
        }

        rawValue = raw;

        voltage = rawValue * ADC_LSB;

        return true;
      }
    }

    delayMicroseconds(300);
  }

  return false;
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  // Initialize DAC
  dacWrite(DAC_PIN, 0);

  // Initialize ADC
  if (!initADC()) {

    Serial.println("# ERROR: MCP3421 not detected");

    while (true) {
      delay(1000);
    }
  }

  delay(10);

  // CSV HEADER
  Serial.println(
    "TIME_MS,DAC_VALUE,EXPECTED_V,ADC_V"
  );
}

// =====================================
// MAIN LOOP
// =====================================

void loop() {

  // Generate DAC voltage
  dacWrite(DAC_PIN, dacValue);

  // Allow the ADC to acquire the updated voltage
  delay(6);

  float adcVoltage = 0;
  int16_t rawValue = 0;

  bool success = readADC(adcVoltage, rawValue);

  if (success) {

    // Ideal DAC voltage
    float expectedVoltage =
      (dacValue / 255.0) * 3.3;

    // CSV OUTPUT
    Serial.print(millis());
    Serial.print(",");

    Serial.print(dacValue);
    Serial.print(",");

    Serial.print(expectedVoltage, 5);
    Serial.print(",");

    Serial.println(adcVoltage, 5);
  }

  else {

    Serial.print("# ADC_TIMEOUT,");
    Serial.println(dacValue);
  }

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
