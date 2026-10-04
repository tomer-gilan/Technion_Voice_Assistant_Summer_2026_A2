// Step 21 - Guard against spurious tool calls on empty/silent input
// Builds directly on step 22 (test/22_openai_opening_greeting.ino.inactive):
// no change to the audio/protocol/opening-greeting/LISTEN-cycle mechanics.
// One change: kSessionUpdateMessage's instructions gained a second STRICT
// RULE telling the model to only call set_board_state when THIS turn
// contains a clear, explicit request - never inferred from earlier turns,
// a repeated pattern, or silence.
//
// Bug being fixed (found during step 20a's verification, tracked since as
// step 21): "turn LED on" -> works. "turn OFF" -> works. Then saying nothing
// at all, repeatedly - each empty LISTEN cycle (`You said: ""` in the log)
// still toggled the LED again, narrating a state change nobody asked for in
// that turn. The model was evidently inferring/continuing a pattern from
// conversation history rather than treating each turn's actual (lack of)
// content as the only basis for acting.
//
// PASS criterion: repeat the exact repro (ask for LED on, then off, then say
// nothing several times in a row) and confirm the LED stops changing and
// set_board_state stops being called on the silent turns - see
// PROJECT_OVERVIEW.md step 21's "What to expect?" section for the full
// procedure and what to watch for.
//
// No new shared module this step - just the instructions text. Wi-Fi/WSS
// plumbing, full-duplex I2S/ring buffer, raw-JSON scanning, leveled logging,
// onboard LED/set_board_state, and the opening greeting stay in wifi_utils,
// openai_realtime_utils, i2s_audio_utils, realtime_json_utils, log_utils,
// board_state_utils, and greeting_utils, unchanged.

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "log_utils.h"
#include "wifi_utils.h"
#include "openai_realtime_utils.h"
#include "i2s_audio_utils.h"
#include "realtime_json_utils.h"
#include "board_state_utils.h"
#include "greeting_utils.h"

namespace {
// Unchanged from step 18/19: 24kHz PCM in / 8kHz mu-law out, manual turn
// control, the set_board_state tool, and the one-short-sentence instructions.
const char kSessionUpdateMessage[] =
    R"JSON({"type":"session.update","session":{"type":"realtime","model":")JSON"
    OPENAI_MODEL
    R"JSON(","instructions":"You are a voice assistant on an ESP32 dev board. STRICT RULE: reply in exactly ONE short sentence, no more than about 12 words - never two sentences, never a list, no exceptions even for complex questions. If the full answer would need more than that, say only the single most important part of it and drop the rest - a short incomplete answer is correct, a longer complete one is not. When the user asks you to turn the onboard LED on or off, call the set_board_state function with the requested led_on value - don't just say you did it. STRICT RULE: only call set_board_state if THIS specific message clearly and explicitly asks for a board state change - never call it because of an earlier request, a repeated pattern from the conversation so far, or silence/unclear audio; if you did not clearly hear a specific request in this turn, do not call the function and do not assume one was intended.","output_modalities":["audio"],"audio":{"input":{"format":{"type":"audio/pcm","rate":24000},"turn_detection":null,"transcription":{"model":"gpt-4o-mini-transcribe"}},"output":{"format":{"type":"audio/pcmu"},"voice":"alloy"}},"tools":[{"type":"function","name":"set_board_state","description":"Turns the ESP32 dev board's onboard LED on or off.","parameters":{"type":"object","properties":{"led_on":{"type":"boolean","description":"true to turn the LED on, false to turn it off"}},"required":["led_on"]}}],"tool_choice":"auto","reasoning":{"effort":"low"}}})JSON";

const char kCommitMessage[] = R"JSON({"type":"input_audio_buffer.commit"})JSON";
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";

