// Step 29 - Measurements for the presentation: timing, levels and heap per LISTEN
// Builds directly on step 28 (test/28_control_center.ino.inactive), whose
// LISTEN cycle, status lines, device controls, recordings and file transfer
// are unchanged. Adds measurement_utils.h, which only reads millis() and
// counts what the sketch already handles - no behavior change:
// - @@BOOT_TIMING once, on the first session.updated: how long each startup
//   stage took (hardware, SD, Wi-Fi, time sync, TLS + WebSocket connect,
//   session setup), plus heap and Wi-Fi signal at that point.
// - @@TIMING once per LISTEN, right after its verdict: time from the end of
//   recording to the first server message, the input transcript, the first
//   reply text and audio, the tool call (and its round trip), response.done
//   and the end of playback; plus bytes sent and upload speed, reply audio
//   length, backpressure pauses, mic peak/RMS in dBFS and clipped samples,
//   chunk handling time, heap and Wi-Fi signal.
// - @@HEAP at every heap snapshot (heap_diagnostics_utils), so the per-stage
//   heap table is machine-readable too.
// tools/measurements_report.py turns a saved log into a CSV and a summary
// (median / min / max per key).
//
// PASS criterion: "===== MEASUREMENTS PASSED =====" prints once, when the
// first LISTEN passes with every startup check ok - after one @@BOOT_TIMING
// line and that LISTEN's @@TIMING line, both with no -1 in the fields that
// must have happened (every *_ms in @@BOOT_TIMING; first_event, first_audio,
// response_done and playback_done in @@TIMING). It FAILS instead if a startup
// check failed. Then run tools/measurements_report.py on the saved log.
//
// Unchanged from step 28: @@STAGE / @@STATE / @@RESULT / @@MIC_LEVEL /
// @@HEARD / @@SAID / @@WIFI status lines and the STATUS command for the
// laptop app; VOLUME / MUTE / BEEP / LED / LED_BRIGHTNESS / LIGHT_BANDS
// controls kept across reboots; every LISTEN's mic audio saved as
// /voice_commands/command_NNNN.pcm and every reply as /replies/reply_NNNN.ulaw
// (newest 5 of each), with LIST / GET / DELETE / DELETE_ALL, refused with
// @@BUSY while a reply is in progress, playing or being saved. Serial stays
// at 115200 baud.

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
#include "light_sensor_utils.h"
#include "heap_diagnostics_utils.h"
#include "sd_card_utils.h"
#include "reply_recorder_utils.h"
#include "sd_file_transfer_utils.h"
#include "voice_command_recorder_utils.h"
#include "status_report_utils.h"
#include "device_controls_utils.h"
#include "measurement_utils.h"

namespace {
// Unchanged from step 21 except for the get_time_of_day tool and its
// instructions sentence: 24kHz PCM in / 8kHz mu-law out, manual turn
// control, the set_board_state tool, and the one-short-sentence instructions.
const char kSessionUpdateMessage[] =
    R"JSON({"type":"session.update","session":{"type":"realtime","model":")JSON"
    OPENAI_MODEL
    R"JSON(","instructions":"You are a voice assistant on an ESP32 dev board. STRICT RULE: reply in exactly ONE short sentence, no more than about 12 words - never two sentences, never a list, no exceptions even for complex questions. If the full answer would need more than that, say only the single most important part of it and drop the rest - a short incomplete answer is correct, a longer complete one is not. When the user asks you to turn the onboard LED on or off, call the set_board_state function with the requested led_on value - don't just say you did it. STRICT RULE: only call set_board_state if THIS specific message clearly and explicitly asks for a board state change - never call it because of an earlier request, a repeated pattern from the conversation so far, or silence/unclear audio; if you did not clearly hear a specific request in this turn, do not call the function and do not assume one was intended. When the user asks about the current time of day - for example what part of the day it is, or whether it is day, evening or night - call the get_time_of_day function and answer from its result, e.g. 'It is currently evening.' Never guess the time of day and never reuse an earlier get_time_of_day result: call it again for every such question, because the light level it is based on changes over time. It only tells day, evening or night, not the clock time.","output_modalities":["audio"],"audio":{"input":{"format":{"type":"audio/pcm","rate":24000},"turn_detection":null,"transcription":{"model":"gpt-4o-mini-transcribe"}},"output":{"format":{"type":"audio/pcmu"},"voice":"alloy"}},"tools":[{"type":"function","name":"set_board_state","description":"Turns the ESP32 dev board's onboard LED on or off.","parameters":{"type":"object","properties":{"led_on":{"type":"boolean","description":"true to turn the LED on, false to turn it off"}},"required":["led_on"]}},{"type":"function","name":"get_time_of_day","description":"Returns the current part of the day - day, evening or night - judged from the board's ambient light sensor. Not a clock time.","parameters":{"type":"object","properties":{}}}],"tool_choice":"auto","reasoning":{"effort":"low"}}})JSON";

