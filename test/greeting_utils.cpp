#include "greeting_utils.h"

#include <Arduino.h>
#include <esp_system.h>
#include <time.h>
#include "log_utils.h"

namespace esp32va_greeting {

namespace {

// Phrased to read naturally with or without a time-of-day prefix in front -
// none lead with "Hey"/"Hi", since a prefix (when chosen) already serves as
// the greeting word, and these still read fine entirely on their own too.
const char* const kOpeners[] = {
    "What's up?",
    "How's it going?",
    "What are we tackling today?",
    "Ready when you are.",
    "What's on your mind?",
    "What can I do for you?",
    "Ready to help - what's up?",
    "What are we doing today?",
    "How's your day going?",
    "What can I help with?",
    "I'm all ears.",
    "So, what's the plan?",
    "What do you need?",
    "What's the mission today?",
    "What are we working on?",
    "Nice to hear from you - what's up?",
    "Ready when you are - what's up?",
    "What can I do for you today?",
    "What brings you by?",
    "What's happening?",
};
constexpr size_t kNumOpeners = sizeof(kOpeners) / sizeof(kOpeners[0]);

// Maps a local hour-of-day to a spoken prefix. Hours matching no entry
// (night) get no prefix at all - not a "Good day"/"Good night" fallback,
// per Tomer's call (2026-09-21).
struct TimeOfDayPrefix {
  int start_hour_inclusive;
  int end_hour_inclusive;
  const char* prefix;
};
const TimeOfDayPrefix kTimeOfDayPrefixes[] = {
    {5, 11, "Good morning! "},
    {12, 17, "Good afternoon! "},
    {18, 21, "Good evening! "},
};
constexpr size_t kNumTimeOfDayPrefixes = sizeof(kTimeOfDayPrefixes) / sizeof(kTimeOfDayPrefixes[0]);

constexpr int kTimePrefixChancePercent = 20;  // Tomer's call (2026-09-21) - otherwise no prefix at all
constexpr size_t kMaxGreetingLen = 96;        // longest opener + longest prefix + margin

// %s is substituted with the chosen greeting text (plain ASCII, no quotes/
// backslashes to escape). The \" pair is literal in this raw string (raw
// strings don't process escapes) - it becomes JSON's escaped inner quote
// around the greeting once embedded in the outer "instructions" string.
constexpr char kGreetingResponseTemplate[] =
    R"JSON({"type":"response.create","response":{"instructions":"Say exactly and only the following, with no other words or tool calls: \"%s\""}})JSON";
constexpr size_t kGreetingJsonBufferSize = kMaxGreetingLen + 128;  // template text + JSON punctuation, generous margin

// Static, not stack-local: same reasoning as test.ino's own buffers (avoids
// heap fragmentation - see CLAUDE.md's Engineering Constraints).
char greeting_text[kMaxGreetingLen];
char greeting_json[kGreetingJsonBufferSize];

/** Looks up which (if any) time-of-day prefix matches `hour` (0-23).
    Returns nullptr if none match (night). */
const char* matchingTimeOfDayPrefix(int hour) {
  for (size_t i = 0; i < kNumTimeOfDayPrefixes; i++) {
    const TimeOfDayPrefix& band = kTimeOfDayPrefixes[i];
    if (hour >= band.start_hour_inclusive && hour <= band.end_hour_inclusive) {
      return band.prefix;
    }
  }
  return nullptr;
}

/** Composes the chosen opening line (with its possible time-of-day prefix)
    into `out`. The system clock is assumed already synced - guaranteed by
    call order (syncSystemTime() blocks in setup(), long before a session can
    exist - see wifi_utils.h), so this doesn't re-check that. */
void composeOpeningGreeting(char* out, size_t out_size) {
  const char* opener = kOpeners[esp_random() % kNumOpeners];

  time_t now = time(nullptr);
  struct tm local_time;
  localtime_r(&now, &local_time);
  const char* matched_prefix = matchingTimeOfDayPrefix(local_time.tm_hour);

  const char* prefix = "";
  if (matched_prefix != nullptr && (esp_random() % 100) < kTimePrefixChancePercent) {
    prefix = matched_prefix;
  }

  snprintf(out, out_size, "%s%s", prefix, opener);
}

}  // namespace

void sendOpeningGreeting(WebSocketsClient& webSocket) {
  composeOpeningGreeting(greeting_text, sizeof(greeting_text));
  LOG_INFO("Opening greeting chosen: \"%s\"\n", greeting_text);
  snprintf(greeting_json, sizeof(greeting_json), kGreetingResponseTemplate, greeting_text);
  webSocket.sendTXT(greeting_json);
}

}  // namespace esp32va_greeting
