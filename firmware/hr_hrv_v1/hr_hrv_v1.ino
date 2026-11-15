/*
 * hr_hrv_v1.ino
 * Heart rate (BPM) and heart rate variability (RMSSD) from a MAX30102 PPG sensor.
 *
 * Hardware: Seeed XIAO nRF52840 Sense + MAX30102 breakout (I2C: SDA=D4, SCL=D5, VIN=3V3)
 * Library:  SparkFun MAX3010x Pulse and Proximity Sensor Library
 *
 * Pipeline (100 samples/s):
 *   raw IR -> baseline removal (EMA high-pass) -> smoothing (EMA low-pass)
 *   -> adaptive-threshold peak detection -> parabolic peak interpolation
 *   -> inter-beat intervals (IBI) -> artifact rejection -> BPM + RMSSD
 */

#include <Wire.h>
#include "MAX30105.h"

MAX30105 sensor;

// ---------- Sensor configuration ----------
const byte LED_POWER   = 0x1F;  // ~6.4 mA LED current
const byte SAMPLE_AVG  = 4;     // average 4 raw samples per output sample
const byte LED_MODE    = 2;     // Red + IR (MAX30102 has no green LED)
const int  SAMPLE_RATE = 400;   // 400 Hz / 4 averaging = 100 samples/s
const int  PULSE_WIDTH = 411;   // us, gives 18-bit resolution
const int  ADC_RANGE   = 4096;

const float SAMPLE_MS = 10.0;   // 100 samples/s -> 10 ms per sample

// ---------- Filter settings ----------
const float DC_ALPHA = 0.01;  // baseline tracker (~0.16 Hz): removes drift
const float LP_ALPHA = 0.2;   // smoother (~3.5 Hz): removes high-freq noise

// ---------- Beat detection settings ----------
const long          FINGER_THRESHOLD = 10000; // raw IR above this = finger present
const unsigned int  SETTLE_SAMPLES   = 200;   // ignore first 2 s after finger lands
const float         MIN_IBI_MS       = 333;   // fastest accepted: 180 BPM
const float         MAX_IBI_MS       = 1500;  // slowest accepted: 40 BPM
const float         ENV_DECAY        = 0.995; // peak envelope decay per sample
const float         THRESH_FRACTION  = 0.50;  // threshold = 50% of recent peak height
const float         MIN_THRESHOLD    = 5.0;   // floor so flat noise isn't counted
const byte          BPM_AVG_N        = 4;     // average BPM over last 4 beats

// Each pulse has a second, smaller bump 300-450 ms after the real beat: the
// dicrotic notch, from the aortic valve closing. It clears a fixed 333 ms floor,
// so every heartbeat gets counted twice and BPM reads double. The floor is
// therefore scaled to the recent beat interval instead.
const float REFRACTORY_FRACTION = 0.60;  // next beat must be >= 60% of recent IBI

// A peak inside the refractory window is one of two things, told apart by height:
// a dicrotic notch, which is well below the beat it follows, or a real beat
// because the heart rate rose. Blocking on time alone would halve a rate that
// genuinely doubled, so height decides. The height is compared against the peak
// envelope, not an average of accepted peaks: once a notch is wrongly accepted,
// an average of accepted peaks sags toward the notches and stops telling them
// apart, while the envelope keeps tracking the true systolic height.
const float DICROTIC_MAX_RATIO = 0.85;   // at or below this x envelope = notch

// ---------- HRV settings ----------
const byte  HRV_N             = 30;    // RMSSD over the last 30 successive differences
const byte  HRV_MIN_DIFFS     = 10;    // need at least 10 before reporting RMSSD
const float ARTIFACT_FRACTION = 0.25;  // reject intervals >25% off the recent average
const byte  MAX_REJECTS       = 3;     // after 3 rejects in a row, accept and restart

// Set to true to print the filtered signal for Serial Plotter tuning
const bool PLOT_MODE = false;

// ---------- State ----------
bool fingerOn = false;
unsigned long sampleCount = 0;
unsigned int settleCount = 0;

