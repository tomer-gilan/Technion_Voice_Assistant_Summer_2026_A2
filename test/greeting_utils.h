#pragma once

#include <WebSocketsClient.h>

// A randomized spoken opening line, sent once when the session first becomes
// ready. This is NOT a tool/function call (unlike board_state_utils.h's
// set_board_state) - there's no schema, no arguments, no function_call_output
// round trip. It's a per-response "instructions" override on a plain
// response.create, the same mechanism session.update's top-level
// "instructions" uses for the persistent system prompt, just scoped to a
// single response instead - confirmed against OpenAI's current Realtime API
// docs (2026-09-21), not assumed. See PROJECT_OVERVIEW.md step 22.

namespace esp32va_greeting {

/** Picks one of a fixed set of casual opening lines (uniformly, via the
    ESP32's true hardware RNG - esp_random(), not Arduino's random()/rand(),
    which need explicit seeding to avoid repeating the same sequence every
    boot), with a chance of prepending a time-of-day-correct "Good
    morning/afternoon/evening! " prefix (skipped entirely outside those
    hours - no prefix at night, deliberately, since "Good night" reads as a
    farewell here, not a greeting). Builds the resulting response.create
    message and sends it over `webSocket`. Requires the system clock to
    already be synced with TIMEZONE_UTC_OFFSET_SEC applied (see
    wifi_utils.h's syncSystemTime() and config.h) so a chosen prefix matches
    real local time. Call once, when the session first becomes ready - not
    on every reconnect. */
void sendOpeningGreeting(WebSocketsClient& webSocket);

}  // namespace esp32va_greeting
