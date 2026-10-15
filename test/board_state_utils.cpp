  #include "board_state_utils.h"

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <string.h>
#include "config.h"
#include "log_utils.h"
#include "status_report_utils.h"

namespace {
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
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

namespace esp32va_board {

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

}  // namespace esp32va_board

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
  if (err) {
    LOG_ERROR("set_board_state args parse failed: %s\n", err.c_str());
    LOG_DEBUG("Failed arguments string: %s\n", arguments_json);
    return false;
  }
  bool led_on = args_doc["led_on"] | false;

  setLed(led_on);
  LOG_INFO("LED turned %s (voice command).\n", led_on ? "on" : "off");
  esp32va_board::announceLedState("voice");

  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"LED turned %s"}})JSON",
           call_id, led_on ? "on" : "off");
  webSocket.sendTXT(tool_response_message);
  webSocket.sendTXT(kResponseCreateMessage);
  LOG_DEBUG("Sent function_call_output - asked the model for a spoken confirmation.\n");
  return true;
}
