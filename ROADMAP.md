# Remaining Work

Status as of 2026-11-18. Phase 2 (signal processing) is essentially done on the
fingertip. Everything below is what stands between here and a finished project.

Critical path is marked **[CP]** — these block everything downstream.

---

## Phase 2 close-out

1. **[CP] Commit and push.** Nothing has been pushed in three sessions. New
   `firmware/wrist_test/`, updated `hr_hrv_v1.ino`, `SIGNAL_PROCESSING.md`,
   `WRIST_TEST.md`, `README.md`, and four files in `docs/test-logs/` exist only
   as local files. Do this before anything else.
2. **Port the fast baseline filter** (~0.6 Hz) from `wrist_test.ino` into
   `hr_hrv_v1.ino`. Simulation: baseline drift at 160 counts against a
   150-count pulse costs 26% of beats, and at wrist amplitude detection fails
   entirely. `hr_hrv_v1` cannot work on a wrist without this.
3. **Optional: paced-breathing re-run** for a respiratory accuracy figure.
   Inhale 5 s, exhale 5 s, 3 minutes = exactly 18 breaths, then hold 20 s.
   The mechanism is already proven; this only produces a number to quote.

## Phase 3a — Wrist mechanical (current blocker)

4. **[CP] Get a strap.** No velcro on hand and none ordered. Options: order a
   velcro watch strap, scavenge an old watch strap, or use an elastic bandage.
   Not tape — it does not hold constant pressure.
5. **[CP] Mount the sensor so it cannot move against skin.** Sensor face flush
   to skin, foam behind the board to push it in, constant pressure. The wrist
   problem is mechanical, not algorithmic: DC swung 3.5x within one run, which
   is contact changing, not physiology.
6. **Light-seal the sensor.** Ambient IR leaks in at the wrist. Check with LEDs
   off (key `1` in `wrist_test`); below ~5,000 is fine, above ~20,000 means no
   LED setting will help.
7. **Run the wrist_test protocol.** Spots A-D seated, then the best spot lying
   on your back and on your side with the arm under you.
8. **Pick the spot and the lowest LED level** that gives clean beats there.
9. **Validate wrist BPM** against a simultaneous reference — Whoop, or a
   fingertip reading on the other hand.
10. **Validate wrist RMSSD.** Currently untrusted: simulation read 140 ms
    against a 34 ms truth on a weak pulse. HRV feeds the recovery score, so this
    matters. Needs a simultaneous fingertip comparison.
11. **Tune the wrist_test quality labels.** They are currently anti-correlated
    with accuracy — reading POOR while BPM was right and FAIR while dropping
    beats.
12. **Confirm the signal survives a position change** (rolling over) without
    losing lock.

## Phase 3b — Power

13. **[CP] Fix the battery connection.** The 503030's leads were cut short at
    the 1.25 mm housing. Either extend the stubs with fine stranded wire
    (heat-sink the joint, insulate each separately) or buy a replacement with
    longer leads or a 2.0 mm connector.
14. **Verify polarity** with a meter: red positive, 3.7-4.2 V, and confirm the
    two leads are not shorted.
15. **Identify the XIAO BAT pads.** BAT- by continuity to GND with USB
    unplugged; BAT+ reads ~4.1-4.2 V with USB plugged in.
16. **Solder the pigtail to BAT+/BAT-.** Check the connector pins read open
    before inserting the battery.
17. **Confirm it boots on battery alone**, USB unplugged.
18. **Confirm charging.** Default 50 mA (6-11 h for 500 mAh); P0.13 LOW gives
    100 mA. Charge LED is P0.17.
19. **Measure actual current draw** and compute real runtime. The 70 mA estimate
    was never verified.
20. **Reduce power for an 8-hour night.** Levers: lower LED current, lower
    sample rate between measurements, duty-cycle the sensor, sleep the nRF52
    between samples.
21. **Battery percentage** via P0.31 / AIN7_BAT. Documented only for the Plus
    variant — unverified on the Sense, needs testing. Battery read enable is
    P0.14.
22. **Low-battery handling** so it does not die silently mid-night.

## Phase 3c — IMU and sleep/wake

23. **Read the LSM6DS3** accelerometer built into the XIAO Sense.
24. **Compute motion magnitude per epoch** (30 s or 60 s epochs).
25. **Sleep/wake classification** from motion, using standard actigraphy
    thresholds.
26. **[CP] Gate PPG artifact rejection on IMU motion.** This is the one thing
    interval width provably cannot do — retention stays near 50-64% under
    simulated motion at any band setting.
27. **Derive sleep latency, sleep efficiency and awakenings** from the
    sleep/wake series. These feed the sleep score.

## Phase 3d — On-device computation and logging

28. **Decide where respiratory rate is computed.** On-device Lomb-Scargle is
    feasible on an nRF52, but logging the intervals and computing on the laptop
    is simpler and loses nothing.
29. **Epoch logging to internal flash**, roughly 1000 epochs x 16 B.
30. **Timestamping.** The XIAO has no RTC, so it needs a time sync at session
    start.
31. **[CP] Survive a full 8-hour night** without resetting or filling memory.
32. **First full overnight recording.**

## Phase 4 — Transfer and website

33. **BLE GATT service** exposing the stored data.
34. **Web Bluetooth sync from the laptop.** iPhone cannot do Web Bluetooth, which
    is why the site syncs from the laptop.
35. **Data storage** on the site.
36. **Recovery score (0-100)** from HRV, resting HR, respiratory rate and sleep
    quality.
37. **Sleep score** from latency, efficiency and awakenings.
38. **Baseline establishment.** Recovery scores are meaningless without a
    personal baseline, which needs roughly two weeks of nights. Start collecting
    early.
39. **Graphs:** sleep vs nightly target, and sleep debt.
40. **Deploy the site.**

## Phase 5 — Enclosure and portfolio

41. **Case design** housing XIAO, MAX30102 and battery; 3D print it.
42. **Sensor window and light sealing** in the case.
43. **Strap integration** with the case.
44. **Multi-night validation** against the Whoop.
45. **README, wiring diagram, demo video, technical writeup.** The four bug
    writeups in `SIGNAL_PROCESSING.md` are the strongest portfolio material here
    — they show hardware-driven debugging, not just working code.

---

## Things to buy

- Velcro or elastic strap (blocks Phase 3a)
- Replacement 503030 LiPo with longer leads or a 2.0 mm connector, unless the
  existing one's stubs can be extended (blocks Phase 3b)
