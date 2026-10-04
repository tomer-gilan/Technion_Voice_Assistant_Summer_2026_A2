#pragma once

#include <WebSocketsClient.h>

// Shared plumbing for talking to OpenAI's Realtime API over WSS: connection
// setup (host/port/path/auth header/root CA), reconnect backoff, and the
// generic WebSocket event logging every test sketch since step 09 has
// needed. All of it reads OPENAI_API_KEY / OPENAI_MODEL straight from
// config.h - no parameters needed for setup.

/** Opens a WSS connection to the OpenAI Realtime API ("/v1/realtime?model=..."
    per config.h's OPENAI_MODEL), with the Authorization header and root CA
    (GTS Root R4 - see step 9 for how this was verified) already applied.
    Caller must set webSocket.onEvent(...) before calling this. */
void beginOpenAiRealtimeConnection(WebSocketsClient& webSocket);

/** Adds up to +/-20% random jitter to a delay, so retries don't all land
    on the same cadence. */
unsigned long addJitter(unsigned long base_ms);

/** Call from a sketch's WStype_DISCONNECTED handler (only while no verdict
    has been reached yet). Applies the next exponential-backoff reconnect
    interval to webSocket and returns false - or, once the attempt cap is
    hit, returns true without touching webSocket, meaning the caller should
    declare TEST FAILED instead of retrying again. Logs its own progress
    either way. */
bool registerDisconnectAndMaybeGiveUp(WebSocketsClient& webSocket);

/** Resets the reconnect-attempt/backoff state. Call once a verdict (PASS or
    FAIL) has been reached, so state doesn't leak into a later reconnect. */
void resetReconnectBackoff();

/** Logs any WebSocket event type a sketch doesn't give special handling to
    (BIN, ERROR, fragments, PING/PONG, and anything unrecognized). Call this
    from a sketch's onWebSocketEvent default case so nothing goes unnoticed,
    per Tomer's Guidelines - the sketch itself only needs explicit cases for
    CONNECTED/DISCONNECTED/TEXT (or whichever it actually reacts to). */
void logGenericWsEvent(WStype_t type, uint8_t* payload, size_t length);