constexpr uint32_t kSampleRateHz = 24000;   // must match kSessionUpdateMessage's input rate and i2s_audio_utils's I2S rate
constexpr uint32_t kRecordDurationSec = 3;  // same duration old step 8b/step 12 used
constexpr uint32_t kChunkSamples = 2400;    // ~100ms per chunk at 24kHz - same chunk duration step 12 confirmed working
constexpr int32_t kSoftwareGain = 4;        // mic levels are otherwise quiet - see step 3 findings
constexpr unsigned long kReplyTimeoutMs = 15000;  // max silence (no server message) while awaiting_reply, not a total-reply-duration cap - see checkReplyTimeout()

constexpr uint32_t kChunkPcmBytes = kChunkSamples * sizeof(int16_t);
constexpr uint32_t kBase64BufferSize = ((kChunkPcmBytes + 2) / 3) * 4 + 1;
constexpr uint32_t kJsonMessageBufferSize = kBase64BufferSize + 128;

// Static, not stack-local: these would overflow the default Arduino loop
// task stack if declared inside a function (same reasoning as old step
// 8b/step 12).
int32_t raw_chunk[kChunkSamples];
int16_t pcm_chunk[kChunkSamples];
unsigned char base64_chunk[kBase64BufferSize];
char json_message[kJsonMessageBufferSize];

WebSocketsClient webSocket;
bool should_keep_running = true;   // only false if the connection itself can never be established
bool should_send_session_update = false;
bool should_send_opening_greeting = false;  // set once, on the very first session.updated - see handleServerMessage()
bool session_ready = false;        // true once session.updated has been received at least once
bool session_ever_ready = false;
bool is_capturing = false;
bool awaiting_reply = false;
bool reply_received_this_cycle = false;
bool transcript_in_progress = false;   // a response.output_audio_transcript.delta stream is currently mid-line
bool response_fully_received = false;  // the final (post-tool-call, if any) response.done arrived - ring buffer may still be draining
bool function_call_turn_pending = false;  // true from a finalized tool call until its function-call-only response.done is consumed
uint32_t listen_cycle = 0;         // deliberately never reset - keeps counting across reconnects for a soak test
uint32_t connection_count = 0;     // how many times WStype_CONNECTED has fired this run (1 = first connect, 2+ = reconnect)
unsigned long last_reply_activity_ms = 0;  // last time ANY server message arrived while awaiting_reply - see checkReplyTimeout()
bool ws_reads_paused_for_backpressure = false;  // see i2s_audio_utils.h's ringShouldPauseReads()
}  // namespace

/** Marks this LISTEN cycle as having gotten a reply, logging the verdict exactly once per cycle.
    Verdicts are never gated behind a log level - see log_utils.h. */
void markReplyReceived() {
  if (reply_received_this_cycle) return;
  reply_received_this_cycle = true;
  awaiting_reply = false;
  Serial.printf("===== LISTEN #%u PASSED =====\n", listen_cycle);
  Serial.println("Type LISTEN to continue the conversation.");
}

/** Marks this LISTEN cycle as failed, logging the verdict exactly once per cycle.
    Reserved for a genuine problem (protocol error, failed response, timeout) - not
    logged at all if a reply already arrived for this cycle. Verdicts are never
    gated behind a log level - see log_utils.h. */
void markReplyFailed(const char* reason) {
  if (reply_received_this_cycle || !awaiting_reply) return;
  reply_received_this_cycle = true;
  awaiting_reply = false;
  Serial.printf("===== LISTEN #%u FAILED: %s =====\n", listen_cycle, reason);
  if (session_ready) {
    Serial.println("Type LISTEN to try again.");
  }
}

/** Halts the sketch entirely - reserved for the connection itself never succeeding
    (see openai_realtime_utils's attempt cap), not for a single LISTEN cycle failing.
    Never gated behind a log level - see log_utils.h. */
void haltSketch(const char* reason) {
  should_keep_running = false;
  Serial.printf("===== HALTED: %s =====\n", reason);
}

/** Parses one server text frame, logs its "type" and any interesting fields, and
    drives session readiness plus the current LISTEN cycle's PASS/FAIL verdict. */
