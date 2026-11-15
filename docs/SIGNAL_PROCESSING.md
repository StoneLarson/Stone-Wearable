# Signal Processing

How `firmware/hr_hrv_v1` turns raw light readings into heart rate and HRV.

## 1. The raw signal

The MAX30102 shines an infrared LED into the fingertip and measures how much light reflects back. Each heartbeat pushes a pulse of blood into the tissue, which absorbs more light, so the reflected IR **dips slightly with every beat**.

The problem is scale. With a finger on the sensor the raw IR reading sits around 30,000 counts, while the heartbeat moves it by only tens to a few hundred counts. On top of that, the baseline drifts by hundreds of counts as finger pressure and blood volume change. The pulse is smaller than the drift, so it can't be read directly.

## 2. Sampling

The sensor runs at 400 Hz with 4-sample averaging, giving **100 samples per second**. Every sample is read from the sensor's FIFO buffer, so none are skipped and the spacing is exactly 10 ms. Beat times are computed from the sample count, not from `millis()`, which keeps timing independent of how long the loop takes.

## 3. Filtering

Two exponential moving averages (EMAs) form a simple band-pass filter.

| Stage | Formula | Cutoff | Purpose |
|---|---|---|---|
| Baseline tracker | `dc += 0.01 * (ir - dc)` | ~0.16 Hz | Follows the slow drift |
| Drift removal | `ac = dc - ir` | — | Leaves only the fast pulse, inverted so each beat is a positive peak |
| Smoother | `lp += 0.2 * (ac - lp)` | ~3.5 Hz | Removes sensor noise |

The passband (~0.16 to 3.5 Hz) covers heart rates from about 10 to 210 BPM. An EMA needs one multiply and one add per sample, which is why it was chosen over a higher-order filter for a microcontroller.

The first 2 seconds after a finger lands are ignored while the filters settle.

## 4. Beat detection

A beat is a local maximum in the filtered signal that rises above an **adaptive threshold**:

- An envelope tracks the recent peak height and decays by 0.5% per sample.
- The threshold is 50% of that envelope, with a small floor so flat noise never counts.

A fixed threshold fails because pulse amplitude varies a lot between people, fingers, and pressure. The adaptive version adjusts within a few beats.

A peak that arrives less than 333 ms after the last beat (faster than 180 BPM) is treated as noise and ignored.

## 4b. The dicrotic notch, and why it doubled the heart rate

The first hardware test with a fingertip read **131 BPM when the true rate was 63**. The intervals showed what was happening:

```
456 + 502 = 958 ms
463 + 448 = 911 ms
542 + 407 = 949 ms
```

Each pair summed to one real cardiac cycle. The detector was finding two peaks per heartbeat.

The second peak is the **dicrotic notch**: a real feature of the pressure waveform, produced when the aortic valve closes and blood rebounds off it. It appears 300–450 ms after the systolic peak at roughly 20–60% of its height, and it is *more* pronounced in young subjects with elastic arteries. The 333 ms floor was set to allow heart rates up to 180 BPM, which is exactly wide enough to let the notch through.

Three changes fixed it.

**1. Threshold raised from 35% to 50% of the envelope.** This alone rejects notches below half the systolic height, but not the larger ones.

**2. A refractory period scaled to the current heart rate.** Instead of a fixed 333 ms floor, the next beat must arrive at least 60% of the recent mean interval after the last one. At 63 BPM that is 570 ms, comfortably past the notch.

**3. A height test to tell a notch from a genuinely early beat.** A refractory period on timing alone has the opposite failure mode: if the heart rate really does rise sharply, every real beat lands inside the window and gets blocked, so the rate reads *half*. A simulated jump from 60 to 125 BPM confirmed this, reading 62. The discriminator is amplitude, since a dicrotic notch is always well below the beat it follows while a genuinely early beat is full height. A peak inside the refractory window is kept only if it exceeds 85% of the envelope.

One subtlety made this harder than expected. The height was first compared against a running average of accepted peak heights, which failed on weak signals: once a notch is wrongly accepted, that average sags toward the notches and stops being able to tell them apart — a self-reinforcing error. Comparing against the **peak envelope** instead fixes it, because the envelope tracks the maximum and cannot be pulled down by smaller peaks.