const char kCommitMessage[] = R"JSON({"type":"input_audio_buffer.commit"})JSON";
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";

constexpr uint32_t kSerialBaudRate = 115200;  // must match tools/serial_logger.ps1 and tools/reply_audio_app.html
constexpr uint32_t kSampleRateHz = 24000;   // must match kSessionUpdateMessage's input rate and i2s_audio_utils's I2S rate
constexpr uint32_t kRecordDurationSec = 3;  // same duration old step 8b/step 12 used
constexpr uint32_t kChunkSamples = 2400;    // ~100ms per chunk at 24kHz - same chunk duration step 12 confirmed working
constexpr uint32_t kChunkMs = kChunkSamples * 1000 / kSampleRateHz;
// Mic levels are quiet without any gain (step 3). x4 until 2026-10-06, when a
// spoken "1, 2, 3" hit full scale (min -32768 / max 32767) - clipped before
// OpenAI heard it. Must match tools/reply_audio_app.html's BOARD_MIC_GAIN.
constexpr int32_t kSoftwareGain = 2;
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
bool step_verdict_reported = false;  // this step's own control-center verdict - see reportStepVerdict()
bool speech_in_progress = false;  // reply (or greeting) audio has started arriving and hasn't finished playing yet
constexpr size_t kToolNameBufferSize = 32;
char active_tool_name[kToolNameBufferSize] = "";  // the tool call being handled, for @@STATE tool <name>
constexpr size_t kStateDetailBufferSize = 64;
}  // namespace

/** Reports this step's verdict exactly once: a final heap snapshot, the heap
    summary table, then the PASSED/FAILED banner. Later calls are no-ops, so
    e.g. a reconnect that fails long after a PASS never contradicts it.
    failure_reason is ignored when passed is true. Banners are never gated
    behind a log level - see log_utils.h. */
void reportStepVerdict(bool passed, const char* failure_reason) {
  if (step_verdict_reported) return;
  step_verdict_reported = true;
  esp32va_heap::recordHeapSnapshot("at verdict");
  esp32va_heap::logHeapSummary();
  if (passed) {
    Serial.println("===== MEASUREMENTS PASSED =====");
    Serial.println("Check the @@BOOT_TIMING and @@TIMING lines above, then keep running LISTEN cycles to collect more.");
  } else {
    Serial.printf("===== MEASUREMENTS FAILED: %s =====\n", failure_reason);
  }
}

/** Tells the laptop app about a reply recording that just closed, if a file
    was kept (the reply recorder logs the outcome itself). */
void handleFinishedRecording(const esp32va_reply_recorder::FinishedRecording& finished) {
  if (finished.bytes_written > 0) {
    esp32va_sd_transfer::announceSavedRecording(finished.file_name, finished.bytes_written);
  }
}

/** Closes this LISTEN's mic recording and tells the laptop app about it.
    A failed save is logged by the recorder itself (step 27 settled that
    feature; it no longer decides this step's verdict). */
void finishVoiceCommandRecording() {
  esp32va_command_recorder::FinishedVoiceCommand finished;
  if (!esp32va_command_recorder::finishVoiceCommand(&finished)) {
    return;
  }
  esp32va_sd_transfer::announceSavedRecording(finished.file_name, finished.bytes_written);
}

/** The first startup check that didn't end ok, as a short phrase for the
    FAILED banner - or nullptr if they all did. */
