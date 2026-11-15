/*
 * wrist_test.ino
 * Finds the best wrist placement and LED power for the MAX30102.
 *
 * Same filters, interval checks, BPM and RMSSD as hr_hrv_v1, plus:
 *   - A wrist detector tuned for small pulses (set WRIST_DETECTOR = false for v1 behavior)
 *   - LED power changeable from the Serial Monitor (type 1-4, press Enter)
 *   - Red LED turned off (only IR is used), which saves power
 *   - One status line per second with signal-quality numbers
 *
 * Hardware: Seeed XIAO nRF52840 Sense + MAX30102 (I2C: SDA=D4, SCL=D5, VIN=3V3)
 * Library:  SparkFun MAX3010x Pulse and Proximity Sensor Library
 *
 * Status line:
 *   LED 0x3F (12.6 mA) | DC 41230 | AC 85 | PI 0.21% | beats 18/20 | BPM 63.2 | RMSSD 31.0 | GOOD
 *
 *   DC     average raw IR level (how much light comes back)
 *   AC     peak-to-peak size of the filtered pulse over the last 2 s
 *   PI     perfusion index = AC / DC x 100. Higher = stronger pulse. Compare spots with this.
 *   beats  of the last 20 detected peaks, how many gave a believable interval
 *   label  GOOD / FAIR / POOR, plus NO SKIN, SATURATED, SETTLING, COLLECTING
 */

#include <Wire.h>
#include "MAX30105.h"

MAX30105 sensor;

// ---------- Sensor configuration ----------
const byte LED_LEVELS[] = {0x1F, 0x3F, 0x7F, 0xBF};  // keys 1-4: ~6, 12.6, 25, 38 mA
const byte NUM_LEVELS  = 4;
byte levelIdx = 1;                                   // start at 0x3F for the wrist

const byte SAMPLE_AVG  = 4;     // average 4 raw samples per output sample
const byte LED_MODE    = 2;     // Red + IR slots (red is switched off below)
const int  SAMPLE_RATE = 400;   // 400 Hz / 4 averaging = 100 samples/s
const int  PULSE_WIDTH = 411;   // us, 18-bit resolution
const int  ADC_RANGE   = 4096;

const float SAMPLE_MS        = 10.0;
const unsigned int REPORT_N  = 100;     // status line every 100 samples (1 s)
const long  SATURATION       = 250000;  // 18-bit ADC tops out at 262143

// ---------- Filter settings ----------
const float DC_ALPHA = 0.01;
const float LP_ALPHA = 0.2;

// ---------- Beat detection settings ----------
const long          SKIN_THRESHOLD   = 5000;  // raw IR above this = on skin (no contact ~500)
const unsigned int  SETTLE_SAMPLES   = 200;   // ignore first 2 s after contact
const float         MIN_IBI_MS       = 333;   // 180 BPM
const float         MAX_IBI_MS       = 1500;  // 40 BPM
const float         ENV_DECAY        = 0.995;
const float         THRESH_FRACTION  = 0.35;
const float         MIN_THRESHOLD    = 5.0;
const byte          BPM_AVG_N        = 4;

// true:  wrist detector. Two changes from v1, both aimed at small pulses:
//        1. A second, faster baseline removal (~0.6 Hz) for detection, so breathing
//           wander can't push weak pulses below the threshold
//        2. Hysteresis: the beat is the highest point between the signal rising above
//           the threshold and falling below half of it, so noise wiggles on top of a
//           pulse can't produce extra peaks
// false: exact hr_hrv_v1 behavior
const bool  WRIST_DETECTOR = true;
const float BASE_ALPHA     = 0.04;  // detection baseline (~0.6 Hz)
const float EXIT_FRACTION  = 0.5;   // pulse ends below 50% of threshold

// Each pulse has a second, smaller bump 300-450 ms after the real beat: the
// dicrotic notch, from the aortic valve closing. It clears the fixed 333 ms
// floor, so every heartbeat is counted twice and BPM reads double. The floor is
// scaled to the recent beat interval, and a peak arriving inside that window is
// told from a real early beat by its height against the envelope.
const float REFRACTORY_FRACTION = 0.60;  // next beat must be >= 60% of recent IBI
const float DICROTIC_MAX_RATIO  = 0.85;  // at or below this x envelope = notch

