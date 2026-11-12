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
- The threshold is 35% of that envelope, with a small floor so flat noise never counts.

A fixed threshold fails because pulse amplitude varies a lot between people, fingers, and pressure. The adaptive version adjusts within a few beats.

A peak that arrives less than 333 ms after the last beat (faster than 180 BPM) is treated as noise and ignored.

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