const char* firstFailedStartupCheck() {
  using esp32va_status::Stage;
  if (!esp32va_status::isStageOk(Stage::kSdCard)) return "the SD card check didn't pass";
  if (!esp32va_status::isStageOk(Stage::kMicrophone)) return "the mic check didn't pass - see the ERROR above";
  if (!esp32va_status::isStageOk(Stage::kSpeaker)) return "the speaker setup didn't pass";
  if (!esp32va_status::isStageOk(Stage::kWifi)) return "Wi-Fi wasn't connected";
  if (!esp32va_status::isStageOk(Stage::kTimeSync)) return "the time sync didn't pass";
  if (!esp32va_status::isStageOk(Stage::kOpenAi)) return "the OpenAI session wasn't ready";
  return nullptr;
}

/** Marks this LISTEN cycle as having gotten a reply, logging the verdict exactly once per cycle.
    Verdicts are never gated behind a log level - see log_utils.h. */
void markReplyReceived() {
  if (reply_received_this_cycle) return;
  reply_received_this_cycle = true;
  awaiting_reply = false;
  Serial.printf("===== LISTEN #%u PASSED =====\n", listen_cycle);
  Serial.println("Type LISTEN to continue the conversation.");
  esp32va_status::printProtocolLine("@@RESULT pass %u", listen_cycle);
  esp32va_measure::reportTurn(true);
  const char* failed_check = firstFailedStartupCheck();
  reportStepVerdict(failed_check == nullptr, failed_check);
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
  esp32va_status::printProtocolLine("@@RESULT fail %u %s", listen_cycle, reason);
  esp32va_measure::reportTurn(false);
  if (session_ready) {
    Serial.println("Type LISTEN to try again.");
  }
}

/** Halts the sketch entirely - reserved for a non-transient problem (the SD card
    being unusable, or the connection never succeeding - see openai_realtime_utils's
    attempt cap), not for a single LISTEN cycle failing. Fails this step's verdict
    too, unless it already passed. Never gated behind a log level - see log_utils.h. */
