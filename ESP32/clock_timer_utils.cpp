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
#include "openai_realtime_utils.h"
#include "parameters.h"
#include "status_report_utils.h"

namespace esp32va_clock {

namespace {

constexpr long kMaxTimerSeconds = esp32va_params::kMaxTimerSeconds;
constexpr int kAlarmBeeps = esp32va_params::kTimerAlarmBeeps;
constexpr uint32_t kAlarmGapMs = esp32va_params::kTimerAlarmGapMs;  // LED off, silence, between two beeps
constexpr int kEarliestPlausibleYear = 2024;  // before this the clock was never synced

constexpr size_t kMaxLabelLength = 48;  // "check the oven" - a few words, said back out loud
constexpr size_t kAnnouncementBufferSize = kMaxLabelLength + 200;  // the ~130-char template plus the spoken text (at most kMaxLabelLength + 32)
// The announcement's text goes inside a JSON string: setTimerLabel() keeps
// only characters that need no escaping.
constexpr char kAnnouncementTemplate[] =
    R"JSON({"type":"response.create","response":{"instructions":"Say exactly and only the following, with no other words or tool calls: \"%s\""}})JSON";

bool timer_armed = false;
bool timer_waiting_to_start = false;  // set by voice: starts once the board is done saying so
uint32_t timer_started_ms = 0;
uint32_t timer_length_ms = 0;
char timer_label[kMaxLabelLength + 1] = "";  // "" when the timer has no label
int timer_light_action = -1;  // -1: leave the LED alone when it rings, 0: turn it off, 1: turn it on
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
  if (timer_waiting_to_start) {
    return timer_length_ms / 1000;
  }
  uint32_t elapsed_ms = millis() - timer_started_ms;
  return elapsed_ms >= timer_length_ms ? 0 : (timer_length_ms - elapsed_ms + 999) / 1000;
}

/** Sends a tool's output text (server_events.ino asks the model to say it once the call's response is done). */
void sendToolOutput(WebSocketsClient& webSocket, const char* call_id, const char* output) {
  esp32va_openai::sendFunctionCallOutput(webSocket, call_id, output);
  LOG_DEBUG("Sent function_call_output (\"%s\").\n", output);
}

/** Blinks the LED and beeps kAlarmBeeps times, then puts the LED back. */
void ringAlarm() {
  bool led_was_on = esp32va_board::isLedOn();
  esp32va_hands_free::holdOff();
  for (int beep = 0; beep < kAlarmBeeps; beep++) {
    esp32va_board::setLed(true);
    esp32va_audio::playTestBeep();
    esp32va_board::setLed(false);
    delay(kAlarmGapMs);
  }
  esp32va_board::setLed(led_was_on);
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
  char output[96 + 48 + kMaxLabelLength];
  if (local_time.tm_year + 1900 < kEarliestPlausibleYear) {
    snprintf(output, sizeof(output), "The board's clock is not set, so the time is unknown.");
  } else {
    strftime(output, sizeof(output), "It is %H:%M on %A, %d %B %Y.", &local_time);
  }
  if (timer_armed) {  // so "how long is left on my timer?" can be answered from the same call
    size_t used = strlen(output);
    snprintf(output + used, sizeof(output) - used, " The timer has %lu seconds left%s%s.",
             (unsigned long)secondsLeft(), timer_label[0] ? " - " : "", timer_label);
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
    sendToolOutput(webSocket, call_id, "Error: seconds must be a number.");  // so the model still answers
    return true;
  }
  // 0 cancels; a negative or sub-second value is a mistake, not a cancel, so
  // it leaves a running timer alone and the model asks again.
  char output[112 + kMaxLabelLength];
  float requested_seconds = seconds_value.as<float>();
  if (requested_seconds < 0.0f || (requested_seconds > 0.0f && requested_seconds < 1.0f)) {
    LOG_INFO("set_timer refused: %.2f seconds is not 0 (cancel) or 1 to %ld - the timer is unchanged.\n",
             requested_seconds, kMaxTimerSeconds);
    snprintf(output, sizeof(output),
             "Error: seconds must be 0 to cancel the timer, or from 1 to %ld. Nothing was changed - ask the user how long the timer should be.",
             kMaxTimerSeconds);
    sendToolOutput(webSocket, call_id, output);
    return true;
  }
  bool capped = requested_seconds > static_cast<float>(kMaxTimerSeconds);
  long seconds = capped ? kMaxTimerSeconds : lroundf(requested_seconds);
  if (seconds <= 0) {
    bool was_armed = timer_armed;
    timer_armed = false;
    timer_waiting_to_start = false;
    timer_light_action = -1;
    esp32va_status::printProtocolLine("@@TIMER off");
    LOG_INFO("Timer cancelled (voice command).\n");
    snprintf(output, sizeof(output), "%s", was_armed ? "Timer cancelled." : "There was no timer running.");
  } else {
    seconds = constrain(seconds, 1L, kMaxTimerSeconds);
    timer_armed = true;
    timer_waiting_to_start = true;  // serviceTimer() starts it once the confirmation has been said
    timer_length_ms = static_cast<uint32_t>(seconds) * 1000;
    setTimerLabel(args_doc["label"] | "");
    // "Turn off the light in 10 minutes": switch the LED when it rings.
    const char* light = args_doc["light"] | "";
    timer_light_action = strcmp(light, "on") == 0 ? 1 : strcmp(light, "off") == 0 ? 0 : -1;
    if (timer_light_action >= 0 && timer_label[0] == '\0') {
      setTimerLabel(timer_light_action ? "light on" : "light off");
    }
    LOG_INFO("Timer set for %ld s (voice command)%s%s - starts once that's been said.\n", seconds, timer_label[0] ? " - " : "", timer_label);
    if (capped) {  // so the model says the 1-hour cap was applied instead of confirming what was asked
      LOG_INFO("set_timer asked for %.0f s - capped at %ld s.\n", requested_seconds, kMaxTimerSeconds);
      snprintf(output, sizeof(output), "The longest timer is %ld minutes, so it was capped and set for %ld seconds instead%s%s.",
               kMaxTimerSeconds / 60, seconds, timer_label[0] ? ": " : "", timer_label);
    } else if (timer_label[0] != '\0') {
      snprintf(output, sizeof(output), "Reminder set: %s, in %ld seconds.", timer_label, seconds);
    } else {
      snprintf(output, sizeof(output), "Timer set for %ld seconds.", seconds);
    }
  }
  sendToolOutput(webSocket, call_id, output);
  return true;
}

bool serviceTimer(bool board_idle) {
  if (timer_armed && timer_waiting_to_start) {
    if (board_idle) {  // the spoken confirmation is over
      timer_waiting_to_start = false;
      timer_started_ms = millis();
      LOG_INFO("Timer started.\n");
      announceTimerState();
    }
    return false;
  }
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
  if (timer_light_action >= 0) {
    esp32va_board::setLed(timer_light_action == 1);
    LOG_INFO("Timer done - LED turned %s, as asked.\n", timer_light_action ? "on" : "off");
    esp32va_board::announceLedState("voice");  // asked for by voice, just later
    timer_light_action = -1;
  }
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

void announceClock() {
  time_t now = time(nullptr);
  struct tm local_time;
  localtime_r(&now, &local_time);
  if (local_time.tm_year + 1900 < kEarliestPlausibleYear) {
    esp32va_status::printProtocolLine("@@CLOCK unset");
  } else {
    esp32va_status::printProtocolLine("@@CLOCK %02d:%02d %ld", local_time.tm_hour, local_time.tm_min, (long)now);
  }
}

}  // namespace esp32va_clock