void handleServerMessage(uint8_t* payload, size_t length) {
  // Any server message at all, while a reply is in flight, proves the
  // connection/response is still alive - resets the inactivity clock so a
  // long reply (paced by playback backpressure) doesn't trip
  // checkReplyTimeout() just for taking a while. See PROJECT_OVERVIEW.md
  // step 16's "reply timeout fired mid-reply" discovery.
  if (awaiting_reply) {
    last_reply_activity_ms = millis();
  }

  char type_buf[48];
  bool have_type = extractEventType(payload, length, type_buf, sizeof(type_buf));

  if (have_type && strcmp(type_buf, "response.output_audio.delta") == 0) {
    const uint8_t* value_start = nullptr;
    size_t value_len = 0;
    if (findRawStringField(payload, length, "delta", &value_start, &value_len)) {
      decodeAndRingAudioDelta(value_start, value_len);
    } else {
      LOG_ERROR("Audio delta missing 'delta' field.\n");
    }
    return;
  }

  if (have_type && strncmp(type_buf, "response.output_audio", 22) == 0) {
    // Any other response.output_audio* event (e.g. .done) carries no large
    // payload, but there's nothing this step needs from it either - just
    // log its size for visibility.
    LOG_DEBUG("Event type: %s (%u bytes).\n", type_buf, (unsigned)length);
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(payload), length);
  if (err) {
    LOG_ERROR("JSON parse error: %s\n", err.c_str());
    markReplyFailed("received a message that isn't valid JSON");
    return;
  }

  const char* type = doc["type"] | "(missing type)";

  if (strcmp(type, "session.created") == 0) {
    LOG_DEBUG("Event type: session.created (server-initiated, before our own update).\n");
    return;
  }

  if (strcmp(type, "session.updated") == 0) {
    session_ready = true;
    if (!session_ever_ready) {
      session_ever_ready = true;
      LOG_INFO("Ready - type LISTEN to talk.\n");
      LOG_DEBUG("session.updated received (first connect).\n");
      // Deferred to loop() rather than sent here - same reasoning as
      // should_send_session_update below (sendTXT() from inside this
      // callback can deadlock the library).
      should_send_opening_greeting = true;
      // A later mid-conversation disconnect gets its own fresh attempt
      // budget, rather than one shared with however many retries startup
      // itself needed.
      resetReconnectBackoff();
    } else {
      LOG_INFO("Reconnected (#%u) - conversation context lost. Resuming at LISTEN #%u.\n",
               connection_count - 1, listen_cycle + 1);
      LOG_DEBUG("session.updated received (reconnect #%u) - OpenAI's Realtime API has "
                "no session-resumption mechanism, unlike old Gemini step 8b's handle, so the model has "
                "no memory of anything before this reconnect.\n",
                connection_count - 1);
    }
    return;
  }

  if (strcmp(type, "response.function_call_arguments.delta") == 0) {
    // Arguments arrive complete in .done below - nothing to act on per-delta,
    // and logging each chunk would just be noise.
    return;
  }

  if (strcmp(type, "response.function_call_arguments.done") == 0) {
    LOG_DEBUG("Event type: response.function_call_arguments.done -> %s\n",
              (const char*)(doc["arguments"] | "{}"));
    if (handleSetBoardStateCall(doc, webSocket)) {
      function_call_turn_pending = true;
    }
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.delta") == 0) {
    // Raw streamed content (the model's own spoken-reply transcript) - always
    // printed, never gated behind a log level. See log_utils.h.
    const char* delta = doc["delta"] | "";
    if (!transcript_in_progress) {
      transcript_in_progress = true;
      Serial.print("Streaming transcript: ");
    }
    Serial.print(delta);
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.done") == 0) {
    if (transcript_in_progress) {
      Serial.println();
      transcript_in_progress = false;
    }
    // Not logged at LOG_INFO: this is a full re-print of content already
    // shown live via the streamed deltas above (raw, ungated) - repeating it
    // here would just be noise. Kept at LOG_DEBUG for cross-checking the
    // assembled transcript against what streamed in piece by piece.
    const char* transcript = doc["transcript"] | (const char*)nullptr;
    LOG_DEBUG("response.output_audio_transcript.done -> full transcript: \"%s\"\n",
              transcript != nullptr ? transcript : "(none)");
    return;
  }

  if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
    // Best-effort: a separate ASR model, not the realtime model itself,
    // running asynchronously - see header comment. Never affects the
    // LISTEN cycle's verdict, which depends only on the spoken reply.
    const char* transcript = doc["transcript"] | (const char*)nullptr;
    LOG_INFO("You said: \"%s\"\n", transcript != nullptr ? transcript : "(none)");
    return;
  }

  if (strcmp(type, "conversation.item.input_audio_transcription.failed") == 0) {
    const char* error_message = doc["error"]["message"] | "(no error detail)";
    LOG_ERROR("Input transcription failed: %s\n", error_message);
    LOG_DEBUG("Input transcript unavailable this cycle - the spoken reply itself is unaffected.\n");
    return;
  }

  if (strcmp(type, "response.done") == 0) {
    const char* status = doc["response"]["status"] | "(unknown)";
    LOG_DEBUG("response.done -> status: %s\n", status);
    if (strcmp(status, "failed") == 0) {
      const char* error_message = doc["response"]["status_details"]["error"]["message"] | "(no error detail)";
      markReplyFailed(error_message);
    } else if (function_call_turn_pending) {
      // This response.done was for the function-call-only turn (no audio) -
      // we already sent function_call_output and a follow-up response.create
      // in handleSetBoardStateCall(). The *next* response.done is the
      // model's real spoken confirmation, which does count as the reply.
      function_call_turn_pending = false;
      LOG_INFO("Function call done - waiting for spoken confirmation.\n");
    } else {
      // The PASS verdict itself waits for the ring buffer to actually finish
      // draining to the speaker - see checkPlaybackComplete() in loop().
      // (Also reached for the opening greeting's response.done - harmless,
      // since awaiting_reply is false then, so checkPlaybackComplete() stays
      // a no-op until the next real LISTEN cycle resets this flag anyway.)
      response_fully_received = true;
    }
    return;
  }

  if (strcmp(type, "error") == 0) {
    const char* message = doc["error"]["message"] | "(no message)";
    LOG_ERROR("Error: %s\n", message);
    markReplyFailed(message);
    return;
  }

  LOG_DEBUG("Event type: %s (routine protocol bookkeeping, no action needed).\n", type);
}

