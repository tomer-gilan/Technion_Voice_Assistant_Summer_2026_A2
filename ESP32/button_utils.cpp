#include "button_utils.h"

#include <Arduino.h>
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
unsigned long last_sample_ms = 0;
int debounce_count = 0;  // 0 = surely released ... kButtonDebounceSamples = surely pressed
bool is_pressed = false;

/** Sets the pin up the first time the button is switched on. */
void preparePin() {
  if (pin_ready) {
    return;
  }
  pinMode(TALK_BUTTON_PIN, INPUT_PULLUP);
  pin_ready = true;
  debounce_count = 0;
  is_pressed = false;
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
  if (!button_enabled || millis() - last_sample_ms < esp32va_params::kButtonSampleMs) {
    return false;
  }
  last_sample_ms = millis();
  if (digitalRead(TALK_BUTTON_PIN) == kPressedLevel) {
    debounce_count = min(debounce_count + 1, esp32va_params::kButtonDebounceSamples);
  } else {
    debounce_count = max(debounce_count - 1, 0);
  }
  if (!is_pressed && debounce_count == esp32va_params::kButtonDebounceSamples) {
    is_pressed = true;
    LOG_DEBUG("Talk button pressed.\n");
    return true;
  }
  if (is_pressed && debounce_count == 0) {
    is_pressed = false;
    LOG_DEBUG("Talk button released.\n");
  }
  return false;
}

}  // namespace esp32va_button