void haltSketch(const char* reason) {
  should_keep_running = false;
  reportStepVerdict(false, reason);
  Serial.printf("===== HALTED: %s =====\n", reason);
  esp32va_status::reportPipelineState(esp32va_status::PipelineState::kHalted, reason);
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
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstServerEvent);
  }

  char type_buf[48];
  bool have_type = extractEventType(payload, length, type_buf, sizeof(type_buf));

  if (have_type && strcmp(type_buf, "response.output_audio.delta") == 0) {
    const uint8_t* value_start = nullptr;
    size_t value_len = 0;
    if (findRawStringField(payload, length, "delta", &value_start, &value_len)) {
      esp32va_reply_recorder::noteReplyAudioArriving();  // before the ring write, so the tap catches it
      speech_in_progress = true;
      if (awaiting_reply) {
        esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstAudio);
        esp32va_measure::addReplyAudio(value_len);
      }
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
      // The TLS handshake has already peaked and released its temporary
      // buffers by now, so the low-water mark is what it cost at its worst.
      esp32va_heap::recordHeapSnapshot("OpenAI session ready");
      LOG_INFO("Connected with the SD card mounted - lowest free heap since boot: %u bytes.\n",
               (unsigned)esp32va_heap::lowestFreeHeapSinceBootBytes());
      Serial.printf("Ready - type LISTEN to talk.\n");
      esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kOk, "session ready");
      LOG_DEBUG("session.updated received (first connect).\n");
      // Deferred to loop() rather than sent here - same reasoning as
      // should_send_session_update below (sendTXT() from inside this
      // callback can deadlock the library).
      should_send_opening_greeting = true;
      esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kSessionReady);
      esp32va_measure::reportBootTiming();
      // A later mid-conversation disconnect gets its own fresh attempt
      // budget, rather than one shared with however many retries startup
      // itself needed.
      resetReconnectBackoff();
    } else {
      LOG_INFO("Reconnected (#%u) - conversation context lost. Resuming at LISTEN #%u.\n",
               connection_count - 1, listen_cycle + 1);
      esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kOk,
                                  "reconnected - the conversation started fresh");
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
    const char* function_name = doc["name"] | "(unknown)";
    LOG_DEBUG("Event type: response.function_call_arguments.done -> %s(%s)\n",
              function_name, (const char*)(doc["arguments"] | "{}"));
    bool handled = false;
    snprintf(active_tool_name, sizeof(active_tool_name), "%s", function_name);
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kToolCall);
    esp32va_measure::setToolName(function_name);
    if (strcmp(function_name, "set_board_state") == 0) {
      handled = handleSetBoardStateCall(doc, webSocket);
    } else if (strcmp(function_name, "get_time_of_day") == 0) {
      handled = esp32va_light_sensor::handleGetTimeOfDayCall(doc, webSocket);
    } else {
      LOG_ERROR("Unhandled function call: %s\n", function_name);
    }
    if (handled) {
      function_call_turn_pending = true;
    }
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.delta") == 0) {
    // Raw streamed content (the model's own spoken-reply transcript) - always
    // printed, never gated behind a log level. See log_utils.h.
    const char* delta = doc["delta"] | "";
    if (awaiting_reply) {
      esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstTranscript);
    }
    if (!transcript_in_progress) {
      transcript_in_progress = true;
      Serial.print("Streaming transcript: ");
      esp32va_status::setRawLineOpen(true);
    }
    Serial.print(delta);
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.done") == 0) {
    if (transcript_in_progress) {
      Serial.println();
      transcript_in_progress = false;
      esp32va_status::setRawLineOpen(false);
    }
    // Not logged at LOG_INFO: this is a full re-print of content already
    // shown live via the streamed deltas above (raw, ungated) - repeating it
    // here would just be noise. Kept at LOG_DEBUG for cross-checking the
    // assembled transcript against what streamed in piece by piece.
    const char* transcript = doc["transcript"] | (const char*)nullptr;
    LOG_DEBUG("response.output_audio_transcript.done -> full transcript: \"%s\"\n",
              transcript != nullptr ? transcript : "(none)");
    if (transcript != nullptr) {
      esp32va_status::reportTextLine("SAID", transcript);  // the app's chat bubble
    }
    return;
  }

  if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
    // Best-effort: a separate ASR model, not the realtime model itself,
    // running asynchronously - see header comment. Never affects the
    // LISTEN cycle's verdict, which depends only on the spoken reply.
    const char* transcript = doc["transcript"] | (const char*)nullptr;
    Serial.printf("You said: \"%s\"\n", transcript != nullptr ? transcript : "(none)");
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kHeardTranscript);
    esp32va_measure::setHeardLength(transcript != nullptr ? strlen(transcript) : 0);
    if (transcript != nullptr) {
      esp32va_status::reportTextLine("HEARD", transcript);  // the app's chat bubble
    }
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
    // Whatever the status, no more audio is coming for this response. A
    // function-call-only response.done has no recording open - a no-op.
    esp32va_reply_recorder::noteReplyFinished();
    if (strcmp(status, "failed") == 0) {
      const char* error_message = doc["response"]["status_details"]["error"]["message"] | "(no error detail)";
      markReplyFailed(error_message);
    } else if (function_call_turn_pending) {
      // This response.done was for the function-call-only turn (no audio) -
      // we already sent function_call_output and a follow-up response.create
      // in handleSetBoardStateCall(). The *next* response.done is the
      // model's real spoken confirmation, which does count as the reply.
      function_call_turn_pending = false;
      esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kToolResponseDone);
      LOG_INFO("Function call done - waiting for spoken confirmation.\n");
    } else {
      // The PASS verdict itself waits for the ring buffer to actually finish
      // draining to the speaker - see checkPlaybackComplete() in loop().
      // (Also reached for the opening greeting's response.done - harmless,
      // since awaiting_reply is false then, so checkPlaybackComplete() stays
      // a no-op until the next real LISTEN cycle resets this flag anyway.)
      response_fully_received = true;
      if (awaiting_reply) {
        esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kResponseDone);
      }
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
      esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kSocketConnected);  // first connect only
      LOG_INFO("Connected (#%u).\n", connection_count);
      session_ready = false;
      esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive,
                                  "setting up the session");
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
      esp32va_reply_recorder::noteReplyAborted();  // keeps what was saved; must come before the ring reset
      resetRingBuffer();  // any partial reply audio is now stale - don't play it
      markReplyFailed("disconnected before a reply arrived");
      if (registerDisconnectAndMaybeGiveUp(webSocket)) {
        esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kFail,
                                    "couldn't connect - check the API key and internet access");
        haltSketch("connection never succeeded after repeated attempts");
      } else {
        char stage_detail[kStateDetailBufferSize];
        snprintf(stage_detail, sizeof(stage_detail), "disconnected - reconnecting (attempt %u of %u)",
                 (unsigned)esp32va_openai::reconnectAttemptCount(), (unsigned)esp32va_openai::maxConnectAttempts());
        esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive,
                                    stage_detail);
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

