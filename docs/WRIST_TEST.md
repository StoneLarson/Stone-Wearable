# Wrist Signal Test

A fingertip gives a large, clean pulse. A wrist gives a much smaller one. Before designing a case, this test answers two questions:

1. Where on the wrist (or arm) does the MAX30102 get a usable pulse?
2. What is the lowest LED power that gives clean beats there?

Firmware: [`firmware/wrist_test`](../firmware/wrist_test/wrist_test.ino)

## Running it

1. Upload `wrist_test.ino` and open the Serial Monitor at 115200 baud.
2. Strap the sensor on with velcro, a rubber band, or tape. Snug, not tight.
3. Type `1`–`4` in the Serial Monitor's input box and press Enter to change LED power without re-uploading:

| Key | LED setting | Current |
|---|---|---|
| 1 | 0x1F | ~6 mA (fingertip default) |
| 2 | 0x3F | ~12.6 mA (starting point) |
| 3 | 0x7F | ~25 mA |
| 4 | 0xBF | ~38 mA |

After each change, hold still for about 10 seconds while the filters settle and the beat history fills.

## Reading the status line

```
LED 0x3F (12.6 mA) | DC 41230 | AC 85 | PI 0.21% | beats 18/20 | BPM 63.2 | RMSSD 31.0 | GOOD
```

| Field | Meaning |
|---|---|
| DC | Average raw IR level: how much light comes back |
| AC | Peak-to-peak size of the filtered pulse over the last 2 s |
| PI | Perfusion index, AC ÷ DC × 100. **The main number for comparing spots.** Higher is better. |
| beats | Of the last 20 peaks found, how many gave a believable interval. Dicrotic notches are skipped rather than counted here, since rejecting them is the detector working correctly. |
| BPM / RMSSD | Same as `hr_hrv_v1`. Shows `--` if there's been no good beat for 5 s |
| Label | GOOD (≥85% believable, PI ≥ 0.03%), FAIR (≥60%), POOR, plus NO SKIN, SATURATED, SETTLING, COLLECTING |

The label thresholds are starting guesses, not validated limits. Expect to tune them once there's real wrist data.

## Protocol

For each spot, start at key `2`, and go up to `3` or `4` only if the label isn't GOOD. Log the lowest setting that gives GOOD.

Spots:
- **A.** Top of the wrist, 1–2 finger widths up the forearm from the wrist bone
- **B.** Same, but on the underside of the wrist
- **C.** Mid-forearm, top side
- **D.** Upper arm (bicep), as the fallback

Positions at the best spot: seated, lying on your back, lying on your side with the arm under you.

Wear a Whoop on the other wrist to compare BPM.

## Results

| Spot | Position | Lowest GOOD LED | PI | beats | BPM | Whoop BPM | Notes |
|---|---|---|---|---|---|---|---|
| A | seated | | | | | | |
| B | seated | | | | | | |
| C | seated | | | | | | |
| D | seated | | | | | | |
| best | on back | | | | | | |
| best | on side | | | | | | |

**Pass:** at least one spot gives GOOD lying down, with BPM within about ±5 of the Whoop.

## How the wrist detector differs from `hr_hrv_v1`

Both sketches now share the dicrotic-notch rejection described in [`SIGNAL_PROCESSING.md`](SIGNAL_PROCESSING.md) (section 4b), which is what fixed the doubled heart rate in the first hardware test. On top of that, simulating small pulses showed two further failure modes in the fingertip pipeline, so the wrist test adds two changes (set `WRIST_DETECTOR = false` to get exact v1 behavior for comparison):

1. **Breathing wander.** Breathing moves the PPG baseline up and down by about as much as a small wrist pulse. The v1 baseline filter (~0.16 Hz) lets it through, so pulses in the troughs fall under the threshold and get missed. A second, faster baseline (~0.6 Hz) is now removed before peak detection.
2. **Noise wiggles.** On a small pulse, noise puts several tiny local peaks on top of each beat, and v1 counts the extra ones as noise peaks. The beat is now the highest point between the signal crossing the threshold and falling below half of it, so each pulse gives exactly one beat.

Synthetic results (40 s runs, pulse 30 counts on a 40,000 baseline, noise 4 counts):

| Case | v1 detector (believable peaks) | Wrist detector |
|---|---|---|
| Nearly steady baseline (12 s) | 11/17 | 11/11 |
| Slow baseline drift (40 s) | 4/20 | 20/20 |
| Breathing wander as big as the pulse (40 s) | 2/20 | 20/20 after settling |
| Fingertip-size pulse | 11/11, RMSSD 39.5 | 11/11, RMSSD 40.1 (true 39.9) |

## Known limitation

On the small simulated pulse, **RMSSD reads about 6–10 ms high** (e.g. 38 vs a true 32), because noise moves the exact peak time by a few milliseconds and RMSSD is sensitive to that. BPM is unaffected. Trust BPM from this test, treat RMSSD as rough, and revisit timing accuracy in Phase 3 once real wrist data is available.