/** Handles WebSocket lifecycle events. CONNECTED/DISCONNECTED/TEXT get
    explicit handling here; everything else is delegated to
    logGenericWsEvent() so nothing goes unnoticed. */
void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connection_count++;
      LOG_INFO("Connected (#%u).\n", connection_count);
      session_ready = false;
      // Deferred to loop() rather than sent here: calling sendTXT() directly
      // inside this callback can deadlock the library (discovered in old
      // step 7 - see PROJECT_OVERVIEW.md "Discoveries").
      should_send_session_update = true;
      break;
    case WStype_DISCONNECTED:
      // Distinguishing why matters for a soak test: mid-turn is a real,
      // visible failure (that LISTEN cycle's reply is lost); idle is
      // expected/benign, most likely the server's own idle timeout - no
      // specific duration has been measured yet, which is exactly what step
      // 16 was meant to find out.
      if (is_capturing || awaiting_reply) {
        LOG_ERROR("Disconnected mid-turn (LISTEN #%u lost).\n", listen_cycle);
      } else {
        LOG_INFO("Disconnected (idle) - reconnecting.\n");
        LOG_DEBUG("Disconnected while idle between LISTEN cycles - likely a server idle timeout.\n");
      }
      session_ready = false;
      resetRingBuffer();  // any partial reply audio is now stale - don't play it
      markReplyFailed("disconnected before a reply arrived");
      if (registerDisconnectAndMaybeGiveUp(webSocket)) {
        haltSketch("connection never succeeded after repeated attempts");
      }
      break;
    case WStype_TEXT:
      handleServerMessage(payload, length);
      break;
    default:
      logGenericWsEvent(type, payload, length);
      break;
  }
}