/** Prints "@@MIC_LEVEL <dBFS>" for one chunk of (gained) mic audio - the
    level OpenAI hears - for the laptop app's live meter. Returns how many of
    its samples sit at full scale (clipped by the gain), for @@TIMING. */
uint32_t reportChunkMicLevel(const int16_t* samples, uint32_t count) {
  if (count == 0) {
    return 0;
  }
  double sum_of_squares = 0.0;
  uint32_t clipped_samples = 0;
  for (uint32_t i = 0; i < count; i++) {
    sum_of_squares += static_cast<double>(samples[i]) * samples[i];
    if (samples[i] == INT16_MAX || samples[i] == INT16_MIN) {
      clipped_samples++;
    }
  }
  double rms = sqrt(sum_of_squares / count);
  int level_dbfs = rms >= 1.0 ? static_cast<int>(lround(20.0 * log10(rms / 32768.0))) : -96;
  esp32va_status::printProtocolLine("@@MIC_LEVEL %d", level_dbfs);
  return clipped_samples;
}

/** Records kRecordDurationSec of mic audio, streaming each ~100ms chunk to the
    session as it's captured and saving the mic's own samples to the SD card
    (voice_command_recorder_utils.h). Commits the buffer and requests a
    response once done, then closes the recording and starts the reply
    timeout. Also checks whether handling each chunk kept up with the mic.
    Does NOT touch the LED - the LED is the model-controlled set_board_state
    output, not a "recording in progress" indicator (see PROJECT_OVERVIEW.md
    step 15). */
void captureAndStreamAudio() {
  listen_cycle++;
  esp32va_measure::beginTurn(listen_cycle);
  is_capturing = true;
  awaiting_reply = false;
  reply_received_this_cycle = false;
  response_fully_received = false;
  function_call_turn_pending = false;
  resetRingBuffer();
  speech_in_progress = false;
  char listen_detail[kStateDetailBufferSize];
  snprintf(listen_detail, sizeof(listen_detail), "%u", (unsigned)kRecordDurationSec);
  esp32va_status::reportPipelineState(esp32va_status::PipelineState::kListening, listen_detail);
  esp32va_command_recorder::beginVoiceCommand();
  Serial.printf("LISTEN #%u: recording %us and streaming to OpenAI...\n",
           listen_cycle, kRecordDurationSec);

  uint32_t total_samples = kSampleRateHz * kRecordDurationSec;
  uint32_t samples_sent = 0;
  uint32_t chunks_sent = 0;
  uint32_t chunks_read = 0;
  unsigned long slowest_chunk_handling_ms = 0;
  unsigned long total_chunk_handling_ms = 0;
  uint32_t sent_json_bytes = 0;
  unsigned long total_send_ms = 0;
  uint32_t clipped_samples = 0;

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
    // From here until the next read, the mic keeps filling its buffer.
    unsigned long handling_started_ms = millis();
    clipped_samples += reportChunkMicLevel(pcm_chunk, samples_read);

    size_t base64_len = 0;
    int b64_result = mbedtls_base64_encode(base64_chunk, sizeof(base64_chunk), &base64_len,
                                            reinterpret_cast<const unsigned char*>(pcm_chunk),
                                            samples_read * sizeof(int16_t));
    if (b64_result == 0) {
      int json_len = snprintf(json_message, kJsonMessageBufferSize,
                              R"JSON({"type":"input_audio_buffer.append","audio":"%.*s"})JSON",
                              (int)base64_len, base64_chunk);
      unsigned long send_started_ms = millis();
      webSocket.sendTXT(json_message);
      total_send_ms += millis() - send_started_ms;
      sent_json_bytes += json_len > 0 ? static_cast<uint32_t>(json_len) : 0;
      chunks_sent++;
    } else {
      LOG_ERROR("base64 encode failed (code %d) - skipping this chunk.\n", b64_result);
    }
    esp32va_command_recorder::appendMicChunk(raw_chunk, samples_read);  // overwrites raw_chunk
    samples_sent += samples_read;
    chunks_read++;

    unsigned long handling_ms = millis() - handling_started_ms;
    slowest_chunk_handling_ms = max(slowest_chunk_handling_ms, handling_ms);
    total_chunk_handling_ms += handling_ms;
  }

  webSocket.sendTXT(kCommitMessage);
  webSocket.sendTXT(kResponseCreateMessage);
  esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kRecordingDone);
  double level_rms = sqrt(level_sum_of_squares / samples_sent);
  Serial.printf("Done streaming - waiting for reply...\n");
  LOG_DEBUG("Done streaming (%u chunks). Mic level: min=%d max=%d rms=%.1f\n",
            chunks_sent, level_min, level_max, level_rms);

  // A chunk lasts kChunkMs; the mic's buffer holds micBufferCapacityMs().
  // One chunk taking longer than the buffer, or chunks taking longer than
  // they last on average, means mic audio was dropped between reads.
  unsigned long average_chunk_handling_ms = chunks_read > 0 ? total_chunk_handling_ms / chunks_read : 0;
  uint32_t mic_buffer_ms = micBufferCapacityMs();
  LOG_INFO("Mic timing: handling a %u ms chunk took %lu ms at most, %lu ms on average (the mic buffer holds %u ms).\n",
           (unsigned)kChunkMs, slowest_chunk_handling_ms, average_chunk_handling_ms, (unsigned)mic_buffer_ms);
  int32_t negative_peak = -static_cast<int32_t>(level_min);
  int32_t positive_peak = static_cast<int32_t>(level_max);
  int32_t peak_abs_sample = negative_peak > positive_peak ? negative_peak : positive_peak;
  esp32va_measure::setCaptureStats(sent_json_bytes, total_send_ms, peak_abs_sample, level_rms, clipped_samples,
                                   slowest_chunk_handling_ms, average_chunk_handling_ms);
  if (slowest_chunk_handling_ms >= mic_buffer_ms || average_chunk_handling_ms >= kChunkMs) {
    LOG_ERROR("Mic audio was probably dropped this LISTEN - handling couldn't keep up with the mic.\n");
  }
  finishVoiceCommandRecording();

  is_capturing = false;
  awaiting_reply = true;
  last_reply_activity_ms = millis();
}

