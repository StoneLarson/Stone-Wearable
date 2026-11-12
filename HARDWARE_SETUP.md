# Hardware Setup

## Block diagram

```
USB-C 5V ──► TP4056 charger ──► Li-Po 3.7 V 500 mAh
                  │ OUT+ / OUT-
                  ▼
           XIAO nRF52840 Sense  (MCU, BLE 5.0, LSM6DS3 IMU)
             3V3 │   │ D4 (SDA)  │ D5 (SCL)
                 ▼   ▼           ▼
              MAX30102 PPG sensor  (I2C address 0x57)
```

## Wiring

| From | To | Notes |
|---|---|---|
| XIAO 3V3 | MAX30102 VIN | Measured 3.3 V at VIN |
| XIAO GND | MAX30102 GND | Common ground |
| XIAO D4 | MAX30102 SDA | I2C data |
| XIAO D5 | MAX30102 SCL | I2C clock |
| Battery red / black | TP4056 B+ / B- | Polarity verified with a multimeter |
| TP4056 OUT+ / OUT- | XIAO battery input / GND | Confirm the battery pin on the Seeed pinout |

No external pull-up resistors are used: the MAX30102 breakout already has 4.7 kΩ pull-ups on SDA and SCL. Adding a second set in parallel would halve the effective value.

## Assembly

1. Soldered 2.54 mm male headers to the XIAO's castellated edge pads, using the breadboard as a jig to keep the pins straight.
2. Soldered 22 AWG leads to the TP4056's B+, B-, OUT+ and OUT- pads. The input pads were left bare, since the USB-C port feeds them.
3. Checked every joint for bridges with a multimeter in continuity mode.

## Bring-up checklist

| Check | Result |
|---|---|
| TP4056 continuity, polarity, output, charging LEDs | Pass |
| MAX30102 VIN voltage | 3.3 V |
| I2C scan (`firmware/i2c_scanner`) | Device found at 0x57 |
| Raw readings (`firmware/raw_ppg_test`) | ~500 counts no finger, ~28–30k with finger |
| HR/HRV (`firmware/hr_hrv_v1`) | Within ±3 BPM of a Whoop strap |

## Troubleshooting notes

- **No COM port / upload timeout ("semaphore timeout period has expired"):** double-tap the XIAO's reset button to enter the bootloader (LED pulses), select the new COM port, and upload immediately.
- **Uploads fail with the charger connected:** upload with only the XIAO's own USB-C plugged in.
- **"Python was not found" (exit status 9009) on Windows:** the Seeed board package needs Python 3 on PATH. Install it, then restart the Arduino IDE.
- **`'float y0' redeclared` compile error:** `y0`, `y1`, `j0`, `j1` are Bessel functions in `math.h`. Don't use them as variable names.
