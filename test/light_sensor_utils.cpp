#include "light_sensor_utils.h"

#include <Arduino.h>
#include <stdlib.h>
#include <strings.h>
#include "config.h"
#include "log_utils.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_light_sensor {

int g_light_sensor_value = 4000;  // starts in the "day" band until told otherwise

namespace {

// Placeholder boundaries on the 0-6142 scale - recalibrate against real
// readings once the physical sensor is wired up. Since step 28 they can be
// changed at runtime (LIGHT_BANDS, e.g. from the laptop app) and are kept
// across reboots; these are the defaults. At or above day_min is day, at or
// above evening_min is evening, anything below is night.
constexpr int kDefaultEveningMin = 1000;
constexpr int kDefaultDayMin = 3000;
const char kEveningMinSettingKey[] = "light_eve";
const char kDayMinSettingKey[] = "light_day";
int evening_min_value = kDefaultEveningMin;
int day_min_value = kDefaultDayMin;

constexpr char kLightCommandWord[] = "LIGHT";
constexpr size_t kLightCommandWordLen = sizeof(kLightCommandWord) - 1;
constexpr char kLightBandsCommandWord[] = "LIGHT_BANDS";
constexpr size_t kLightBandsCommandWordLen = sizeof(kLightBandsCommandWord) - 1;

#ifdef LIGHT_SENSOR_PIN
constexpr uint32_t kSensorReadIntervalMs = 1000;
constexpr int kSamplesPerReading = 8;           // averaged - an ADC reading jitters by a few counts
constexpr int kAnnounceChange = 150;            // on the 0-6142 scale: smaller moves don't print @@LIGHT
uint32_t last_read_ms = 0;
int last_announced_value = -1;

/** One reading of the real sensor, on the same 0-6142 scale as LIGHT. */
int readLightSensor() {
  long sum = 0;
  for (int i = 0; i < kSamplesPerReading; i++) {
    sum += analogRead(LIGHT_SENSOR_PIN);
  }
  return static_cast<int>(sum / kSamplesPerReading) * 3 / 2;
}
#endif

const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
constexpr uint32_t kToolResponseBufferSize = 256;  // function_call_output messages are tiny - no audio involved
char tool_response_message[kToolResponseBufferSize];

/** Maps a light level to its part of the day ("day", "evening" or "night"). */
const char* partOfDayForLightLevel(int light_value) {
  if (light_value >= day_min_value) {
    return "day";
  }
  if (light_value >= evening_min_value) {
    return "evening";
  }
  return "night";
}

/** Handles "LIGHT_BANDS <evening min> <day min>": both whole numbers, 0 <=
    evening < day <= kMaxLightSensorValue. Saves them and announces @@LIGHT;
    anything else is logged and ignored. Bare "LIGHT_BANDS" just announces
    the current ones. */
void handleLightBandsArguments(const char* arguments) {
  while (*arguments == ' ') {
    arguments++;
  }
  if (*arguments == '\0') {
    announceLightState();
    return;
  }
  char* parse_end = nullptr;
  long evening_min = strtol(arguments, &parse_end, 10);
  char* second_start = parse_end;
  long day_min = strtol(second_start, &parse_end, 10);
  while (*parse_end == ' ') {
    parse_end++;
  }
  bool parsed = second_start != arguments && parse_end != second_start && *parse_end == '\0';
  if (!parsed || evening_min < 0 || day_min <= evening_min || day_min > kMaxLightSensorValue) {
    Serial.printf("LIGHT_BANDS ignored - expected two whole numbers, evening below day, 0 to %d (got \"%s\").\n",
                  kMaxLightSensorValue, arguments);
    announceLightState();  // so the app's fields snap back to what the board really uses
    return;
  }
  evening_min_value = static_cast<int>(evening_min);
  day_min_value = static_cast<int>(day_min);
  esp32va_settings::saveSetting(kEveningMinSettingKey, evening_min_value);
  esp32va_settings::saveSetting(kDayMinSettingKey, day_min_value);
  LOG_INFO("Light thresholds set: evening from %d, day from %d.\n", evening_min_value, day_min_value);
  announceLightState();
}

}  // namespace

