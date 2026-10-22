// Unit test 09 - light sensor (LDR) on an ADC1 pin
//
// STATUS: the LDR is NOT wired on the submitted board (per the instructor's
// guidance the firmware simulates the light value from the control center
// instead). This test is ready for when it is wired; without a sensor GPIO 34
// floats and the test will most likely print FAILED.
//
// Wiring: LDR and a 10 kOhm resistor in series between 3.3V and GND, their
// middle point to GPIO 34 (any ADC1 pin, 32-39 - ADC2 doesn't work while
// Wi-Fi is on).
//
// How it runs, guided by Serial: first kPhaseMs with the sensor COVERED, then
// kPhaseMs with a phone flashlight shining on it. Each phase's reading is the
// average of its last kAveragedWindowMs.
//
// PASS: "===== LIGHT SENSOR TEST PASSED =====" when the lit reading is at least
// kMinRangeToPass above the covered one. FAILED says whether the range was too
// small, or covering it read HIGHER (swap the LDR and the resistor).

#define LIGHT_SENSOR_PIN 34  // the pin to set in ESP32/parameters.h too

namespace {
constexpr uint32_t kSerialBaudRate = 115200;
constexpr int kSamplesPerReading = 8;   // averaged, as light_sensor_utils.cpp
constexpr uint32_t kReportIntervalMs = 500;
constexpr uint32_t kPhaseMs = 6000;            // time given to cover, then to light, the sensor
constexpr uint32_t kAveragedWindowMs = 3000;   // the end of each phase, once the hand/light has settled
constexpr int kMinRangeToPass = 1500;   // flashlight minus covered, out of 4095

enum class Phase { kCover, kLight, kDone };

Phase phase = Phase::kCover;
uint32_t phase_started_ms = 0;
long window_sum = 0;       // readings summed over the current phase's averaging window
int window_count = 0;
int covered_average = 0;

/** One averaged ADC reading, 0-4095. */
int readLight() {
  long sum = 0;
  for (int i = 0; i < kSamplesPerReading; i++) {
    sum += analogRead(LIGHT_SENSOR_PIN);
  }
  return static_cast<int>(sum / kSamplesPerReading);
}

/** Average of the readings collected in the current phase's window. */
int windowAverage() {
  return window_count > 0 ? static_cast<int>(window_sum / window_count) : 0;
}

/** Starts a phase: resets its averaging window and tells the user what to do. */
void startPhase(Phase next_phase, const char* instruction) {
  phase = next_phase;
  phase_started_ms = millis();
  window_sum = 0;
  window_count = 0;
  Serial.println(instruction);
}

/** Prints the PASSED/FAILED verdict from the two averaged readings. */
void reportVerdict(int covered, int lit) {
  Serial.printf("Covered average: %d, lit average: %d.\n", covered, lit);
  if (lit - covered >= kMinRangeToPass) {
    Serial.println("===== LIGHT SENSOR TEST PASSED =====");
  } else if (covered > lit) {
    Serial.println("===== LIGHT SENSOR TEST FAILED: covering it read HIGHER than light - swap the LDR and the resistor =====");
  } else {
    Serial.printf("===== LIGHT SENSOR TEST FAILED: lit minus covered is %d, needs at least %d (wiring? is the flashlight on the sensor?) =====\n",
                  lit - covered, kMinRangeToPass);
  }
}
}  // namespace

/** Sets the ADC up and starts the covered phase. */
void setup() {
  Serial.begin(kSerialBaudRate);
  delay(500);
  analogReadResolution(12);
  analogSetPinAttenuation(LIGHT_SENSOR_PIN, ADC_11db);  // the full 0-3.3 V range
  Serial.println("Unit test 09 - light sensor.");
  startPhase(Phase::kCover, "Phase 1: COVER the sensor with your finger now...");
}

/** Reads twice a second, moves between the two phases and reports the verdict. */
void loop() {
  if (phase == Phase::kDone) {
    return;
  }
  int value = readLight();
  uint32_t elapsed_ms = millis() - phase_started_ms;
  Serial.printf("Light: %4d\n", value);
  if (elapsed_ms >= kPhaseMs - kAveragedWindowMs) {
    window_sum += value;
    window_count++;
  }
  if (elapsed_ms >= kPhaseMs) {
    if (phase == Phase::kCover) {
      covered_average = windowAverage();
      startPhase(Phase::kLight, "Phase 2: shine a phone FLASHLIGHT on the sensor now...");
    } else {
      reportVerdict(covered_average, windowAverage());
      phase = Phase::kDone;
    }
  }
  delay(kReportIntervalMs);
}
