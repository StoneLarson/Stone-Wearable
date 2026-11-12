/*
 * i2c_scanner.ino
 * Scans the I2C bus and prints every responding address.
 * Expected result with the MAX30102 connected: a device at 0x57.
 */

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(1000);

  Serial.println("\nI2C Scanner Start");
  Wire.begin();

  int nDevices = 0;
  Serial.println("Scanning...");
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    }
  }

  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("I2C scan complete\n");
}

void loop() {
  delay(5000);
}