float dc = 0;                    // baseline estimate
float lp = 0;                    // filtered pulse signal
float s0 = 0, s1 = 0, s2 = 0;    // last three filtered samples (s0 = newest)
float env = 0;                   // recent peak envelope

float lastBeatTime = -1;         // ms, sub-sample precision (-1 = none yet)
float ibis[BPM_AVG_N];           // recent inter-beat intervals (ms) for BPM
byte ibiIdx = 0;
byte ibiCount = 0;
float bpm = 0;

float sqDiffs[HRV_N];            // squared successive differences (ms^2)
byte sqIdx = 0;
byte sqCount = 0;
float prevValidIbi = 0;          // 0 = no valid previous beat to compare with
byte rejectCount = 0;
float rmssd = 0;

unsigned long lastStatusMs = 0;

// Throw away the running averages and the HRV window. Called when the beat
// pattern changes enough that the old intervals would poison the new ones.
void restartAverages() {
  ibiCount = 0;
  ibiIdx = 0;
  sqCount = 0;
  sqIdx = 0;
  prevValidIbi = 0;
  rejectCount = 0;
}

void resetState() {
  fingerOn = false;
  settleCount = 0;
  dc = 0;
  lp = 0;
  s0 = s1 = s2 = 0;
  env = 0;
  lastBeatTime = -1;
  ibiIdx = 0;
  ibiCount = 0;
  bpm = 0;
  sqIdx = 0;
  sqCount = 0;
  prevValidIbi = 0;
  rejectCount = 0;
  rmssd = 0;
}

// Fit a parabola through the three samples around a peak and return the
// offset of the true maximum from the middle sample, in samples (-0.5..0.5).
// This recovers timing finer than the 10 ms sample spacing.
float parabolicOffset(float left, float mid, float right) {
  float denom = left - 2.0 * mid + right;
  if (denom >= 0) return 0;      // not a proper peak shape: no correction
  float offset = 0.5 * (left - right) / denom;
  if (offset > 0.5) offset = 0.5;
  if (offset < -0.5) offset = -0.5;
  return offset;
}

