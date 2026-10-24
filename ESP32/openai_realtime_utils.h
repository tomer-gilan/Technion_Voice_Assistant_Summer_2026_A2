#pragma once

#include <WebSocketsClient.h>
#include <stddef.h>
#include <stdint.h>

// Shared plumbing for talking to OpenAI's Realtime API over WSS: connection
// setup (host/port/path/auth header/root CA), the API key, reconnect
// backoff, and the generic WebSocket event logging. OPENAI_MODEL comes
// straight from parameters.h.
//
// The board never gives up on the connection: after
// kOpenAiAttemptsBeforeOffline failed attempts in a row it counts as offline
// (the control center shows it, the timer and the controls keep working) and
// keeps retrying every kOfflineRetryIntervalMs until OpenAI is back.
//
// The API key: the one saved in flash from the control center (OPENAI_KEY
// command), or else SECRETS.h's OPENAI_API_KEY - so a new key needs no
// compile, the same way as the Wi-Fi network (wifi_utils.h). If OpenAI
// refuses the saved key kOpenAiKeyRefusalsBeforeFallback times in a row
// (HTTP 401 or 403 on the WebSocket handshake - parameters.h), the board
// tries SECRETS.h's key instead, and back again the same way - in RAM only,
// the saved one stays in flash. Status line:
//   @@OPENAI_KEY <saved|default> <hint>   - which key the board uses, shown
//       as its first 3 and last 4 characters (sk-...AbCd), never in full
//
// Commands:
//   OPENAI_KEY              - prints the key in use (@@OPENAI_KEY)
//   OPENAI_KEY <key>        - saves a new key in flash and connects with it
//                             right away
//   OPENAI_KEY_FORGET       - back to SECRETS.h's key

namespace esp32va_openai {

/** API key limits, in characters: an OpenAI key is "sk-" and then letters,
    digits, '-' and '_' (a project key, sk-proj-..., is 164 long). Also
    reported to the laptop app in @@LIMITS (board_info_utils.h). */
constexpr size_t kMinApiKeyLength = 20;
constexpr size_t kMaxApiKeyLength = 200;

/** Loads the key to use: the one saved in flash, or SECRETS.h's. Call once
    from setup(), before beginOpenAiRealtimeConnection(). */
void loadApiKey();

/** Prints "@@OPENAI_KEY <saved|default> <hint>" - the key in use. */
void announceApiKey();

/** Handles OPENAI_KEY / OPENAI_KEY_FORGET (see above). command is the
    trimmed line. Returns false if it isn't one of them. Sets *key_changed
    when the key in use changed: webSocket's next handshake uses it, and the
    caller closes the connection (or the attempt) still on the old one. Never
    prints the key. */
bool handleApiKeyCommand(const char* command, WebSocketsClient& webSocket, bool* key_changed);

/** Call from the WStype_CONNECTED handler: the handshake went through, so
    the key works - the refusals in a row start over. */
void noteHandshakeAccepted();

/** Call from the WStype_DISCONNECTED handler with its payload (the
    library's reason, e.g. "WebSocket handshake failed - HTTP 401"). Returns
    the HTTP status if OpenAI refused the API key (401 or 403), 0 for any
    other disconnect. Counts the refusal, and after
    kOpenAiKeyRefusalsBeforeFallback in a row switches to the other key -
    if one was saved - for the next attempt. */
int noteDisconnectReason(const uint8_t* reason, size_t length, WebSocketsClient& webSocket);

/** The HTTP status of the last refusal of the key, 0 if it hasn't been
    refused since the last handshake that went through (or the last
    OPENAI_KEY / OPENAI_KEY_FORGET). */
int lastKeyRefusalHttpCode();

/** Opens a WSS connection to the OpenAI Realtime API ("/v1/realtime?model=..."
    per parameters.h's OPENAI_MODEL), with the Authorization header (the key
    from loadApiKey()) and root CA (GTS Root R4 - openai_realtime_utils.cpp
    says how it was verified) already applied, and the ping/pong heartbeat
    (parameters.h's kHeartbeatIntervalMs) on. Caller must set
    webSocket.onEvent(...) before calling this. */
void beginOpenAiRealtimeConnection(WebSocketsClient& webSocket);

/** Adds up to +/-20% random jitter to a delay, so retries don't all land
    on the same cadence. */
unsigned long addJitter(unsigned long base_ms);

/** Call from the WStype_DISCONNECTED handler. Sets the next reconnect
    interval on webSocket - exponential backoff, then every
    kOfflineRetryIntervalMs once offline - and returns true exactly once: on
    the disconnect that makes the board offline. Logs its own progress. */
bool registerDisconnect(WebSocketsClient& webSocket);

/** Clears the attempt count and the backoff. Call whenever the session is
    ready again, so earlier disconnects never add up to "offline". */
void resetReconnectBackoff(WebSocketsClient& webSocket);

/** Logs any WebSocket event type the sketch doesn't give special handling to
    (BIN, ERROR, fragments, PING/PONG, and anything unrecognized). Call this
    from onWebSocketEvent's default case so nothing goes unnoticed. */
void logGenericWsEvent(WStype_t type, uint8_t* payload, size_t length);

/** Disconnects in a row since the session was last ready - shown as
    "reconnecting n/max" in the laptop app. */
uint32_t reconnectAttemptCount();

/** How many disconnects in a row make the board offline. */
uint32_t attemptsBeforeOffline();

/** True once attemptsBeforeOffline() disconnects in a row have happened and
    the session hasn't been ready since. */
bool isOffline();

/** How often the board retries once offline. */
unsigned long offlineRetryIntervalMs();

/** Answers a tool call: sends one conversation.item.create holding a
    function_call_output with call_id and output inserted as they are (so
    output must already be JSON-safe). The follow-up response.create is
    server_events.ino's (handleServerMessage()), once the tool-call response
    is done. Called from the tool handlers, inside the WebSocket TEXT event -
    a known exception to "no sends from the callback", see
    Documentation/decisions.md. One static buffer, shared by every tool. */
void sendFunctionCallOutput(WebSocketsClient& webSocket, const char* call_id, const char* output);

}  // namespace esp32va_openai