/** True when a LIST/GET may run now: no turn in progress, nothing left to
    play, nothing left to save. A GET holds loop() for its whole transfer, so
    it must never overlap playback or recording. */
bool isIdleForFileTransfer() {
  return !is_capturing && !awaiting_reply && ringUsedBytes() == 0 && esp32va_reply_recorder::isRecorderIdle();
}

/** Prints everything the laptop app shows about the board right now: every
    startup check, the assistant's state, Wi-Fi signal, volume, LED and the
    light sensor - the answer to STATUS. */
void reprintFullStatus() {
  esp32va_status::reprintStatusSnapshot();
  esp32va_wifi::reportWifiSignal();
  esp32va_controls::announceControlStates();
  esp32va_light_sensor::announceLightState();
}

/** Checks Serial for a typed command. LIST / GET / DELETE <name> / DELETE_ALL answer the laptop
    app (see sd_file_transfer_utils.h) - with @@BUSY unless idle. STATUS
    reprints the board's whole status (status_report_utils.h). VOLUME / MUTE
    / BEEP / LED / LED_BRIGHTNESS control the hardware
    (device_controls_utils.h), LIGHT [<value>] / LIGHT_BANDS set/show the
    simulated light sensor (light_sensor_utils.h) - all at any time.
    LISTEN starts a capture if the session is ready and idle and the last
    reply is fully saved; otherwise logs why not, without starting one.
    Anything else is logged and ignored. */
