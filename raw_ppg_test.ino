/*
 * raw_ppg_test.ino
 * Prints raw IR and RED readings from the MAX30102.
 * No finger: low values (hundreds). Finger on sensor: tens of thousands.
 */

#include <Wire.h>
#include "MAX30105.h"

MAX30105 particleSensor;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(1000);

  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("ERROR: MAX30102 not found!");
    while (1);
  }

  Serial.println("SUCCESS: MAX30102 detected!");
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x0A);
}

void loop() {
  Serial.print("IR: ");
  Serial.print(particleSensor.getIR());
  Serial.print("  RED: ");
  Serial.println(particleSensor.getRed());
  delay(100);
}
