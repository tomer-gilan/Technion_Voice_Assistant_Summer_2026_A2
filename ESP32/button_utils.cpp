#include "button_utils.h"

#include <Arduino.h>
#include <atomic>
#include <esp_timer.h>
#include <string.h>
#include <strings.h>
#include "log_utils.h"
#include "parameters.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_button {

namespace {
const char kEnabledSettingKey[] = "button_on";
constexpr int kPressedLevel = LOW;  // the BOOT button pulls GPIO0 to ground

bool button_enabled = false;
bool pin_ready = false;
// Sampled by a periodic esp_timer, not once per loop() pass: a pass can take
// ~40 ms during playback, which would stretch the debounce to ~240 ms and
// miss a quick tap.
esp_timer_handle_t sample_timer = nullptr;
volatile int debounce_count = 0;  // 0 = surely released ... kButtonDebounceSamples = surely pressed
volatile bool is_pressed = false;
std::atomic<bool> press_pending{false};  // set by the timer on a debounced press, taken by takeButtonPress()

/** The sample timer's callback (esp_timer task, every kButtonSampleMs):
    nudges the saturating counter by one toward the pin's level and flags a
    press when it saturates on "pressed". */
void sampleButton(void* /*unused*/) {
  if (digitalRead(TALK_BUTTON_PIN) == kPressedLevel) {
    debounce_count = min(debounce_count + 1, esp32va_params::kButtonDebounceSamples);
  } else {
    debounce_count = max(debounce_count - 1, 0);
  }
  if (!is_pressed && debounce_count == esp32va_params::kButtonDebounceSamples) {
    is_pressed = true;
    press_pending = true;
  } else if (is_pressed && debounce_count == 0) {
    is_pressed = false;
  }
}

/** Stops sampling the button (fine if it never started). */
void stopSampling() {
  if (sample_timer != nullptr) {
    esp_timer_stop(sample_timer);  // an error only means it wasn't running
  }
}

/** Sets the pin up the first time the button is switched on, and starts
    the debounce over every time - a press already held then (or one left
    over from before it was switched off) doesn't count - then samples it
    every kButtonSampleMs. */
void preparePin() {
  if (!pin_ready) {
    pinMode(TALK_BUTTON_PIN, INPUT_PULLUP);
    pin_ready = true;
  }
  if (sample_timer == nullptr) {
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &sampleButton;
    timer_args.name = "talk_button";
    esp_err_t result = esp_timer_create(&timer_args, &sample_timer);
    if (result != ESP_OK) {
      sample_timer = nullptr;
      LOG_ERROR("Talk button timer couldn't be created (error %d) - the button won't work.\n", (int)result);
      return;
    }
  }
  stopSampling();
  bool held_now = digitalRead(TALK_BUTTON_PIN) == kPressedLevel;
  debounce_count = held_now ? esp32va_params::kButtonDebounceSamples : 0;
  is_pressed = held_now;
  press_pending = false;
  uint64_t period_us = static_cast<uint64_t>(esp32va_params::kButtonSampleMs) * 1000;
  esp_err_t result = esp_timer_start_periodic(sample_timer, period_us);
  if (result != ESP_OK) {
    LOG_ERROR("Talk button timer couldn't start (error %d) - the button won't work.\n", (int)result);
  }
}
}  // namespace

void setupTalkButton() {
  button_enabled = esp32va_settings::loadSetting(kEnabledSettingKey, 0) != 0;
  if (button_enabled) {
    preparePin();
    LOG_INFO("Talk button (BOOT) is on.\n");
  }
}

bool handleButtonCommand(const char* command) {
  const size_t word_length = strlen("BUTTON");
  if (strncasecmp(command, "BUTTON", word_length) != 0 ||
      (command[word_length] != '\0' && command[word_length] != ' ')) {
    return false;
  }
  const char* argument = command + word_length;
  while (*argument == ' ') {
    argument++;
  }
  if (strcasecmp(argument, "on") == 0 || strcasecmp(argument, "off") == 0) {
    button_enabled = strcasecmp(argument, "on") == 0;
    if (button_enabled) {
      preparePin();
    } else {
      stopSampling();
    }
    esp32va_settings::saveSetting(kEnabledSettingKey, button_enabled ? 1 : 0);
    LOG_INFO("Talk button (BOOT) turned %s.\n", button_enabled ? "on" : "off");
  } else if (*argument != '\0') {
    Serial.printf("BUTTON ignored - expected on or off, got \"%s\".\n", argument);
  }
  announceButtonState();
  return true;
}

void announceButtonState() {
  esp32va_status::printProtocolLine("@@BUTTON %s", button_enabled ? "on" : "off");
}

bool takeButtonPress() {
  if (!button_enabled || !press_pending.exchange(false)) {
    return false;
  }
  LOG_DEBUG("Talk button pressed.\n");
  return true;
}

}  // namespace esp32va_button