void checkForSerialCommand() {
  if (!Serial.available()) {
    return;
  }
  String command = Serial.readStringUntil('\n');
  command.trim();
  if (command.length() == 0) {
    return;
  }
  if (esp32va_sd_transfer::handleFileCommand(command.c_str(), isIdleForFileTransfer())) {
    return;
  }
  if (command.equalsIgnoreCase("STATUS")) {
    reprintFullStatus();
    return;
  }
  if (esp32va_controls::handleControlCommand(command.c_str(), isIdleForFileTransfer())) {
    return;
  }
  if (esp32va_light_sensor::handleLightCommand(command.c_str())) {
    return;
  }
  if (!command.equalsIgnoreCase("LISTEN")) {
    Serial.printf("Unknown command \"%s\" - expected LISTEN, STATUS, LIGHT <0-%d>, LIGHT_BANDS <evening> <day>, VOLUME <0-100>, MUTE on|off, BEEP, LED on|off, LED_BRIGHTNESS <0-100>, LIST, GET <name>, DELETE <name> or DELETE_ALL [replies|commands].\n",
             command.c_str(), esp32va_light_sensor::kMaxLightSensorValue);
    return;
  }

  if (!should_keep_running) {
    Serial.printf("LISTEN ignored - the sketch has halted (see the HALTED line above).\n");
  } else if (!session_ready) {
    Serial.printf("LISTEN ignored - not ready yet (still connecting/setting up).\n");
  } else if (is_capturing || awaiting_reply) {
    Serial.printf("LISTEN ignored - still recording or playing back a reply.\n");
  } else if (!esp32va_reply_recorder::isRecorderIdle()) {
    Serial.printf("LISTEN ignored - still saving the last reply to the SD card, try again in a moment.\n");
  } else {
    captureAndStreamAudio();
  }
}

/** Lets the recorder do its SD work for this loop() pass, and settles this
    step's verdict once a recording closes. */
void serviceReplyRecording() {
  esp32va_reply_recorder::FinishedRecording finished_recording;
  if (esp32va_reply_recorder::serviceReplyRecorder(&finished_recording)) {
    handleFinishedRecording(finished_recording);
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
  esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kPlaybackDone);
  markReplyReceived();
}

/** Works out what the assistant is doing from this sketch's flags and
    reports it as @@STATE when it changes (status_report_utils.h). Called
    every loop() pass; captureAndStreamAudio() reports listening itself,
    since it holds loop() for the whole recording. */
void updatePipelineState() {
  using esp32va_status::PipelineState;
  if (!should_keep_running) {
    return;  // haltSketch() already reported halted
  }
  if (!session_ready) {
    if (!session_ever_ready) {
      esp32va_status::reportPipelineState(PipelineState::kBooting, nullptr);
      return;
    }
    char reconnect_detail[kStateDetailBufferSize];
    snprintf(reconnect_detail, sizeof(reconnect_detail), "attempt %u of %u",
             (unsigned)esp32va_openai::reconnectAttemptCount(), (unsigned)esp32va_openai::maxConnectAttempts());
    esp32va_status::reportPipelineState(PipelineState::kReconnecting, reconnect_detail);
    return;
  }
  // Speech is over once nothing is left to play and either the server said
  // the response is done or no LISTEN is waiting on it (the opening greeting,
  // or a cycle that already failed).
  if (speech_in_progress && ringUsedBytes() == 0 && (response_fully_received || !awaiting_reply)) {
    speech_in_progress = false;
  }
  if (function_call_turn_pending) {
    esp32va_status::reportPipelineState(PipelineState::kTool, active_tool_name);
  } else if (speech_in_progress) {
    esp32va_status::reportPipelineState(PipelineState::kSpeaking, awaiting_reply ? nullptr : "greeting");
  } else if (awaiting_reply) {
    esp32va_status::reportPipelineState(PipelineState::kProcessing, nullptr);
  } else if (!esp32va_reply_recorder::isRecorderIdle()) {
    esp32va_status::reportPipelineState(PipelineState::kSaving, nullptr);
  } else {
    esp32va_status::reportPipelineState(PipelineState::kReady, nullptr);
  }
}

/** One-time setup: playback ring buffer, full-duplex mic+speaker I2S, onboard
    LED, (simulated) light sensor, SD card mount + self-test + reply recorder,
    Wi-Fi, time sync, then open the secure WebSocket connection - with a heap
    snapshot after each stage, and each startup check reported as @@STAGE
    for the laptop app. Also checks the mic and loads the saved volume /
    brightness. Halts (step FAILED) if the SD card isn't usable. */
