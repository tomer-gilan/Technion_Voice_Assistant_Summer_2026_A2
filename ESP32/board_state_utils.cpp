  #include "board_state_utils.h"

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <math.h>
#include <string.h>
#include "parameters.h"
#include "device_controls_utils.h"
#include "log_utils.h"
#include "status_report_utils.h"

namespace {
constexpr uint32_t kToolResponseBufferSize = 256;  // function_call_output messages are tiny - no audio involved
char tool_response_message[kToolResponseBufferSize];

// The LED is dimmed with PWM (the ESP32's LEDC peripheral). 5 kHz is far
// above visible flicker; 8 bits gives 256 steps.
constexpr uint32_t kLedPwmFrequencyHz = 5000;
constexpr uint8_t kLedPwmResolutionBits = 8;
constexpr uint32_t kLedPwmMaxDuty = (1u << kLedPwmResolutionBits) - 1;
#if ESP_ARDUINO_VERSION_MAJOR < 3
constexpr uint8_t kLedPwmChannel = 0;  // core 2.x addresses LEDC by channel, 3.x by pin
#endif

bool led_on = false;
uint8_t led_brightness_percent = 100;  // remembered while off, so "on" brings back the last brightness

/** Drives the LED's PWM duty from led_on / led_brightness_percent. Square
    curve, so the slider's steps look even (the eye is far more sensitive at
    the dark end). */
void applyLedOutput() {
  uint32_t percent = led_on ? led_brightness_percent : 0;
  uint32_t duty = percent * percent * kLedPwmMaxDuty / (100 * 100);
  if (!LED_ACTIVE_HIGH) {
    duty = kLedPwmMaxDuty - duty;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LED_PIN, duty);
#else
  ledcWrite(kLedPwmChannel, duty);
#endif
}
}  // namespace

namespace esp32va_board {

void setupBoardState() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LED_PIN, kLedPwmFrequencyHz, kLedPwmResolutionBits);
#else
  ledcSetup(kLedPwmChannel, kLedPwmFrequencyHz, kLedPwmResolutionBits);
  ledcAttachPin(LED_PIN, kLedPwmChannel);
#endif
  setLed(false);
}

void setLed(bool on) {
  led_on = on;
  applyLedOutput();
}

void setLedBrightness(uint8_t percent) {
  led_brightness_percent = percent > 100 ? 100 : percent;
  applyLedOutput();
}

bool isLedOn() {
  return led_on;
}

uint8_t ledBrightnessPercent() {
  return led_brightness_percent;
}

void announceLedState(const char* source) {
  esp32va_status::printProtocolLine("@@LED %s %u %s", led_on ? "on" : "off", (unsigned)led_brightness_percent, source);
}


bool handleSetBoardStateCall(JsonDocument& doc, WebSocketsClient& webSocket) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  const char* name = doc["name"] | "(unknown)";
  const char* arguments_json = doc["arguments"] | "{}";

  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }

  if (strcmp(name, "set_board_state") != 0) {
    LOG_ERROR("Unhandled function call: %s\n", name);
    return false;
  }

  JsonDocument args_doc;
  DeserializationError err = deserializeJson(args_doc, arguments_json);
  JsonVariant led_on_value = args_doc["led_on"];
  if (err || !led_on_value.is<bool>()) {
    // Still answer the call, like the other tools - otherwise the model
    // waits on it and the user hears nothing.
    LOG_ERROR("set_board_state called without a valid led_on: %s\n", arguments_json);
    snprintf(tool_response_message, kToolResponseBufferSize,
             R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"Error: led_on must be true or false - the LED was not changed."}})JSON",
             call_id);
    webSocket.sendTXT(tool_response_message);
    return true;
  }
  bool led_on = led_on_value.as<bool>();
  JsonVariant brightness_value = args_doc["brightness_percent"];
  bool brightness_given = brightness_value.is<int>() || brightness_value.is<float>();
  if (brightness_given) {
    long brightness = constrain(lroundf(brightness_value.as<float>()), 0L, 100L);
    esp32va_controls::setAndSaveLedBrightness(static_cast<uint8_t>(brightness));
  }

  setLed(led_on);
  LOG_INFO("LED turned %s at %u%% (voice command).\n", led_on ? "on" : "off",
           (unsigned)esp32va_board::ledBrightnessPercent());
  esp32va_board::announceLedState("voice");

  char output[48];
  if (led_on && brightness_given) {
    snprintf(output, sizeof(output), "LED turned on at %u percent brightness",
             (unsigned)esp32va_board::ledBrightnessPercent());
  } else {
    snprintf(output, sizeof(output), "LED turned %s", led_on ? "on" : "off");
  }
  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"%s"}})JSON",
           call_id, output);
  webSocket.sendTXT(tool_response_message);
  LOG_DEBUG("Sent function_call_output.\n");
  return true;
}

}  // namespace esp32va_board
