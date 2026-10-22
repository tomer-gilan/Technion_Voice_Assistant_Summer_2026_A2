#pragma once

#include <stddef.h>
#include <stdint.h>

// Cheap raw-JSON scanning helpers for the OpenAI Realtime API's server
// events, used to avoid a full deserializeJson() pass on events that carry a
// large payload (e.g. a base64 audio delta) before even knowing whether it's
// worth acting on. Both functions scan the WebSocket library's own
// already-allocated payload buffer directly - no copy, no JsonDocument.

/** Extracts the "type" field's value from a raw JSON event payload without a
    full parse, by scanning for the literal `"type":"..."` that every event
    observed so far starts with. Returns false if not found within the first
    128 bytes (which "type" always is, for every event observed). */
bool extractEventType(const uint8_t* payload, size_t length, char* out, size_t out_size);

/** Finds a `"field_name":"..."` string field anywhere in a raw JSON payload
    and returns a pointer/length into the ORIGINAL buffer - no copy - so a
    large value (like a base64 audio blob) is never duplicated in RAM just to
    read it. Safe for base64 content specifically: that alphabet never
    contains a literal `"`, so the first following quote is always the true
    end. */
bool findRawStringField(const uint8_t* payload, size_t length, const char* field_name,
                         const uint8_t** value_start, size_t* value_len);
