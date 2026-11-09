
#include <Wire.h>

// DAC - MCP4725
#define DAC_SDA 21
#define DAC_SCL 22

// ADC - MCP3421
#define ADC_SDA 32
#define ADC_SCL 33

void scanI2C(TwoWire &bus, const char* busName) {

  Serial.println();
  Serial.print("Scanning ");
  Serial.println(busName);

  int deviceCount = 0;

  for (byte address = 1; address < 127; address++) {

    bus.beginTransmission(address);
    byte error = bus.endTransmission();

    if (error == 0) {

      Serial.print("Device found at address: 0x");
      if (address < 16) Serial.print("0");

      Serial.println(address, HEX);
      deviceCount++;
    }
  }

  if (deviceCount == 0) {
    Serial.println("No I2C devices found!");
  } else {
    Serial.print("Total devices found: ");
    Serial.println(deviceCount);
  }
}

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println("================================");
  Serial.println("POTENTIOSTAT I2C SCANNER");
  Serial.println("================================");

  // Initialize DAC I2C bus
  Wire.begin(DAC_SDA, DAC_SCL);

  // Initialize ADC I2C bus
  Wire1.begin(ADC_SDA, ADC_SCL);

  scanI2C(Wire, "DAC BUS (MCP4725)");
  scanI2C(Wire1, "ADC BUS (MCP3421)");

  Serial.println();
  Serial.println("I2C SCAN COMPLETED");
}

void loop() {
  // No continuous scanning required
}