void processSample(long ir) {
  sampleCount++;

  // --- Finger detection ---
  if (ir < FINGER_THRESHOLD) {
    if (fingerOn && !PLOT_MODE) Serial.println("Finger removed.");
    resetState();
    return;
  }
  if (!fingerOn) {
    fingerOn = true;
    dc = ir;  // start baseline at the current level to avoid a jump
    if (!PLOT_MODE) Serial.println("Finger detected. Hold still...");
  }

  // --- Filtering ---
  dc += DC_ALPHA * (ir - dc);   // slow baseline
  float ac = dc - ir;           // remove drift, invert so each beat is a positive peak
  lp += LP_ALPHA * (ac - lp);   // smooth

  s2 = s1;
  s1 = s0;
  s0 = lp;

  // --- Let filters settle after the finger lands ---
  if (settleCount < SETTLE_SAMPLES) {
    settleCount++;
    return;
  }

  // --- Adaptive threshold ---
  env *= ENV_DECAY;
  if (s0 > env) env = s0;
  float thresh = max(MIN_THRESHOLD, THRESH_FRACTION * env);

  if (PLOT_MODE) {
    Serial.print(lp, 1);
    Serial.print(",");
    Serial.println(thresh, 1);
  }

  // --- Peak detection: s1 is a local maximum above threshold ---
  bool isPeak = (s1 > s2) && (s1 >= s0) && (s1 > thresh);
  if (!isPeak) return;

  // Peak is at the previous sample, refined by parabolic interpolation
  float peakSample = (float)(sampleCount - 1) + parabolicOffset(s2, s1, s0);
  float peakTime = peakSample * SAMPLE_MS;

  if (lastBeatTime < 0) {                  // first beat: just record it
    lastBeatTime = peakTime;
    return;
  }

  float ibi = peakTime - lastBeatTime;

  if (ibi < MIN_IBI_MS) return;            // faster than 180 BPM: noise

  // --- Refractory period: rejects the dicrotic notch ---
  float minIbi = MIN_IBI_MS;
  if (ibiCount > 0) {
    float sum = 0;
    for (byte i = 0; i < ibiCount; i++) sum += ibis[i];
    float refractory = REFRACTORY_FRACTION * (sum / ibiCount);
    if (refractory > minIbi) minIbi = refractory;
  }
  if (ibi < minIbi) {
    if (s1 <= DICROTIC_MAX_RATIO * env) {
      return;   // too small for a beat: dicrotic notch. Keep lastBeatTime so the
                // next real beat is still measured from the last real one.
    }
    // Full height this soon means the rate itself rose. Take it, and drop the
    // old averages so the faster rate isn't fought by its own refractory floor.
    restartAverages();
  }
  lastBeatTime = peakTime;

  if (ibi > MAX_IBI_MS) {                  // gap too long (missed beat)
    prevValidIbi = 0;                      // break the HRV chain
    return;
  }

  // --- Artifact rejection ---
  if (ibiCount > 0) {
    float sum = 0;
    for (byte i = 0; i < ibiCount; i++) sum += ibis[i];
    float meanIbi = sum / ibiCount;

    if (fabs(ibi - meanIbi) > ARTIFACT_FRACTION * meanIbi) {
      rejectCount++;
      prevValidIbi = 0;                    // don't compare across a bad beat
      if (rejectCount < MAX_REJECTS) {
        if (!PLOT_MODE) {
          Serial.print("Rejected IBI: ");
          Serial.print(ibi, 0);
          Serial.println(" ms (artifact)");
        }
        return;
      }
      // Several in a row: heart rate really changed, so start the average over.
      // The HRV window is flushed too, or differences measured across the old
      // beat pattern would inflate RMSSD for the next 30 beats.
      restartAverages();
    } else {
      rejectCount = 0;
    }
  }

  // --- Store interval and average BPM ---
  ibis[ibiIdx] = ibi;
  ibiIdx = (ibiIdx + 1) % BPM_AVG_N;
  if (ibiCount < BPM_AVG_N) ibiCount++;

  float bpmSum = 0;
  for (byte i = 0; i < ibiCount; i++) bpmSum += ibis[i];
  bpm = 60000.0 / (bpmSum / ibiCount);

  // --- HRV: successive difference between back-to-back valid beats ---
  if (prevValidIbi > 0) {
    float d = ibi - prevValidIbi;
    sqDiffs[sqIdx] = d * d;
    sqIdx = (sqIdx + 1) % HRV_N;
    if (sqCount < HRV_N) sqCount++;
  }
  prevValidIbi = ibi;

  if (sqCount >= HRV_MIN_DIFFS) {
    float sqSum = 0;
    for (byte i = 0; i < sqCount; i++) sqSum += sqDiffs[i];
    rmssd = sqrt(sqSum / sqCount);
  }

  if (!PLOT_MODE) {
    Serial.print("Beat  IBI: ");
    Serial.print(ibi, 1);
    Serial.print(" ms  BPM: ");
    Serial.print(bpm, 1);
    Serial.print("  RMSSD: ");
    if (sqCount >= HRV_MIN_DIFFS) {
      Serial.print(rmssd, 1);
      Serial.println(" ms");
    } else {
      Serial.print("collecting (");
      Serial.print(sqCount);
      Serial.print("/");
      Serial.print(HRV_MIN_DIFFS);
      Serial.println(")");
    }
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);  // don't hang on battery

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX30102 not found. Check wiring.");
    while (1) delay(100);
  }

  sensor.setup(LED_POWER, SAMPLE_AVG, LED_MODE, SAMPLE_RATE, PULSE_WIDTH, ADC_RANGE);
  resetState();

  if (!PLOT_MODE) Serial.println("Ready. Place finger on sensor.");
}

void loop() {
  sensor.check();                    // pull new samples from the sensor FIFO
  while (sensor.available()) {
    long ir = sensor.getFIFOIR();
    sensor.nextSample();
    processSample(ir);
  }

  // Status line once per second when no beats are printing
  if (!PLOT_MODE && millis() - lastStatusMs >= 1000) {
    lastStatusMs = millis();
    if (!fingerOn) Serial.println("No finger.");
    else if (ibiCount == 0) Serial.println("Waiting for beats...");
  }
}
