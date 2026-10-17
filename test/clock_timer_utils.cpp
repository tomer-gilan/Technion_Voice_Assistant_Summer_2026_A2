#include "clock_timer_utils.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "board_state_utils.h"
#include "hands_free_utils.h"
#include "i2s_audio_utils.h"
#include "log_utils.h"
#include "status_report_utils.h"

namespace esp32va_clock {

namespace {

constexpr long kMaxTimerSeconds = 3600;
constexpr int kAlarmBeeps = 3;
constexpr uint32_t kAlarmGapMs = 150;       // LED off, silence, between two beeps
constexpr int kEarliestPlausibleYear = 2024;  // before this the clock was never synced
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
constexpr size_t kToolResponseBufferSize = 256;  // function_call_output messages are tiny - no audio involved
char tool_response_message[kToolResponseBufferSize];

constexpr size_t kMaxLabelLength = 48;  // "check the oven" - a few words, said back out loud
constexpr size_t kAnnouncementBufferSize = kMaxLabelLength + 160;  // template text + JSON punctuation, generous margin
// The announcement's text goes inside a JSON string: setTimerLabel() keeps
// only characters that need no escaping.
constexpr char kAnnouncementTemplate[] =
    R"JSON({"type":"response.create","response":{"instructions":"Say exactly and only the following, with no other words or tool calls: \"%s\""}})JSON";

bool timer_armed = false;
uint32_t timer_started_ms = 0;
uint32_t timer_length_ms = 0;
char timer_label[kMaxLabelLength + 1] = "";  // "" when the timer has no label
char announcement_json[kAnnouncementBufferSize];

/** Keeps a short, JSON-safe copy of the model's label: letters, digits,
    spaces and simple punctuation only, cut to kMaxLabelLength. */
void setTimerLabel(const char* label) {
  size_t length = 0;
  for (const char* c = label; *c != '\0' && length < kMaxLabelLength; c++) {
    if (isalnum(static_cast<unsigned char>(*c)) || strchr(" ,.'-", *c) != nullptr) {
      timer_label[length++] = *c;
    }
  }
  while (length > 0 && timer_label[length - 1] == ' ') {
    length--;
  }
  timer_label[length] = '\0';
}

/** Seconds until the timer rings, rounded up (0 once due). */
uint32_t secondsLeft() {
  uint32_t elapsed_ms = millis() - timer_started_ms;
  return elapsed_ms >= timer_length_ms ? 0 : (timer_length_ms - elapsed_ms + 999) / 1000;
}

/** Sends a tool's output text and asks the model to say it. */
void sendToolOutput(WebSocketsClient& webSocket, const char* call_id, const char* output) {
  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"%s"}})JSON",
           call_id, output);
  webSocket.sendTXT(tool_response_message);
  webSocket.sendTXT(kResponseCreateMessage);
  LOG_DEBUG("Sent function_call_output (\"%s\") - asked the model for a spoken answer.\n", output);
}

/** Blinks the LED and beeps kAlarmBeeps times, then puts the LED back. */
void ringAlarm() {
  bool led_was_on = esp32va_board::isLedOn();
  esp32va_hands_free::holdOff();
  for (int beep = 0; beep < kAlarmBeeps; beep++) {
    setLed(true);
    playTestBeep();
    setLed(false);
    delay(kAlarmGapMs);
  }
  setLed(led_was_on);
  esp32va_hands_free::holdOff();  // restart the cooldown after the last beep
}

}  // namespace

bool handleGetCurrentTimeCall(JsonDocument& doc, WebSocketsClient& webSocket) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }
  time_t now = time(nullptr);
  struct tm local_time;
  localtime_r(&now, &local_time);
  char output[96];
  if (local_time.tm_year + 1900 < kEarliestPlausibleYear) {
    snprintf(output, sizeof(output), "The board's clock is not set, so the time is unknown.");
  } else {
    strftime(output, sizeof(output), "It is %H:%M on %A, %d %B %Y.", &local_time);
  }
  LOG_INFO("Time asked - %s\n", output);
  sendToolOutput(webSocket, call_id, output);
  return true;
}

bool handleSetTimerCall(JsonDocument& doc, WebSocketsClient& webSocket) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  const char* arguments_json = doc["arguments"] | "{}";
  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }
  JsonDocument args_doc;
  DeserializationError err = deserializeJson(args_doc, arguments_json);
  JsonVariant seconds_value = args_doc["seconds"];
  if (err || !(seconds_value.is<int>() || seconds_value.is<float>())) {
    LOG_ERROR("set_timer called without a number of seconds: %s\n", arguments_json);
    return false;
  }
  long seconds = lroundf(seconds_value.as<float>());
  char output[64 + kMaxLabelLength];
  if (seconds <= 0) {
    bool was_armed = timer_armed;
    timer_armed = false;
    esp32va_status::printProtocolLine("@@TIMER off");
    LOG_INFO("Timer cancelled (voice command).\n");
    snprintf(output, sizeof(output), "%s", was_armed ? "Timer cancelled." : "There was no timer running.");
  } else {
    seconds = constrain(seconds, 1L, kMaxTimerSeconds);
    timer_armed = true;
    timer_started_ms = millis();
    timer_length_ms = static_cast<uint32_t>(seconds) * 1000;
    setTimerLabel(args_doc["label"] | "");
    announceTimerState();
    LOG_INFO("Timer set for %ld s (voice command)%s%s.\n", seconds, timer_label[0] ? " - " : "", timer_label);
    if (timer_label[0] != '\0') {
      snprintf(output, sizeof(output), "Reminder set: %s, in %ld seconds.", timer_label, seconds);
    } else {
      snprintf(output, sizeof(output), "Timer set for %ld seconds.", seconds);
    }
  }
  sendToolOutput(webSocket, call_id, output);
  return true;
}

bool serviceTimer(bool board_idle) {
  if (!timer_armed || secondsLeft() > 0 || !board_idle) {
    return false;
  }
  timer_armed = false;
  LOG_INFO("Timer done - ringing.\n");
  if (timer_label[0] != '\0') {
    esp32va_status::printProtocolLine("@@TIMER done %s", timer_label);
  } else {
    esp32va_status::printProtocolLine("@@TIMER done");
  }
  ringAlarm();
  return true;
}

void sendTimerAnnouncement(WebSocketsClient& webSocket) {
  char spoken[kMaxLabelLength + 32];
  if (timer_label[0] != '\0') {
    snprintf(spoken, sizeof(spoken), "Time's up - %s.", timer_label);
  } else {
    snprintf(spoken, sizeof(spoken), "Time's up - your timer is done.");
  }
  snprintf(announcement_json, sizeof(announcement_json), kAnnouncementTemplate, spoken);
  webSocket.sendTXT(announcement_json);
  LOG_INFO("Timer announcement sent: \"%s\"\n", spoken);
}

void announceTimerState() {
  if (timer_armed) {
    esp32va_status::printProtocolLine("@@TIMER %lu %lu%s%s", (unsigned long)secondsLeft(),
                                      (unsigned long)(timer_length_ms / 1000), timer_label[0] ? " " : "", timer_label);
  } else {
    esp32va_status::printProtocolLine("@@TIMER off");
  }
}

}  // namespace esp32va_clock