/** Records kRecordDurationSec of mic audio, streaming each ~100ms chunk to the
    session as it's captured. Commits the buffer and requests a response once
    done, then starts the reply timeout. Does NOT touch the LED - the LED is
    the model-controlled set_board_state output, not a "recording in
    progress" indicator (see PROJECT_OVERVIEW.md step 15). */
void captureAndStreamAudio() {
  listen_cycle++;
  is_capturing = true;
  awaiting_reply = false;
  reply_received_this_cycle = false;
  response_fully_received = false;
  function_call_turn_pending = false;
  resetRingBuffer();
  LOG_INFO("LISTEN #%u: recording %us and streaming to OpenAI...\n",
           listen_cycle, kRecordDurationSec);

  uint32_t total_samples = kSampleRateHz * kRecordDurationSec;
  uint32_t samples_sent = 0;
  uint32_t chunks_sent = 0;

  // Independent of whether the model replies, this tells us whether real
  // audio was actually captured - separates "mic/gain problem" from "server
  // never responded" as possible explanations for a FAILED verdict.
  int16_t level_min = INT16_MAX;
  int16_t level_max = INT16_MIN;
  double level_sum_of_squares = 0.0;

  while (samples_sent < total_samples) {
    uint32_t samples_wanted = min(kChunkSamples, total_samples - samples_sent);
    uint32_t samples_read = readMicChunkGained(raw_chunk, pcm_chunk, samples_wanted, kSoftwareGain,
                                                &level_min, &level_max, &level_sum_of_squares);

    size_t base64_len = 0;
    int b64_result = mbedtls_base64_encode(base64_chunk, sizeof(base64_chunk), &base64_len,
                                            reinterpret_cast<const unsigned char*>(pcm_chunk),
                                            samples_read * sizeof(int16_t));
    if (b64_result != 0) {
      LOG_ERROR("base64 encode failed (code %d) - skipping this chunk.\n", b64_result);
      samples_sent += samples_read;
      continue;
    }

    snprintf(json_message, kJsonMessageBufferSize,
             R"JSON({"type":"input_audio_buffer.append","audio":"%.*s"})JSON",
             (int)base64_len, base64_chunk);
    webSocket.sendTXT(json_message);

    samples_sent += samples_read;
    chunks_sent++;
  }

  webSocket.sendTXT(kCommitMessage);
  webSocket.sendTXT(kResponseCreateMessage);
  double level_rms = sqrt(level_sum_of_squares / samples_sent);
  LOG_INFO("Done streaming - waiting for reply...\n");
  LOG_DEBUG("Done streaming (%u chunks). Mic level: min=%d max=%d rms=%.1f\n",
            chunks_sent, level_min, level_max, level_rms);

  is_capturing = false;
  awaiting_reply = true;
  last_reply_activity_ms = millis();
}

/** Checks Serial for a typed LISTEN command and starts a capture if the
    session is ready and idle; otherwise logs why not, without starting one. */
void checkForListenCommand() {
  if (!Serial.available()) {
    return;
  }
  String command = Serial.readStringUntil('\n');
  command.trim();
  if (!command.equalsIgnoreCase("LISTEN")) {
    return;
  }

  if (!session_ready) {
    LOG_INFO("LISTEN ignored - not ready yet (still connecting/setting up).\n");
  } else if (is_capturing || awaiting_reply) {
    LOG_INFO("LISTEN ignored - still recording or playing back a reply.\n");
  } else {
    captureAndStreamAudio();
  }
}