// ---------- HRV settings ----------
const byte  HRV_N             = 30;
const byte  HRV_MIN_DIFFS     = 10;
const float ARTIFACT_FRACTION = 0.25;
const byte  MAX_REJECTS       = 3;

// ---------- Quality settings (rules of thumb, tune from real data) ----------
const byte  HIST_N     = 20;     // judge the last 20 peaks
const byte  MIN_HIST   = 8;      // need 8 before giving a label
const float GOOD_RATE  = 0.85;   // >= 85% believable peaks
const float FAIR_RATE  = 0.60;
const float MIN_PI_PCT = 0.03;   // below this the pulse is too small to trust
const unsigned int NO_BEAT_SAMPLES = 1000;  // 10 s settled with too few peaks = POOR

// Set true to stream the filtered signal for the Serial Plotter instead of status lines
const bool PLOT_MODE = false;

// ---------- Beat state ----------
bool skinOn = false;
unsigned long sampleCount = 0;
unsigned int settleCount = 0;

float dc = 0, lp = 0;
float lpBase = 0;                // detection baseline (wrist detector)
float s0 = 0, s1 = 0, s2 = 0;
float env = 0;
unsigned int settledSamples = 0; // samples since the filters settled

bool armed = false;              // pulse is above threshold (hysteresis detector)
float candVal = 0;               // highest local max seen in this pulse
float candTime = -1;             // its refined time (ms), -1 = none

float lastBeatTime = -1;
float ibis[BPM_AVG_N];
byte ibiIdx = 0, ibiCount = 0;
float bpm = 0;

float sqDiffs[HRV_N];
byte sqIdx = 0, sqCount = 0;
float prevValidIbi = 0;
byte rejectCount = 0;
float rmssd = 0;

unsigned long lastGoodSample = 0; // when the last believable beat arrived
const unsigned int STALE_SAMPLES = 500;  // show BPM/RMSSD as "--" if none in 5 s

byte hist[HIST_N];               // 1 = believable peak, 0 = thrown away
byte histIdx = 0, histCount = 0;

// ---------- Report window state ----------
float irSum = 0;
unsigned int windowN = 0;
long irMaxWin = 0;
float curMin = 1e9, curMax = -1e9;    // filtered-pulse range, this second
float prevMin = 1e9, prevMax = -1e9;  // and the second before

void resetState() {
  skinOn = false;
  settleCount = 0;
  dc = lp = 0;
  lpBase = 0;
  s0 = s1 = s2 = 0;
  env = 0;
  settledSamples = 0;
  armed = false;
  candTime = -1;
  lastBeatTime = -1;
  ibiIdx = ibiCount = 0;
  bpm = 0;
  sqIdx = sqCount = 0;
  prevValidIbi = 0;
  rejectCount = 0;
  rmssd = 0;
  histIdx = histCount = 0;
}

void resetWindow() {
  irSum = 0;
  windowN = 0;
  irMaxWin = 0;
  curMin = prevMin = 1e9;
  curMax = prevMax = -1e9;
}

bool settled() {
  return skinOn && settleCount >= SETTLE_SAMPLES;
}

