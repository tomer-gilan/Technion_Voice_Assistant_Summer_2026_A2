#include "board_state_utils.h"

#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "log_utils.h"

namespace {
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
constexpr uint32_t kToolResponseBufferSize = 256;  // function_call_output messages are tiny - no audio involved
char tool_response_message[kToolResponseBufferSize];
}  // namespace

void setupBoardState() {
  pinMode(LED_PIN, OUTPUT);
  setLed(false);
}

void setLed(bool on) {
  digitalWrite(LED_PIN, (on == LED_ACTIVE_HIGH) ? HIGH : LOW);
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
  if (err) {
    LOG_ERROR("set_board_state args parse failed: %s\n", err.c_str());
    LOG_DEBUG("Failed arguments string: %s\n", arguments_json);
    return false;
  }
  bool led_on = args_doc["led_on"] | false;

  setLed(led_on);
  LOG_INFO("LED turned %s (voice command).\n", led_on ? "on" : "off");

  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"LED turned %s"}})JSON",
           call_id, led_on ? "on" : "off");
  webSocket.sendTXT(tool_response_message);
  webSocket.sendTXT(kResponseCreateMessage);
  LOG_DEBUG("Sent function_call_output - asked the model for a spoken confirmation.\n");
  return true;
}