/** Checks whether the current LISTEN cycle has gone quiet for too long - an
    INACTIVITY timeout (no server message at all for kReplyTimeoutMs), not a
    cap on the reply's total duration. A tool-using cycle takes two full
    response round trips and a long spoken reply is paced by playback
    backpressure, both of which can legitimately take well past
    kReplyTimeoutMs from the start of the cycle - what should never happen is
    total silence from the server for that long. (A total-duration cap fired
    while transcript/audio was still actively arriving in a real run - see
    PROJECT_OVERVIEW.md step 16's "reply timeout fired mid-reply" discovery.) */
void checkReplyTimeout() {
  if (!awaiting_reply) {
    return;
  }
  if (millis() - last_reply_activity_ms < kReplyTimeoutMs) {
    return;
  }
  char reason[64];
  snprintf(reason, sizeof(reason), "no server activity for %lu ms", kReplyTimeoutMs);
  markReplyFailed(reason);
}

/** Once the server has said the (post-tool-call, if any) response is fully done
    AND the ring buffer has finished draining to the speaker, the reply has
    genuinely been heard in full - that's this step's actual PASS criterion,
    not just "bytes arrived". */
void checkPlaybackComplete() {
  if (!response_fully_received || !awaiting_reply) {
    return;
  }
  if (ringUsedBytes() > 0) {
    return;  // still playing
  }
  markReplyReceived();
}

/** One-time setup: Wi-Fi, time sync, full-duplex mic+speaker I2S, onboard
    LED, then open the secure WebSocket connection. */
void setup() {
  Serial.begin(115200);
  LOG_INFO("Boot.\n");
  LOG_DEBUG("Free heap at boot (baseline): %u, largest free block: %u\n",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  initPlaybackRingBuffer();
  setupBoardState();
  setupI2sFullDuplex();

  connectToWifi();
  syncSystemTime();

  LOG_INFO("Connecting to OpenAI Realtime API...\n");
  webSocket.onEvent(onWebSocketEvent);
  beginOpenAiRealtimeConnection(webSocket);
}

/** Services the WebSocket connection (unless paused for backpressure - see
    i2s_audio_utils.h's ringShouldPauseReads()), sends session.update once
    flagged ready, sends the one-time opening greeting once flagged ready,
    watches for a typed LISTEN command, drains playback audio to the
    speaker, and checks for a reply timeout / playback completion. This is a
    soak-test sketch by design: it keeps running across as many LISTEN
    cycles and reconnects as it keeps getting fed, and only ever stops if the
    connection itself can never be (re-)established. */
void loop() {
  if (!should_keep_running) {
    return;
  }

  // Backpressure pause/resume can fire many times over one long reply - too
  // frequent to be useful at LOG_INFO (see PROJECT_OVERVIEW.md step 17's
  // logging doctrine), so this is LOG_DEBUG-only.
  bool should_pause = ringShouldPauseReads(ws_reads_paused_for_backpressure);
  if (should_pause != ws_reads_paused_for_backpressure) {
    // LOG_DEBUG's fmt must be a string literal (see log_utils.h) - it's
    // concatenated with the "[LEVEL][%s:%d] " prefix at compile time, so a
    // ternary computed fmt won't compile. Two literal calls instead.
    if (should_pause) {
      LOG_DEBUG("Ring buffer nearly full - pausing WebSocket reads until playback catches up.\n");
    } else {
      LOG_DEBUG("Ring buffer has room again - resuming WebSocket reads.\n");
    }
    ws_reads_paused_for_backpressure = should_pause;
  }
  if (!ws_reads_paused_for_backpressure) {
    webSocket.loop();
  }

  if (should_send_session_update) {
    should_send_session_update = false;
    LOG_DEBUG("Sending session.update...\n");
    webSocket.sendTXT(kSessionUpdateMessage);
  }

  if (should_send_opening_greeting) {
    should_send_opening_greeting = false;
    esp32va_greeting::sendOpeningGreeting(webSocket);
  }

  checkForListenCommand();
  drainRingBufferToSpeaker();
  checkPlaybackComplete();
  checkReplyTimeout();
}