void recordPeak(byte ok) {
  hist[histIdx] = ok;
  histIdx = (histIdx + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
}

float parabolicOffset(float left, float mid, float right) {
  float denom = left - 2.0 * mid + right;
  if (denom >= 0) return 0;
  float offset = 0.5 * (left - right) / denom;
  if (offset > 0.5) offset = 0.5;
  if (offset < -0.5) offset = -0.5;
  return offset;
}

void onBeat(float peakTime, float peakVal);

// Filtering and peak picking (same filters as hr_hrv_v1)
void handleSample(long ir) {
  if (ir < SKIN_THRESHOLD) {
    resetState();
    return;
  }
  if (!skinOn) {
    skinOn = true;
    dc = ir;
  }

  dc += DC_ALPHA * (ir - dc);
  float ac = dc - ir;
  lp += LP_ALPHA * (ac - lp);
  lpBase += BASE_ALPHA * (lp - lpBase);
  float det = WRIST_DETECTOR ? lp - lpBase : lp;   // signal the peak picker works on

  s2 = s1;
  s1 = s0;
  s0 = det;

  if (settleCount < SETTLE_SAMPLES) {
    settleCount++;
    return;
  }

  env *= ENV_DECAY;
  if (s0 > env) env = s0;
  float thresh = max(MIN_THRESHOLD, THRESH_FRACTION * env);

  if (PLOT_MODE) {
    Serial.print(s0, 1);
    Serial.print(",");
    Serial.println(thresh, 1);
  }

  if (settledSamples < 65535) settledSamples++;

  bool localMax = (s1 > s2) && (s1 >= s0);
  float refined = ((float)(sampleCount - 1) + parabolicOffset(s2, s1, s0)) * SAMPLE_MS;

  if (!WRIST_DETECTOR) {
    if (localMax && s1 > thresh) onBeat(refined, s1);
    return;
  }

  if (!armed && s0 > thresh) {           // pulse starts
    armed = true;
    candTime = -1;
  }
  if (armed) {
    if (localMax && s1 > thresh && (candTime < 0 || s1 > candVal)) {
      candVal = s1;
      candTime = refined;
    }
    if (s0 < EXIT_FRACTION * thresh) {   // pulse over: its highest point is the beat
      armed = false;
      if (candTime >= 0) onBeat(candTime, candVal);
    }
  }
}

// Interval checks, BPM and RMSSD for one detected beat at peakTime (ms),
// whose height is peakVal
void onBeat(float peakTime, float peakVal) {
  if (lastBeatTime < 0) {
    lastBeatTime = peakTime;
    return;
  }

  float ibi = peakTime - lastBeatTime;
  if (ibi < MIN_IBI_MS) {          // extra peak inside a beat: noise
    recordPeak(0);
    return;
  }

  // --- Refractory period: rejects the dicrotic notch ---
  float minIbi = MIN_IBI_MS;
  if (ibiCount > 0) {
    float sum = 0;
    for (byte i = 0; i < ibiCount; i++) sum += ibis[i];
    float refractory = REFRACTORY_FRACTION * (sum / ibiCount);
    if (refractory > minIbi) minIbi = refractory;
  }
  if (ibi < minIbi) {
    if (peakVal <= DICROTIC_MAX_RATIO * env) {
      // Dicrotic notch. Keep lastBeatTime so the next real beat is still
      // measured from the last real one. Deliberately not recorded in the
      // quality history: correctly skipping a notch is the detector working,
      // not a bad beat, and counting it would drag the label down to POOR.
      return;
    }
    // Full height this soon means the rate itself rose: drop the old averages
    // so the faster rate isn't fought by its own refractory floor.
    ibiCount = 0;
    ibiIdx = 0;
    sqCount = 0;
    sqIdx = 0;
    prevValidIbi = 0;
    rejectCount = 0;
  }
  lastBeatTime = peakTime;

  if (ibi > MAX_IBI_MS) {          // missed beat
    prevValidIbi = 0;
    recordPeak(0);
    return;
  }

  if (ibiCount > 0) {
    float sum = 0;
    for (byte i = 0; i < ibiCount; i++) sum += ibis[i];
    float meanIbi = sum / ibiCount;

    if (fabs(ibi - meanIbi) > ARTIFACT_FRACTION * meanIbi) {
      rejectCount++;
      prevValidIbi = 0;
      if (rejectCount < MAX_REJECTS) {
        recordPeak(0);
        return;
      }
      // The HRV window is flushed too, or differences measured across the old
      // beat pattern would inflate RMSSD for the next 30 beats.
      ibiCount = 0;
      ibiIdx = 0;
      sqCount = 0;
      sqIdx = 0;
      rejectCount = 0;
    } else {
      rejectCount = 0;
    }
  }
  recordPeak(1);
  lastGoodSample = sampleCount;

  ibis[ibiIdx] = ibi;
  ibiIdx = (ibiIdx + 1) % BPM_AVG_N;
  if (ibiCount < BPM_AVG_N) ibiCount++;

  float bpmSum = 0;
  for (byte i = 0; i < ibiCount; i++) bpmSum += ibis[i];
  bpm = 60000.0 / (bpmSum / ibiCount);

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
}

void printLevel() {
  Serial.print("LED 0x");
  Serial.print(LED_LEVELS[levelIdx], HEX);
  Serial.print(" (");
  Serial.print(LED_LEVELS[levelIdx] * 0.2, 1);
  Serial.print(" mA)");
}

void report() {
  float dcMean = irSum / windowN;
  float lo = fminf(prevMin, curMin);
  float hi = fmaxf(prevMax, curMax);
  float acPP = (hi > lo) ? hi - lo : 0;
  float pi = (dcMean > 0) ? 100.0 * acPP / dcMean : 0;

  byte good = 0;
  for (byte i = 0; i < histCount; i++) good += hist[i];

  const char* label;
  const char* hint = "";
  if (dcMean < SKIN_THRESHOLD) {
    label = "NO SKIN";
  } else if (irMaxWin >= SATURATION) {
    label = "SATURATED";
    hint = "  <- lower the LED power";
  } else if (!settled()) {
    label = "SETTLING";
  } else if (histCount < MIN_HIST && settledSamples < NO_BEAT_SAMPLES) {
    label = "COLLECTING";
  } else if (histCount < MIN_HIST) {
    label = "POOR";
    hint = "  <- hardly any beats found: try another spot or a higher LED level";
  } else {
    float rate = (float)good / histCount;
    if (rate >= GOOD_RATE && pi >= MIN_PI_PCT) label = "GOOD";
    else if (rate >= FAIR_RATE && pi >= MIN_PI_PCT) label = "FAIR";
    else {
      label = "POOR";
      hint = "  <- try another spot, a snugger strap, or another LED level";
    }
  }

  printLevel();
  Serial.print(" | DC ");
  Serial.print(dcMean, 0);
  Serial.print(" | AC ");
  Serial.print(acPP, 0);
  Serial.print(" | PI ");
  Serial.print(pi, 2);
  Serial.print("% | beats ");
  Serial.print(good);
  Serial.print("/");
  Serial.print(histCount);
  bool fresh = ibiCount > 0 && sampleCount - lastGoodSample < STALE_SAMPLES;
  Serial.print(" | BPM ");
  if (fresh) Serial.print(bpm, 1); else Serial.print("--");
  Serial.print(" | RMSSD ");
  if (fresh && sqCount >= HRV_MIN_DIFFS) Serial.print(rmssd, 1); else Serial.print("--");
  Serial.print(" | ");
  Serial.print(label);
  Serial.println(hint);
}

void processSample(long ir) {
  sampleCount++;
  handleSample(ir);

  irSum += ir;
  windowN++;
  if (ir > irMaxWin) irMaxWin = ir;
  if (settled()) {
    if (s0 < curMin) curMin = s0;
    if (s0 > curMax) curMax = s0;
  }

  if (windowN >= REPORT_N) {
    if (!PLOT_MODE) report();
    prevMin = curMin;
    prevMax = curMax;
    curMin = 1e9;
    curMax = -1e9;
    irSum = 0;
    windowN = 0;
    irMaxWin = 0;
  }
}

void applyLevel() {
  sensor.setPulseAmplitudeIR(LED_LEVELS[levelIdx]);
  sensor.setPulseAmplitudeRed(0);   // red isn't used for heart rate
  resetState();
  resetWindow();
  if (!PLOT_MODE) {
    Serial.print("Switched to ");
    printLevel();
    Serial.println(". Hold still while it settles.");
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX30102 not found. Check wiring.");
    while (1) delay(100);
  }

  sensor.setup(LED_LEVELS[levelIdx], SAMPLE_AVG, LED_MODE, SAMPLE_RATE, PULSE_WIDTH, ADC_RANGE);
  sensor.setPulseAmplitudeRed(0);
  resetState();
  resetWindow();

  if (!PLOT_MODE) {
    Serial.println("Wrist test. Type 1-4 and press Enter to change LED power:");
    Serial.println("  1 = 0x1F (~6 mA)   2 = 0x3F (~12.6 mA)   3 = 0x7F (~25 mA)   4 = 0xBF (~38 mA)");
  }
}

void loop() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c >= '1' && c < '1' + NUM_LEVELS) {
      levelIdx = c - '1';
      applyLevel();
    }
  }

  sensor.check();
  while (sensor.available()) {
    long ir = sensor.getFIFOIR();
    sensor.nextSample();
    processSample(ir);
  }
}
