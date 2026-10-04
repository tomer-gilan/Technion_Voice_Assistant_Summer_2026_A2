#pragma once

#include <Arduino.h>

// Leveled logging over Serial.printf, gated by a runtime log level. This
// deliberately does NOT cover test verdicts (the PASSED/FAILED/HALTED
// banners and their immediate "Type LISTEN..." follow-up hints) or raw
// streamed content (Wi-Fi/SNTP wait dots, the model's transcript text as it
// streams in) - those always print regardless of level, since they're the
// sketch's actual required output, not diagnostic noise to be filtered.

// Severity doctrine (2026-09-19): each level is a cumulative superset of the
// one below it (g_log_level >= LOG_LEVEL_DEBUG shows everything), so this is
// about what belongs at EACH level, not what it excludes:
//   ERROR - simple and concise. One line, states the problem, no protocol
//           dumps or rationale paragraphs. A human should be able to scan a
//           wall of ERROR lines and know what broke without re-reading.
//   INFO  - human-readable and non-noisy. The narrative a person watching
//           Serial actually wants: connected, LISTEN #N started, got a
//           reply, reconnected. Never repeats content already shown via raw
//           streamed output (e.g. the transcript), and never fires more than
//           a few times per LISTEN cycle - anything that could fire many
//           times in one reply (e.g. backpressure pause/resume) is DEBUG.
//   DEBUG - extremely verbose, written for analysis (by a human OR an AI
//           reading the log back) rather than for reading live. Covers the
//           actual info: everywhere INFO's wording is trimmed or a message
//           is demoted out of INFO entirely, the fuller/more technical
//           version lives here, so nothing is ever truly lost - only
//           deprioritized out of the default reading experience.
enum LogLevel {
  LOG_LEVEL_NONE = 0,   // nothing through LOG_*() - verdicts/raw output still print
  LOG_LEVEL_ERROR = 1,  // genuine problems: parse/protocol/allocation failures
  LOG_LEVEL_INFO = 2,   // normal operational narrative: connects, LISTEN cycles, replies
  LOG_LEVEL_DEBUG = 3,  // routine per-event protocol bookkeeping, and the full detail behind every INFO/ERROR line
};

// Real-hardware discovery (2026-09-19): a bare global named `g_log_level`
// got silently clobbered (read back as LOG_LEVEL_ERROR instead of its
// LOG_LEVEL_DEBUG initializer) right around Wi-Fi connecting, with nothing
// in this project ever writing to it - strongly indicating a linker-level
// symbol collision with an identically-named global in one of the pulled-in
// libraries (WiFi/lwIP/ESP-IDF/WebSockets/etc. all being plausible, "log
// level" being a generic enough name to collide). Namespacing it changes its
// mangled linker symbol entirely, which rules that class of collision out
// regardless of which library it actually was. See PROJECT_OVERVIEW.md step
// 17 for the full bisection.
namespace esp32va_log {
/** Defined in log_utils.cpp. Defaults to LOG_LEVEL_INFO as of step 20a (was
    LOG_LEVEL_DEBUG through step 19, while the doctrine/format were still
    being worked out) - change here, or assign to it at runtime, to adjust
    verbosity. */
extern LogLevel g_log_level;
}  // namespace esp32va_log

/** Generic macro every LOG_*() below expands to - prepends a "[LEVEL]"
    tag and the calling function name/source line to fmt (via
    __func__/__LINE__, standard C++11 - not a "__FUNC__" macro, which
    doesn't exist) before handing off to Serial.printf, and only when level
    is at or below g_log_level. No per-module tag like the old
    "[openai_playback]"/"[wifi]" prefixes - __func__ already identifies
    where a message came from, and every function name in this project is
    unique and self-explanatory, so a second, hand-maintained tag was pure
    redundancy (step 20a). fmt must be a string literal, since it's
    concatenated with the "[LEVEL][%s:%d] " prefix at compile time -
    level_str/__func__ themselves are passed as normal %s arguments, not
    pasted into the literal. */
#define LOG(level, level_str, fmt, ...) \
  do { \
    if (esp32va_log::g_log_level >= (level)) { \
      Serial.printf("[" level_str "] [%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); \
    } \
  } while (0)

#define LOG_ERROR(fmt, ...) LOG(LOG_LEVEL_ERROR, "ERROR", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG(LOG_LEVEL_INFO,  "INFO",  fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG(LOG_LEVEL_DEBUG, "DEBUG", fmt, ##__VA_ARGS__)
