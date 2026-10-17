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

namespace esp32va_info {

/** Handles a finalized response.function_call_arguments.done event for the
    get_board_status tool: sends the Wi-Fi signal, the uptime and the free
    heap as the function_call_output, plus a response.create so the model
    says it. Returns true if handled, false (logged) if call_id is missing. */
bool handleGetBoardStatusCall(JsonDocument& doc, WebSocketsClient& webSocket);

}  // namespace esp32va_info
