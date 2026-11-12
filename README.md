# stone-wearable

A low-cost wearable that measures heart rate and heart rate variability (HRV) from a reflective PPG sensor, built toward overnight sleep tracking with a companion mobile app.

**Status:** Heart rate and HRV working on the bench. Sleep detection and the mobile app are next.

## Results so far

- **Heart rate within ±3 BPM** of a Whoop strap in a seated fingertip test
- **HRV (RMSSD)** computed on-device with sub-sample beat timing and artifact rejection
- **On a synthetic test signal**, RMSSD within 1.1 ms of the true value (3.5% error, down from 11% without interpolation)

## Hardware

| Part | Role |
|---|---|
| Seeed XIAO nRF52840 Sense | Microcontroller, Bluetooth LE, built-in IMU |
| MAX30102 | PPG sensor (red + infrared LEDs, photodiode) |
| TP4056 USB-C module | Li-ion charging and protection |
| 3.7 V 500 mAh Li-Po | Battery |

Full parts list: [`hardware/BOM.csv`](hardware/BOM.csv). Wiring and bring-up: [`docs/HARDWARE_SETUP.md`](docs/HARDWARE_SETUP.md).

## How it works

```
raw IR (100 Hz) → drift removal → smoothing → adaptive-threshold peak detection
→ parabolic peak interpolation → beat intervals → artifact rejection → BPM + RMSSD
```

Everything runs on the microcontroller in a single pass per sample, with no buffering of the raw signal. The design choices and test results are written up in [`docs/SIGNAL_PROCESSING.md`](docs/SIGNAL_PROCESSING.md).

## Repository layout

```
firmware/
  i2c_scanner/      Confirms the sensor responds on the I2C bus
  raw_ppg_test/     Streams raw IR/RED readings
  hr_hrv_v1/        Heart rate + HRV (current main firmware)
hardware/           Bill of materials
docs/               Hardware setup, signal processing write-up, test logs
app/                React Native app (Phase 4)
```

## Running the firmware

1. Arduino IDE → Preferences → add the Seeed board URL:
   `https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json`
2. Boards Manager → install **Seeed nRF52 Boards**, select **Seeed XIAO nRF52840 Sense**.
3. Library Manager → install **SparkFun MAX3010x Pulse and Proximity Sensor Library**.
4. Open `firmware/hr_hrv_v1/hr_hrv_v1.ino`, upload, and open the Serial Monitor at 115200 baud.
5. Rest a fingertip lightly on the sensor and hold still.

## Roadmap

- [x] Phase 1: Hardware bring-up (I2C, raw PPG)
- [x] Phase 2: Signal processing (BPM, RMSSD)
- [ ] Phase 3: Accelerometer + sleep/wake detection
- [ ] Phase 4: Bluetooth LE + React Native app
- [ ] Phase 5: Enclosure, overnight testing, demo video

## Author

Stone Larson, Electrical Engineering, University of Nebraska Omaha