A second subtlety: a first-order low-pass filter attenuates the narrow systolic peak more than the wider notch, so after filtering the notch is *relatively taller* than in the raw signal. The 85% figure accounts for this and was chosen by sweeping the threshold across 30 simulated cases.

**Results** (synthetic PPG with a dicrotic notch, true rate known):

| Case | Before | After |
|---|---|---|
| 63 BPM, notch at 45% of peak | 171 BPM | 62.7 BPM |
| 63 BPM, notch at 60% of peak | 172 BPM | 63.3 BPM |
| 48 BPM (athlete at rest) | 173 BPM | 48.2 BPM |
| 100 BPM | 101 BPM | 101 BPM |
| Sudden jump 60 → 125 BPM | 62 BPM | re-locks in 1.2 s |
| Sweep, 42–130 BPM | — | all within ±2 BPM |

Of 30 simulated cases spanning notch height, notch delay, heart rate, pulse amplitude, noise, and motion, 25 land within 4 BPM. The five that do not are outside the usable envelope: a notch at 70–80% of the systolic peak (genuinely ambiguous), a 2:1 signal-to-noise ratio, and heavy motion. Motion is the case the IMU is meant to gate in Phase 3.

## 5. Sub-sample timing (parabolic interpolation)

At 100 samples/s, a raw peak time is only known to the nearest 10 ms. For HRV, where beat-to-beat differences are often only 20 to 50 ms, that rounding adds noticeable error.

The fix fits a parabola through the three samples around each peak and takes the parabola's vertex as the true peak time:

```
offset = 0.5 * (left - right) / (left - 2*mid + right)     // in samples, -0.5 to 0.5
peakTime = (peakSampleIndex + offset) * 10 ms
```

**Simulation result** (synthetic PPG with known beat times, drift, and noise; true RMSSD 31.4 ms):

| Method | Measured RMSSD | Error |
|---|---|---|
| Nearest sample | 34.9 ms | +3.5 ms (11%) |
| Parabolic interpolation | 32.5 ms | +1.1 ms (3.5%) |

## 6. Artifact rejection

One missed or extra beat produces an interval far from the others and would inflate RMSSD. Each new interval is compared with the mean of the last 4 accepted intervals:

- More than 25% off: rejected, and the HRV chain is broken so no difference is computed across it.
- Three rejections in a row: the heart rate has genuinely changed, so the average restarts.
- Gaps over 1500 ms (slower than 40 BPM) also break the chain.

When the average restarts, the HRV window is flushed as well. Without this, squared differences measured across the old beat pattern stay in the 30-sample window and inflate RMSSD for the next 30 beats. This was visible in the first hardware log, where RMSSD sat at 60–68 ms during and after a stretch of double-counted beats, well above the ~48 ms the clean intervals in that same log imply.

In the first finger test, every rejected interval was 1400 to 1440 ms, almost exactly double the normal ~720 ms interval. Each was a single missed beat, and none reached the RMSSD value.

## 7. Outputs

- **BPM**: 60,000 divided by the mean of the last 4 accepted intervals.
- **RMSSD**: root mean square of successive differences between back-to-back accepted intervals, over the last 30 differences. Reported once at least 10 are available.

## Validation so far

| Test | Result |
|---|---|
| BPM vs. Whoop strap (seated, finger) | Agreed within ±3 BPM |
| RMSSD, seated, awake, ~60 s | Settled at ~29–32 ms (plausible range for a short awake reading) |
| Missed beats | ~1 per 15 beats at 40% threshold; all caught by artifact rejection |
| Synthetic test, 198 known beats | 197 detected, 0 false rejections, RMSSD within 1.1 ms |

Logs are in [`test-logs/`](test-logs/).

## Known limitations

- Validated on a fingertip only. A wrist placement will have a weaker signal and more motion artifact.
- Whoop is also an optical sensor, so it's a comparison, not ground truth. A chest strap (ECG) is the proper HRV reference.
- Daytime RMSSD is not directly comparable to Whoop's overnight HRV figure.