void setup() {
  Serial.begin(kSerialBaudRate);
  esp32va_heap::startCountingFailedAllocations();
  LOG_INFO("Boot.\n");
  // Every check starts out pending - this also resets a laptop app that was
  // already connected when the board restarted.
  esp32va_status::reportPipelineState(esp32va_status::PipelineState::kBooting, nullptr);
  esp32va_status::reprintStatusSnapshot();
  esp32va_heap::recordHeapSnapshot("boot");

  initPlaybackRingBuffer();
  setupBoardState();
  esp32va_light_sensor::setupLightSensor();
  esp32va_light_sensor::announceLightState();
  esp32va_status::reportStage(esp32va_status::Stage::kMicrophone, esp32va_status::StageState::kActive, nullptr);
  setupI2sFullDuplex();
  esp32va_controls::setupDeviceControls();
  esp32va_controls::announceControlStates();
  esp32va_status::reportStage(esp32va_status::Stage::kSpeaker, esp32va_status::StageState::kOk,
                              "ready - check it by ear with Test beep");
  char stage_detail[kStateDetailBufferSize];
  bool mic_ok = checkMicrophoneSignal(stage_detail, sizeof(stage_detail));
  esp32va_status::reportStage(esp32va_status::Stage::kMicrophone,
                              mic_ok ? esp32va_status::StageState::kOk : esp32va_status::StageState::kFail,
                              stage_detail);
  esp32va_heap::recordHeapSnapshot("after ring buffer + I2S");
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kHardwareReady);

  esp32va_status::reportStage(esp32va_status::Stage::kSdCard, esp32va_status::StageState::kActive, "mounting");
  bool sd_card_usable = esp32va_sd_card::mountSdCard() && esp32va_sd_card::runSdSelfTest() &&
                        esp32va_reply_recorder::setupReplyRecorder(esp32va_sd_transfer::announceDeletedRecording) &&
                        esp32va_command_recorder::setupVoiceCommandRecorder(
                            esp32va_sd_transfer::announceDeletedRecording);
  esp32va_heap::recordHeapSnapshot("after SD + recorder setup");
  if (!sd_card_usable) {
    esp32va_status::reportStage(esp32va_status::Stage::kSdCard, esp32va_status::StageState::kFail,
                                "not usable - no card, loose wiring, or not FAT32");
    haltSketch("SD card not usable - see the error above");
    return;
  }
  char sd_summary[kStateDetailBufferSize / 2];
  esp32va_sd_card::describeSdCard(sd_summary, sizeof(sd_summary));
  snprintf(stage_detail, sizeof(stage_detail), "%s, self-test passed", sd_summary);
  esp32va_status::reportStage(esp32va_status::Stage::kSdCard, esp32va_status::StageState::kOk, stage_detail);
  // Now, not on request: loop() - which answers LIST - only starts after
  // Wi-Fi and time sync, 10+ s from here.
  esp32va_sd_transfer::announceRecordingList();
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kSdReady);

  connectToWifi();
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kWifiConnected);
  syncSystemTime();
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kTimeSynced);
  esp32va_heap::recordHeapSnapshot("after Wi-Fi + time sync");

  Serial.printf("Connecting to OpenAI Realtime API...\n");
  esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive, "connecting");
  webSocket.onEvent(onWebSocketEvent);
  beginOpenAiRealtimeConnection(webSocket);
}

/** Services the WebSocket connection (unless paused for backpressure - see
    i2s_audio_utils.h's ringShouldPauseReads()), sends session.update once
    flagged ready, sends the one-time opening greeting once flagged ready,
    watches for a typed command, drains playback audio to the speaker, saves
    reply audio to the SD card, checks for a reply timeout / playback
    completion, and keeps the laptop app's Wi-Fi and assistant-state
    display current. This is a soak-test sketch by design: it keeps running across
    as many LISTEN cycles and reconnects as it keeps getting fed, and only
    ever stops if the SD card is unusable or the connection itself can never
    be (re-)established - and even then keeps answering LIST/GET. */
void loop() {
  if (!should_keep_running) {
    serviceReplyRecording();  // closes a recording cut short by the halt
    checkForSerialCommand();
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
      esp32va_measure::noteBackpressurePause();
      LOG_DEBUG("Ring buffer nearly full - pausing WebSocket reads until playback and the recorder catch up.\n");
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

  checkForSerialCommand();
  drainRingBufferToSpeaker();
  serviceReplyRecording();
  checkPlaybackComplete();
  checkReplyTimeout();
  esp32va_wifi::monitorWifiConnection();
  updatePipelineState();
}
