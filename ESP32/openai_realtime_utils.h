#pragma once

#include <WebSocketsClient.h>

// Shared plumbing for talking to OpenAI's Realtime API over WSS: connection
// setup (host/port/path/auth header/root CA), reconnect backoff, and the
// generic WebSocket event logging. All of it reads OPENAI_API_KEY / OPENAI_MODEL straight from SECRETS.h /
// parameters.h - no parameters needed for setup.
//
// The board never gives up on the connection: after
// kOpenAiAttemptsBeforeOffline failed attempts in a row it counts as offline
// (the control center shows it, the timer and the controls keep working) and
// keeps retrying every kOfflineRetryIntervalMs until OpenAI is back.

namespace esp32va_openai {

/** Opens a WSS connection to the OpenAI Realtime API ("/v1/realtime?model=..."
    per parameters.h's OPENAI_MODEL), with the Authorization header and root CA
    (GTS Root R4 - openai_realtime_utils.cpp says how it was verified) already applied,
    and the ping/pong heartbeat (parameters.h's kHeartbeatIntervalMs) on.
    Caller must set webSocket.onEvent(...) before calling this. */
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
