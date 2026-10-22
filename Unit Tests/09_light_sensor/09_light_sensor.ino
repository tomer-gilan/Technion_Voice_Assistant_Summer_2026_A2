// Unit test 09 - light sensor (LDR) on an ADC1 pin
// Wiring: LDR and a 10 kOhm resistor in series between 3.3V and GND, their
// middle point to GPIO 34 (any ADC1 pin, 32-39 - ADC2 doesn't work while
// Wi-Fi is on). Prints the averaged reading (0-4095) twice a second, with
// the darkest and brightest seen so far.
//
// PASS: cover the sensor, then light it with a phone flashlight -
// "===== LIGHT SENSOR TEST PASSED =====" once the readings span at least
// kMinRangeToPass, and more light reads higher. If covering it makes the
// number go UP, swap the LDR and the resistor.

#define LIGHT_SENSOR_PIN 34  // the pin to set in ESP32/parameters.h too

namespace {
constexpr uint32_t kSerialBaudRate = 115200;
constexpr int kSamplesPerReading = 8;   // averaged, as light_sensor_utils.cpp
constexpr uint32_t kReportIntervalMs = 500;
constexpr int kMinRangeToPass = 1500;   // dark vs flashlight, out of 4095

int darkest = 4095;
int brightest = 0;
bool verdict_printed = false;

/** One averaged ADC reading, 0-4095. */
int readLight() {
  long sum = 0;
  for (int i = 0; i < kSamplesPerReading; i++) {
    sum += analogRead(LIGHT_SENSOR_PIN);
  }
  return static_cast<int>(sum / kSamplesPerReading);
}
}  // namespace

/** Sets the ADC up. */
void setup() {
  Serial.begin(kSerialBaudRate);
  delay(500);
  analogReadResolution(12);
  analogSetPinAttenuation(LIGHT_SENSOR_PIN, ADC_11db);  // the full 0-3.3 V range
  Serial.println("Unit test 09 - light sensor. Cover it, then shine a flashlight on it.");
}

/** Reads and reports twice a second. */
void loop() {
  int value = readLight();
  darkest = min(darkest, value);
  brightest = max(brightest, value);
  Serial.printf("Light: %4d   (darkest %4d, brightest %4d)\n", value, darkest, brightest);
  if (!verdict_printed && brightest - darkest >= kMinRangeToPass) {
    verdict_printed = true;
    Serial.println("===== LIGHT SENSOR TEST PASSED ===== (check: covering it read LOWER - if not, swap the LDR "
                   "and the resistor)");
  }
  delay(kReportIntervalMs);
}
