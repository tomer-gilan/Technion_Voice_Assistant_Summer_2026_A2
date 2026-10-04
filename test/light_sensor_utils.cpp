#include "light_sensor_utils.h"

#include <Arduino.h>
#include <stdlib.h>
#include <strings.h>
#include "log_utils.h"

namespace esp32va_light_sensor {

int g_light_sensor_value = 4000;  // starts in the "day" band until told otherwise

namespace {

// Placeholder boundaries on the 0-6142 scale - recalibrate against real
// readings once the physical sensor is wired up. Checked top-down: the first
// band whose minimum the reading reaches wins.
struct PartOfDayBand {
  int min_light_value_inclusive;
  const char* name;
};
const PartOfDayBand kPartOfDayBands[] = {
    {3000, "day"},
    {1000, "evening"},
    {0, "night"},
};
constexpr size_t kNumPartOfDayBands = sizeof(kPartOfDayBands) / sizeof(kPartOfDayBands[0]);

constexpr char kLightCommandWord[] = "LIGHT";
constexpr size_t kLightCommandWordLen = sizeof(kLightCommandWord) - 1;

const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
constexpr uint32_t kToolResponseBufferSize = 256;  // function_call_output messages are tiny - no audio involved
char tool_response_message[kToolResponseBufferSize];

/** Maps a light level to its part of the day ("day", "evening" or "night"). */
const char* partOfDayForLightLevel(int light_value) {
  for (size_t i = 0; i < kNumPartOfDayBands; i++) {
    if (light_value >= kPartOfDayBands[i].min_light_value_inclusive) {
      return kPartOfDayBands[i].name;
    }
  }
  return kPartOfDayBands[kNumPartOfDayBands - 1].name;  // only reachable for a negative reading
}

}  // namespace

void setupLightSensor() {
  LOG_INFO("Simulated light sensor starts at %d (%s) - type LIGHT <0-%d> to change it.\n",
           g_light_sensor_value, partOfDayForLightLevel(g_light_sensor_value), kMaxLightSensorValue);
}

bool handleLightCommand(const char* command) {
  // "LIGHTS" or "LIGHT1800" aren't LIGHT commands - the word must stand alone.
  if (strncasecmp(command, kLightCommandWord, kLightCommandWordLen) != 0) {
    return false;
  }
  const char* value_text = command + kLightCommandWordLen;
  if (*value_text != '\0' && *value_text != ' ') {
    return false;
  }
  while (*value_text == ' ') {
    value_text++;
  }

  if (*value_text == '\0') {
    LOG_INFO("Light sensor reads %d (%s).\n", g_light_sensor_value, partOfDayForLightLevel(g_light_sensor_value));
    return true;
  }

  // strtol() alone can't tell "0" from "not a number" - parse_end catches the latter.
  char* parse_end = nullptr;
  long requested_value = strtol(value_text, &parse_end, 10);
  if (*parse_end != '\0' || requested_value < 0 || requested_value > kMaxLightSensorValue) {
    LOG_INFO("LIGHT ignored - expected a whole number from 0 to %d, got \"%s\".\n",
             kMaxLightSensorValue, value_text);
    return true;
  }

  int previous_value = g_light_sensor_value;
  g_light_sensor_value = static_cast<int>(requested_value);
  LOG_INFO("Light sensor set: %d (%s) -> %d (%s).\n",
           previous_value, partOfDayForLightLevel(previous_value),
           g_light_sensor_value, partOfDayForLightLevel(g_light_sensor_value));
  return true;
}

bool handleGetTimeOfDayCall(JsonDocument& doc, WebSocketsClient& webSocket) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }

  const char* part_of_day = partOfDayForLightLevel(g_light_sensor_value);
  LOG_INFO("Time of day asked - light sensor reads %d, so it's %s.\n", g_light_sensor_value, part_of_day);

  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"It is currently %s."}})JSON",
           call_id, part_of_day);
  webSocket.sendTXT(tool_response_message);
  webSocket.sendTXT(kResponseCreateMessage);
  LOG_DEBUG("Sent function_call_output (\"It is currently %s.\") - asked the model for a spoken answer.\n",
            part_of_day);
  return true;
}

}  // namespace esp32va_light_sensor
