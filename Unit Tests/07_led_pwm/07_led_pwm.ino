// Unit test 07 - onboard LED with PWM brightness
// Fades the onboard LED (GPIO 2) up and down three times, then holds it at
// 100%, 30% and off for 2 s each - the same LEDC setup and the same square
// brightness curve (duty = percent^2 / 100^2) the firmware uses for "dim the
// light to 30 percent".
//
// PASS (by eye): the LED fades smoothly, and 30% is visibly dimmer than
// 100%. The sketch prints "===== LED TEST DONE - PASS if ... =====" at the
// end, since only a person can see the LED.

// Pins - the same as ESP32/parameters.h (each unit test stands alone, so it
// carries its own copy).
#define LED_PIN 2
#define LED_ACTIVE_HIGH 1

namespace {
constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kPwmFrequencyHz = 5000;   // as board_state_utils.cpp
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmMaxDuty = (1u << kPwmResolutionBits) - 1;
constexpr uint8_t kPwmChannel = 0;           // core 2.x addresses LEDC by channel
constexpr int kFades = 3;
constexpr uint32_t kFadeStepMs = 8;
constexpr uint32_t kHoldMs = 2000;

/** Sets the LED to percent brightness (0 = off), honoring its polarity.
    Square curve as board_state_utils.cpp, so the steps look even to the eye. */
void setBrightness(uint32_t percent) {
  uint32_t duty = percent * percent * kPwmMaxDuty / (100 * 100);
  if (!LED_ACTIVE_HIGH) {
    duty = kPwmMaxDuty - duty;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LED_PIN, duty);
#else
  ledcWrite(kPwmChannel, duty);
#endif
}
}  // namespace

/** Sets up the PWM channel, then runs the fades and the three levels once. */
void setup() {
  Serial.begin(kSerialBaudRate);
  delay(500);
  Serial.println("Unit test 07 - LED. Watch the onboard (blue) LED.");
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LED_PIN, kPwmFrequencyHz, kPwmResolutionBits);
#else
  ledcSetup(kPwmChannel, kPwmFrequencyHz, kPwmResolutionBits);
  ledcAttachPin(LED_PIN, kPwmChannel);
#endif
  for (int fade = 1; fade <= kFades; fade++) {
    Serial.printf("Fade %d of %d...\n", fade, kFades);
    for (uint32_t percent = 0; percent <= 100; percent++) {
      setBrightness(percent);
      delay(kFadeStepMs);
    }
    for (int percent = 100; percent >= 0; percent--) {
      setBrightness(static_cast<uint32_t>(percent));
      delay(kFadeStepMs);
    }
  }
  const uint32_t kLevels[] = {100, 30, 0};
  for (uint32_t level : kLevels) {
    Serial.printf("Holding %u%%...\n", (unsigned)level);
    setBrightness(level);
    delay(kHoldMs);
  }
  Serial.println("===== LED TEST DONE - PASS if it faded smoothly and 30% looked dimmer than 100% =====");
}

/** Nothing to repeat - press Reset to run it again. */
void loop() {
}
