#pragma once

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

// "How are you doing?" - the get_board_status tool: the board reports on
// itself, in one sentence the model can say back:
//
//   "Wi-Fi signal is strong (-42 dBm), up for 12 minutes, 45 KB of memory free."
//
// Read-only - it changes nothing on the board, so it prints no @@ line of
// its own (the app sees it as @@STATE tool get_board_status).
//
// Also the board's fixed limits, for the laptop app (so it doesn't keep its
// own copies of them), as one line of space-separated key=value integers:
//   @@LIMITS max_ask_bytes=200 max_ssid_bytes=32 min_password_bytes=8 ...

namespace esp32va_info {

/** Prints the @@LIMITS line above from the firmware's own constants: the
    typed question, Wi-Fi name and password and API key sizes in bytes,
    recordings kept,
    the mic's software gain, the light sensor's top value, the longest
    hands-free utterance and the hands-free sensitivity / end-of-speech
    silence ranges. Once at boot and again for every STATUS. */
void announceLimits();

/** Handles a finalized response.function_call_arguments.done event for the
    get_board_status tool: sends the Wi-Fi signal, the uptime and the free
    heap as the function_call_output, plus a response.create so the model
    says it. Returns true if handled, false (logged) if call_id is missing. */
bool handleGetBoardStatusCall(JsonDocument& doc, WebSocketsClient& webSocket);

}  // namespace esp32va_info
