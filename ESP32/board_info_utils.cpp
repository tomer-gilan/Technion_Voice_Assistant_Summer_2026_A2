#include "board_info_utils.h"

#include <Arduino.h>
#include <WiFi.h>
#include "heap_diagnostics_utils.h"
#include "light_sensor_utils.h"
#include "log_utils.h"
#include "parameters.h"
#include "recording_folder_utils.h"
#include "status_report_utils.h"
#include "vad_utils.h"
#include "wifi_utils.h"

namespace esp32va_info {

namespace {

constexpr size_t kToolResponseBufferSize = 320;  // the JSON around it, a ~40-char call_id and the ~90-char output
char tool_response_message[kToolResponseBufferSize];

/** The same bands as the app's Wi-Fi bars. */
const char* signalWord(long rssi) {
  return rssi >= -55 ? "strong" : rssi >= -67 ? "good" : rssi >= -75 ? "fair" : "weak";
}

}  // namespace

bool handleGetBoardStatusCall(JsonDocument& doc, WebSocketsClient& webSocket) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }
  char output[128];
  size_t used = 0;
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    used = snprintf(output, sizeof(output), "Wi-Fi signal is %s (%ld dBm), ", signalWord(rssi), rssi);
  } else {
    used = snprintf(output, sizeof(output), "Wi-Fi is not connected, ");
  }
  unsigned long minutes = millis() / 60000UL;
  if (minutes == 0) {
    snprintf(output + used, sizeof(output) - used, "up for less than a minute, %lu KB of memory free.",
             (unsigned long)(esp32va_heap::freeHeapBytes() / 1024));
  } else {
    snprintf(output + used, sizeof(output) - used, "up for %lu minute%s, %lu KB of memory free.", minutes,
             minutes == 1 ? "" : "s", (unsigned long)(esp32va_heap::freeHeapBytes() / 1024));
  }
  LOG_INFO("Board status asked - %s\n", output);
  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"%s"}})JSON",
           call_id, output);
  webSocket.sendTXT(tool_response_message);
  LOG_DEBUG("Sent function_call_output.\n");
  return true;
}

void announceLimits() {
  esp32va_status::printProtocolLine(
      "@@LIMITS max_ask_bytes=%u max_ssid_bytes=%u min_password_bytes=%u max_password_bytes=%u "
      "max_kept_recordings=%u mic_gain=%d light_max=%d max_utterance_ms=%u vad_sensitivity_min=%u "
      "vad_sensitivity_max=%u vad_hangover_min_ms=%u vad_hangover_max_ms=%u",
      (unsigned)esp32va_params::kMaxTypedQuestionBytes, (unsigned)esp32va_wifi::kMaxSsidLength,
      (unsigned)esp32va_wifi::kMinPasswordLength, (unsigned)esp32va_wifi::kMaxPasswordLength,
      (unsigned)esp32va_recordings::kMaxKeptRecordings, (int)esp32va_params::kMicSoftwareGain,
      (int)esp32va_light_sensor::kMaxLightSensorValue, (unsigned)esp32va_vad::kMaxUtteranceMs,
      (unsigned)esp32va_vad::kMinSensitivity, (unsigned)esp32va_vad::kMaxSensitivity,
      (unsigned)esp32va_vad::kMinHangoverMs, (unsigned)esp32va_vad::kMaxHangoverMs);
}

}  // namespace esp32va_info