void setupLightSensor() {
  int saved_evening_min = esp32va_settings::loadSetting(kEveningMinSettingKey, kDefaultEveningMin);
  int saved_day_min = esp32va_settings::loadSetting(kDayMinSettingKey, kDefaultDayMin);
  if (saved_evening_min >= 0 && saved_evening_min < saved_day_min && saved_day_min <= kMaxLightSensorValue) {
    evening_min_value = saved_evening_min;
    day_min_value = saved_day_min;
  } else {
    LOG_ERROR("Saved light thresholds %d/%d don't make sense - using the defaults.\n", saved_evening_min,
              saved_day_min);
  }
#ifdef LIGHT_SENSOR_PIN
  g_light_sensor_value = readLightSensor();
  last_announced_value = g_light_sensor_value;
  last_read_ms = millis();
  LOG_INFO("Light sensor on GPIO %d starts at %d (%s).\n", LIGHT_SENSOR_PIN, g_light_sensor_value,
           partOfDayForLightLevel(g_light_sensor_value));
#else
  LOG_INFO("Simulated light sensor starts at %d (%s) - type LIGHT <0-%d> to change it.\n",
           g_light_sensor_value, partOfDayForLightLevel(g_light_sensor_value), kMaxLightSensorValue);
#endif
}

void serviceLightSensor() {
#ifdef LIGHT_SENSOR_PIN
  if (millis() - last_read_ms < kSensorReadIntervalMs) {
    return;
  }
  last_read_ms = millis();
  g_light_sensor_value = readLightSensor();
  // Same string literal for the same band, so comparing the pointers is enough.
  bool band_changed = partOfDayForLightLevel(g_light_sensor_value) != partOfDayForLightLevel(last_announced_value);
  if (band_changed || abs(g_light_sensor_value - last_announced_value) > kAnnounceChange) {
    last_announced_value = g_light_sensor_value;
    announceLightState();
  }
#endif
}

void announceLightState() {
  esp32va_status::printProtocolLine("@@LIGHT %d %s %d %d", g_light_sensor_value,
                                    partOfDayForLightLevel(g_light_sensor_value), evening_min_value, day_min_value);
}

bool handleLightCommand(const char* command) {
  if (strncasecmp(command, kLightBandsCommandWord, kLightBandsCommandWordLen) == 0 &&
      (command[kLightBandsCommandWordLen] == ' ' || command[kLightBandsCommandWordLen] == '\0')) {
    handleLightBandsArguments(command + kLightBandsCommandWordLen);
    return true;
  }
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
    announceLightState();
    return true;
  }

#ifdef LIGHT_SENSOR_PIN
  LOG_INFO("LIGHT ignored - the real sensor on GPIO %d sets the light level.\n", LIGHT_SENSOR_PIN);
  announceLightState();
  return true;
#endif

  // strtol() alone can't tell "0" from "not a number" - parse_end catches the latter.
  char* parse_end = nullptr;
  long requested_value = strtol(value_text, &parse_end, 10);
  if (*parse_end != '\0' || requested_value < 0 || requested_value > kMaxLightSensorValue) {
    LOG_INFO("LIGHT ignored - expected a whole number from 0 to %d, got \"%s\".\n",
             kMaxLightSensorValue, value_text);
    announceLightState();
    return true;
  }

  int previous_value = g_light_sensor_value;
  g_light_sensor_value = static_cast<int>(requested_value);
  LOG_INFO("Light sensor set: %d (%s) -> %d (%s).\n",
           previous_value, partOfDayForLightLevel(previous_value),
           g_light_sensor_value, partOfDayForLightLevel(g_light_sensor_value));
  announceLightState();
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
