// Step 30 - Hands-free listening: speech starts and ends the recording by itself
// Builds directly on step 29 (test/29_measurements.ino.inactive): the same
// session, tools, status lines, controls, recordings and measurements. Adds:
// - A mic task of its own (mic_capture_utils.h): the mic is read every 20 ms
//   whatever loop() is busy with, into the playback ring buffer (borrowed
//   while nothing plays; ~1.36 s of mu-law audio), so a slow WebSocket send
//   or SD write delays the audio instead of losing it - step 29 lost some in
//   most LISTENs. LISTEN and the Talk button record through it too, still a
//   fixed 3 s.
// - Hands-free listening (hands_free_utils.h, "VAD on"): while the board is
//   idle, a voice activity detector (vad_utils.h) waits for speech - voiced,
//   syllable-paced speech, not a door slam, a knock or an air conditioner -
//   and records from 500 ms before it was sure (so the first word isn't
//   cut) until ~1 s of silence (VAD_HANGOVER) or 10 s at most. The audio
//   streams to OpenAI as it's captured, as before; an utterance that turns
//   out not to be speech is cleared (input_audio_buffer.clear) instead of
//   committed, and its recording deleted. Half-duplex: it never listens
//   while the board is answering, and waits 600 ms after (the speaker's
//   echo); a BEEP or a file transfer stops it first.
// - Status lines for the laptop app: @@STATE armed (waiting for speech) and
//   @@STATE listening vad, @@VAD / @@VAD_LEVEL / @@VAD_EVENT (see
//   hands_free_utils.h), and new @@TIMING keys: mode, preroll_ms, speech_ms,
//   eos_to_commit_ms (end of speech -> commit), fifo_max_ms, dropped_ms,
//   mic_gap_max_ms (see measurement_utils.h).
// tools/vad_replay.cpp runs the same detector on a laptop, over recordings
// downloaded from the SD card, to see and tune where it starts and stops.
//
// PASS criterion: "===== HANDS-FREE PASSED =====" prints once, after
// kHandsFreeTurnsToPass LISTENs in a row that started by voice, were each
// answered, and lost no mic audio - with every startup check ok and the
// lowest free heap since boot at least kMinLowestHeapBytes. (Manual LISTENs
// in between neither count nor break the streak.) It FAILS instead if a
// startup check failed or the heap went lower. The rest - no trigger on
// knocks, slams or a running fan, none on the board's own voice - is
// checked by ear; see this step's "What to expect?".
//
// Unchanged from step 29: @@STAGE / @@STATE / @@RESULT / @@MIC_LEVEL /
// @@HEARD / @@SAID / @@WIFI status lines and the STATUS command for the
// laptop app; VOLUME / MUTE / BEEP / LED / LED_BRIGHTNESS / LIGHT_BANDS
// controls kept across reboots; every LISTEN's mic audio saved as
// /voice_commands/command_NNNN.pcm and every reply as /replies/reply_NNNN.ulaw
// (newest 5 of each), with LIST / GET / DELETE / DELETE_ALL, refused with
// @@BUSY while a reply is in progress, playing or being saved; @@BOOT_TIMING,
// @@TIMING and @@HEAP. Serial stays at 115200 baud.

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <math.h>
#include <string.h>
#include <strings.h>
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
#include "mic_capture_utils.h"
#include "hands_free_utils.h"
#include "vad_utils.h"
#include "clock_timer_utils.h"
#include "board_info_utils.h"

namespace {
// Unchanged from step 21 except for the get_time_of_day, set_volume,
// get_current_time, set_timer and get_board_status tools and their instructions sentences: 24kHz PCM in / 8kHz mu-law out, manual turn
// control, the set_board_state tool, and the one-short-sentence instructions.
const char kSessionUpdateMessage[] =
    R"JSON({"type":"session.update","session":{"type":"realtime","model":")JSON"
    OPENAI_MODEL
    R"JSON(","instructions":"You are a voice assistant on an ESP32 dev board. STRICT RULE: reply in exactly ONE short sentence, no more than about 12 words - never two sentences, never a list, no exceptions even for complex questions. If the full answer would need more than that, say only the single most important part of it and drop the rest - a short incomplete answer is correct, a longer complete one is not. When the user asks you to turn the onboard LED on or off, call the set_board_state function with the requested led_on value - don't just say you did it. For a brightness (e.g. 'dim the light to 30 percent'), also pass brightness_percent, with led_on true. The LED has one color and can't blink. STRICT RULE: only call set_board_state if THIS specific message clearly and explicitly asks for a board state change - never call it because of an earlier request, a repeated pattern from the conversation so far, or silence/unclear audio; if you did not clearly hear a specific request in this turn, do not call the function and do not assume one was intended. When the user asks about the current time of day - for example what part of the day it is, or whether it is day, evening or night - call the get_time_of_day function and answer from its result, e.g. 'It is currently evening.' Never guess the time of day and never reuse an earlier get_time_of_day result: call it again for every such question, because the light level it is based on changes over time. It only tells day, evening or night, not the clock time. When the user asks to change the speaker volume, call set_volume - with percent for a specific level (e.g. 'set the volume to 30 percent' -> 30), or with step up or down for just louder or quieter - and only when THIS message clearly asks for it. When the user asks what time or date it is, or how long is left on the timer, call get_current_time and answer from its result. When the user asks for a timer or a reminder in some time, call set_timer with the total number of seconds (seconds 0 cancels the timer), and with label if the user said what it is for (e.g. 'remind me in 5 minutes to check the oven' -> 300, check the oven); the board rings and says it by itself when it is done. To switch the light later (e.g. 'turn off the light in 10 minutes'), call only set_timer with light off (or on) - don't switch it now. When the user asks how you or the board are doing, or about the Wi-Fi signal, uptime or memory, call get_board_status and answer from its result. If the user asks what you can do, say in one sentence that you answer questions and can switch the light, change the volume, set timers and reminders, switch the light later, tell the time or the part of the day, and report how the board is doing.","output_modalities":["audio"],"audio":{"input":{"format":{"type":"audio/pcm","rate":24000},"turn_detection":null,"transcription":{"model":"gpt-4o-mini-transcribe"}},"output":{"format":{"type":"audio/pcmu"},"voice":"alloy"}},"tools":[{"type":"function","name":"set_board_state","description":"Turns the ESP32 dev board's onboard LED on or off, optionally at a brightness.","parameters":{"type":"object","properties":{"led_on":{"type":"boolean","description":"true to turn the LED on, false to turn it off"},"brightness_percent":{"type":"integer","description":"how bright, 0 to 100 percent - only when the user asked for a brightness"}},"required":["led_on"]}},{"type":"function","name":"get_time_of_day","description":"Returns the current part of the day - day, evening or night - judged from the board's ambient light sensor. Not a clock time.","parameters":{"type":"object","properties":{}}},{"type":"function","name":"set_volume","description":"Sets the board's speaker volume, and unmutes it. Give percent for a specific level, or step for louder/quieter by 20 percent.","parameters":{"type":"object","properties":{"percent":{"type":"integer","description":"the requested volume in percent, 0 to 100"},"step":{"type":"string","enum":["up","down"],"description":"up for louder, down for quieter - only when no specific percent was asked"}}}},{"type":"function","name":"get_current_time","description":"Returns the current local date and clock time from the board's clock, and how long a running timer has left.","parameters":{"type":"object","properties":{}}},{"type":"function","name":"get_board_status","description":"Returns the board's own health: Wi-Fi signal strength, how long it has been on, and free memory.","parameters":{"type":"object","properties":{}}},{"type":"function","name":"set_timer","description":"Starts a timer on the board that beeps and blinks the LED when it is done. 0 cancels a running timer.","parameters":{"type":"object","properties":{"seconds":{"type":"integer","description":"how long until it rings, in seconds (1 to 3600), or 0 to cancel"},"label":{"type":"string","description":"what it is for, in a few words, e.g. check the oven - only if the user said"},"light":{"type":"string","enum":["on","off"],"description":"switch the LED on or off when it rings - only when the user asked for the light to change later"}},"required":["seconds"]}}],"tool_choice":"auto","reasoning":{"effort":"low"}}})JSON";

const char kCommitMessage[] = R"JSON({"type":"input_audio_buffer.commit"})JSON";
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
// Forgets the audio already appended for an utterance that wasn't speech.
const char kClearMessage[] = R"JSON({"type":"input_audio_buffer.clear"})JSON";
const char kAppendMessagePrefix[] = R"JSON({"type":"input_audio_buffer.append","audio":")JSON";
const char kAppendMessageSuffix[] = R"JSON("})JSON";

constexpr uint32_t kSerialBaudRate = 115200;  // must match tools/serial_logger.ps1 and tools/reply_audio_app.html
constexpr uint32_t kSampleRateHz = 24000;   // must match kSessionUpdateMessage's input rate and i2s_audio_utils's I2S rate
constexpr uint32_t kManualRecordDurationSec = 3;  // LISTEN / the Talk button: the same fixed 3 s as before
constexpr uint32_t kChunkSamples = 2400;    // ~100ms per input_audio_buffer.append - same chunk duration step 12 confirmed working
constexpr uint32_t kChunkMs = kChunkSamples * 1000 / kSampleRateHz;
// Mic levels are quiet without any gain (step 3). x4 until 2026-10-06, when a
// spoken "1, 2, 3" hit full scale (min -32768 / max 32767) - clipped before
// OpenAI heard it. Must match tools/reply_audio_app.html's BOARD_MIC_GAIN.
constexpr int32_t kSoftwareGain = 2;
constexpr unsigned int kMaxTypedQuestionLength = 200;  // ASK <text> - longer is cut
constexpr unsigned long kReplyTimeoutMs = 15000;  // max silence (no server message) while awaiting_reply, not a total-reply-duration cap - see checkReplyTimeout()
// The gain and the base64 encoding run kGainBlockSamples at a time, straight
// into the JSON message: 192 bytes = 64 whole base64 groups, so only a
// chunk's last block can need padding - and no separate base64 buffer.
constexpr uint32_t kGainBlockSamples = 96;
constexpr uint32_t kChunkPcmBytes = kChunkSamples * sizeof(int16_t);
constexpr uint32_t kBase64ChunkChars = ((kChunkPcmBytes + 2) / 3) * 4;
constexpr uint32_t kJsonMessageBufferSize =
    sizeof(kAppendMessagePrefix) + kBase64ChunkChars + sizeof(kAppendMessageSuffix) + 1;
// This step's PASS: this many hands-free LISTENs in a row answered with no
// mic audio lost, and the heap's lowest point since boot at least this.
constexpr uint32_t kHandsFreeTurnsToPass = 5;
constexpr uint32_t kMinLowestHeapBytes = 20 * 1024;

// Static, not stack-local: too big for the Arduino loop task's stack (same
// reasoning as old step 8b/step 12).
int16_t mic_chunk[kChunkSamples];       // one chunk taken from the mic task: the mic's own samples
int16_t gain_block[kGainBlockSamples];  // one block of it with the software gain applied
char json_message[kJsonMessageBufferSize];

/** How the LISTEN being recorded right now is going. */
struct CaptureProgress {
  esp32va_measure::ListenMode mode;
  uint32_t samples_sent;
  uint32_t chunks_sent;
  uint32_t chunks_handled;
  uint32_t sent_json_bytes;
  unsigned long total_send_ms;
  unsigned long slowest_chunk_ms;
  unsigned long total_chunk_ms;
  uint32_t clipped_samples;
  int16_t level_min;  // gained samples - the level OpenAI hears
  int16_t level_max;
  double level_sum_of_squares;
};

WebSocketsClient webSocket;
bool should_keep_running = true;   // only false if the connection itself can never be established
bool should_send_session_update = false;
bool should_send_opening_greeting = false;  // set once, on the very first session.updated - see handleServerMessage()
bool session_ready = false;        // true once session.updated has been received at least once
bool session_ever_ready = false;
bool is_capturing = false;         // a LISTEN's audio is being recorded and sent (manual or hands-free)
bool awaiting_reply = false;
bool reply_received_this_cycle = false;
bool transcript_in_progress = false;   // a response.output_audio_transcript.delta stream is currently mid-line
bool response_fully_received = false;  // the final (post-tool-call, if any) response.done arrived - ring buffer may still be draining
bool function_call_turn_pending = false;  // true from a finalized tool call until its function-call-only response.done is consumed
bool response_in_flight = false;   // a response.create (greeting or LISTEN) hasn't had its final response.done yet
uint32_t listen_cycle = 0;         // deliberately never reset - keeps counting across reconnects for a soak test
uint32_t connection_count = 0;     // how many times WStype_CONNECTED has fired this run (1 = first connect, 2+ = reconnect)
unsigned long last_reply_activity_ms = 0;  // last time ANY server message arrived while a response was due - see checkReplyTimeout()
bool ws_reads_paused_for_backpressure = false;  // see i2s_audio_utils.h's ringShouldPauseReads()
bool step_verdict_reported = false;  // this step's own control-center verdict - see reportStepVerdict()
const char* unprompted_speech_detail = "greeting";  // @@STATE speaking detail for speech no LISTEN asked for - "timer" once a timer announced itself, "typed" for ASK
bool speech_in_progress = false;  // reply (or greeting) audio has started arriving and hasn't finished playing yet
constexpr size_t kToolNameBufferSize = 32;
char active_tool_name[kToolNameBufferSize] = "";  // the tool call being handled, for @@STATE tool <name>
constexpr size_t kStateDetailBufferSize = 64;
CaptureProgress capture = {};
bool last_turn_was_hands_free = false;   // the LISTEN awaiting its verdict started by voice
bool last_turn_lost_mic_audio = false;   // ...and lost some mic audio on the way
uint32_t hands_free_turns_passed = 0;    // in a row, toward this step's PASS
}  // namespace

/** Milliseconds of audio in a number of 24 kHz samples. */
uint32_t samplesToMs(uint32_t samples) {
  return static_cast<uint32_t>(static_cast<uint64_t>(samples) * 1000 / kSampleRateHz);
}

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
    Serial.println("===== HANDS-FREE PASSED =====");
    Serial.println("Keep talking to it - and check by ear that knocks, slams and its own voice never start a LISTEN.");
  } else {
    Serial.printf("===== HANDS-FREE FAILED: %s =====\n", failure_reason);
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

/** Counts a LISTEN that just got its verdict toward this step's: a failed
    startup check fails it at once; otherwise kHandsFreeTurnsToPass
    hands-free LISTENs in a row that passed without losing mic audio pass
    it, if the heap never went below kMinLowestHeapBytes. A manual LISTEN
    neither counts nor breaks the streak. */
void countTowardStepVerdict(bool turn_passed) {
  const char* failed_check = firstFailedStartupCheck();
  if (failed_check != nullptr) {
    reportStepVerdict(false, failed_check);
    return;
  }
  if (!last_turn_was_hands_free) {
    return;
  }
  if (!turn_passed || last_turn_lost_mic_audio) {
    if (hands_free_turns_passed > 0) {
      LOG_INFO("Hands-free streak back to 0 of %u - this LISTEN %s.\n", (unsigned)kHandsFreeTurnsToPass,
               turn_passed ? "lost mic audio" : "failed");
    }
    hands_free_turns_passed = 0;
    return;
  }
  hands_free_turns_passed++;
  LOG_INFO("Hands-free LISTEN %u of %u in a row passed.\n", (unsigned)hands_free_turns_passed,
           (unsigned)kHandsFreeTurnsToPass);
  if (hands_free_turns_passed < kHandsFreeTurnsToPass) {
    return;
  }
  uint32_t lowest_heap_bytes = esp32va_heap::lowestFreeHeapSinceBootBytes();
  if (lowest_heap_bytes < kMinLowestHeapBytes) {
    char reason[96];
    snprintf(reason, sizeof(reason), "free heap fell to %u bytes (needs %u) - see the @@HEAP lines",
             (unsigned)lowest_heap_bytes, (unsigned)kMinLowestHeapBytes);
    reportStepVerdict(false, reason);
    return;
  }
  reportStepVerdict(true, nullptr);
}

/** Tells whoever is at the keyboard how to start the next LISTEN. */
void printNextTurnHint() {
  if (esp32va_hands_free::isHandsFreeOn()) {
    Serial.println("Just talk to continue the conversation (or type LISTEN).");
  } else {
    Serial.println("Type LISTEN to continue the conversation.");
  }
}

/** Marks this LISTEN cycle as having gotten a reply, logging the verdict exactly once per cycle.
    Verdicts are never gated behind a log level - see log_utils.h. */
void markReplyReceived() {
  if (reply_received_this_cycle) return;
  reply_received_this_cycle = true;
  awaiting_reply = false;
  Serial.printf("===== LISTEN #%u PASSED =====\n", listen_cycle);
  printNextTurnHint();
  esp32va_status::printProtocolLine("@@RESULT pass %u", listen_cycle);
  esp32va_measure::reportTurn(true);
  countTowardStepVerdict(true);
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
    Serial.println(esp32va_hands_free::isHandsFreeOn() ? "Just talk to try again (or type LISTEN)." : "Type LISTEN to try again.");
  }
  countTowardStepVerdict(false);
}

/** Halts the sketch entirely - reserved for a non-transient problem (the SD card
    being unusable, the mic task not starting, or the connection never
    succeeding - see openai_realtime_utils's attempt cap), not for a single
    LISTEN cycle failing. Fails this step's verdict too, unless it already
    passed. Never gated behind a log level - see log_utils.h. */
void haltSketch(const char* reason) {
  should_keep_running = false;
  esp32va_mic_capture::stopCapture();
  reportStepVerdict(false, reason);
  Serial.printf("===== HALTED: %s =====\n", reason);
  esp32va_status::reportPipelineState(esp32va_status::PipelineState::kHalted, reason);
}

/** Hands this LISTEN's capture numbers to @@TIMING (measurement_utils.h),
    logs how well the mic task and loop() kept up, and notes whether any mic
    audio was lost. speech_ms and end_of_speech_to_commit_ms are -1 when not
    known (a manual or aborted LISTEN). */
void recordCaptureMeasurements(const esp32va_mic_capture::CaptureStats& stats, long speech_ms,
                               long end_of_speech_to_commit_ms) {
  esp32va_measure::setListenStats(capture.mode, samplesToMs(stats.pre_roll_samples), speech_ms,
                                  end_of_speech_to_commit_ms, samplesToMs(stats.max_backlog_samples),
                                  samplesToMs(stats.dropped_samples), stats.max_read_gap_ms);
  double level_rms = capture.samples_sent > 0 ? sqrt(capture.level_sum_of_squares / capture.samples_sent) : 0.0;
  int32_t negative_peak = -static_cast<int32_t>(capture.level_min);
  int32_t positive_peak = static_cast<int32_t>(capture.level_max);
  int32_t peak_abs_sample = capture.samples_sent > 0 ? max(negative_peak, positive_peak) : 0;
  unsigned long average_chunk_ms = capture.chunks_handled > 0 ? capture.total_chunk_ms / capture.chunks_handled : 0;
  esp32va_measure::setCaptureStats(capture.sent_json_bytes, capture.total_send_ms, peak_abs_sample, level_rms,
                                   capture.clipped_samples, capture.slowest_chunk_ms, average_chunk_ms);
  LOG_DEBUG("Captured %u chunks. Mic level: min=%d max=%d rms=%.1f\n", (unsigned)capture.chunks_sent,
            capture.level_min, capture.level_max, level_rms);

  // The mic task's reads keep up unless one waited longer than the I2S
  // driver's buffer; loop() keeps up unless it fell further behind than
  // the borrowed ring holds.
  uint32_t mic_buffer_ms = micBufferCapacityMs();
  LOG_INFO("Mic timing: longest gap between reads %u ms (the driver holds %u ms); sending fell %u ms behind at "
           "most, %u ms dropped; a %u ms chunk took %lu ms at most to handle, %lu ms on average.\n",
           (unsigned)stats.max_read_gap_ms, (unsigned)mic_buffer_ms, (unsigned)samplesToMs(stats.max_backlog_samples),
           (unsigned)samplesToMs(stats.dropped_samples), (unsigned)kChunkMs, capture.slowest_chunk_ms,
           average_chunk_ms);
  last_turn_lost_mic_audio = stats.dropped_samples > 0 || stats.max_read_gap_ms >= mic_buffer_ms;
  if (last_turn_lost_mic_audio) {
    LOG_ERROR("Mic audio was lost this LISTEN - %u ms dropped from the ring, longest read gap %u ms.\n",
              (unsigned)samplesToMs(stats.dropped_samples), (unsigned)stats.max_read_gap_ms);
  }
  if (stats.unexpected_events > 0) {
    LOG_ERROR("The mic task saw %u unexpected detector events this LISTEN.\n", (unsigned)stats.unexpected_events);
  }
}

/** Ends a capture that can't become a turn any more (the connection
    dropped): stops the mic, keeps what was recorded on the SD card, and
    fails the LISTEN. */
void abortCapture(const char* reason) {
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  esp32va_mic_capture::stopCapture();
  recordCaptureMeasurements(stats, -1, -1);
  finishVoiceCommandRecording();
  is_capturing = false;
  awaiting_reply = true;  // so markReplyFailed() reports this LISTEN's verdict
  markReplyFailed(reason);
}

/** Parses one server text frame, logs its "type" and any interesting fields, and
    drives session readiness plus the current LISTEN cycle's PASS/FAIL verdict. */
void handleServerMessage(uint8_t* payload, size_t length) {
  // Any server message at all, while a response is due, proves the
  // connection/response is still alive - resets the inactivity clock so a
  // long reply (paced by playback backpressure) doesn't trip
  // checkReplyTimeout() just for taking a while. See PROJECT_OVERVIEW.md
  // step 16's "reply timeout fired mid-reply" discovery.
  if (awaiting_reply || response_in_flight) {
    last_reply_activity_ms = millis();
  }
  if (awaiting_reply) {
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstServerEvent);
  }

  char type_buf[48];
  bool have_type = extractEventType(payload, length, type_buf, sizeof(type_buf));

  if (have_type && strcmp(type_buf, "response.output_audio.delta") == 0) {
    const uint8_t* value_start = nullptr;
    size_t value_len = 0;
    if (!findRawStringField(payload, length, "delta", &value_start, &value_len)) {
      LOG_ERROR("Audio delta missing 'delta' field.\n");
      return;
    }
    if (isRingLentToMic()) {
      // Half-duplex says this can't happen: the ring is only lent while no
      // response is coming. Playing it would mean overwriting the mic.
      LOG_ERROR("Reply audio arrived while the mic was listening - dropped it.\n");
      return;
    }
    esp32va_reply_recorder::noteReplyAudioArriving();  // before the ring write, so the tap catches it
    speech_in_progress = true;
    if (awaiting_reply) {
      esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstAudio);
      esp32va_measure::addReplyAudio(value_len);
    }
    decodeAndRingAudioDelta(value_start, value_len);
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
      if (esp32va_hands_free::isHandsFreeOn()) {
        Serial.printf("Ready - just start talking once the greeting ends (or type LISTEN).\n");
      } else {
        Serial.printf("Ready - type LISTEN to talk, or VAD on for hands-free.\n");
      }
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
    } else if (strcmp(function_name, "set_volume") == 0) {
      handled = esp32va_controls::handleSetVolumeCall(doc, webSocket);
    } else if (strcmp(function_name, "get_current_time") == 0) {
      handled = esp32va_clock::handleGetCurrentTimeCall(doc, webSocket);
    } else if (strcmp(function_name, "set_timer") == 0) {
      handled = esp32va_clock::handleSetTimerCall(doc, webSocket);
    } else if (strcmp(function_name, "get_board_status") == 0) {
      handled = esp32va_info::handleGetBoardStatusCall(doc, webSocket);
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
    }
    Serial.print(delta);
    // After every piece, not just the first: an @@ line printed in between
    // ends the line, and the next @@ line must find it open again - or it's
    // glued to the text, where the app doesn't see it.
    esp32va_status::setRawLineOpen(true);
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.done") == 0) {
    if (transcript_in_progress) {
      esp32va_status::endRawLine();
      transcript_in_progress = false;
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
    esp32va_status::endRawLine();  // it can arrive while the reply's transcript is streaming
    Serial.printf("You said: \"%s\"\n", transcript != nullptr ? transcript : "(none)");
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kHeardTranscript);
    esp32va_measure::setHeardLength(transcript != nullptr ? strlen(transcript) : 0);
    if (transcript != nullptr) {
      esp32va_status::reportTextLine("HEARD", transcript);  // the app's chat bubble
    }
    if (last_turn_was_hands_free) {
      esp32va_hands_free::noteUtteranceTranscript(transcript);  // no words: it was noise after all
    }
    return;
  }

  if (strcmp(type, "conversation.item.input_audio_transcription.failed") == 0) {
    const char* error_message = doc["error"]["message"] | "(no error detail)";
    LOG_ERROR("Input transcription failed: %s\n", error_message);
    LOG_DEBUG("Input transcript unavailable this cycle - the spoken reply itself is unaffected.\n");
    return;
  }

  if (strcmp(type, "input_audio_buffer.cleared") == 0) {
    LOG_DEBUG("input_audio_buffer.cleared - the discarded utterance's audio is gone.\n");
    return;
  }

  if (strcmp(type, "response.done") == 0) {
    const char* status = doc["response"]["status"] | "(unknown)";
    LOG_DEBUG("response.done -> status: %s\n", status);
    // Whatever the status, no more audio is coming for this response. A
    // function-call-only response.done has no recording open - a no-op.
    esp32va_reply_recorder::noteReplyFinished();
    if (strcmp(status, "failed") == 0) {
      response_in_flight = false;
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
      response_in_flight = false;
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
      response_in_flight = false;
      if (is_capturing) {
        abortCapture("disconnected while recording");  // stops the mic (no send happens here)
      }
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

/** Starts a LISTEN turn whose audio the mic task is already capturing:
    manual (LISTEN / Talk, a fixed kManualRecordDurationSec from now) or by
    voice (hands-free, from its pre-roll on). Its audio is sent chunk by
    chunk by serviceCapture(). */
void beginListenTurn(esp32va_measure::ListenMode mode) {
  listen_cycle++;
  esp32va_measure::beginTurn(listen_cycle);
  is_capturing = true;
  awaiting_reply = false;
  reply_received_this_cycle = false;
  response_fully_received = false;
  function_call_turn_pending = false;
  speech_in_progress = false;
  capture = {};
  capture.mode = mode;
  capture.level_min = INT16_MAX;
  capture.level_max = INT16_MIN;
  last_turn_was_hands_free = mode == esp32va_measure::ListenMode::kVoice;
  last_turn_lost_mic_audio = false;
  esp32va_command_recorder::beginVoiceCommand();
  if (mode == esp32va_measure::ListenMode::kVoice) {
    esp32va_status::reportPipelineState(esp32va_status::PipelineState::kListening, "vad");
    Serial.printf("LISTEN #%u: heard speech - recording until you stop talking, streaming to OpenAI...\n",
                  listen_cycle);
  } else {
    char listen_detail[kStateDetailBufferSize];
    snprintf(listen_detail, sizeof(listen_detail), "%u", (unsigned)kManualRecordDurationSec);
    esp32va_status::reportPipelineState(esp32va_status::PipelineState::kListening, listen_detail);
    Serial.printf("LISTEN #%u: recording %us and streaming to OpenAI...\n", listen_cycle,
                  (unsigned)kManualRecordDurationSec);
  }
}

/** Handles the mic task confirming an utterance: it becomes a LISTEN turn,
    unless the board stopped being able to take one meanwhile. */
void handleVoiceStarted() {
  if (is_capturing) {
    LOG_ERROR("Speech was detected during another recording - ignored.\n");
    return;
  }
  if (!session_ready || awaiting_reply || response_in_flight) {
    // The mic stops listening on the next loop() pass anyway (hands_free_utils.h).
    LOG_INFO("Heard speech just as the board stopped listening - ignored it.\n");
    esp32va_mic_capture::stopCapture();
    return;
  }
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  beginListenTurn(esp32va_measure::ListenMode::kVoice);
  esp32va_hands_free::noteUtteranceStarted(samplesToMs(stats.pre_roll_samples));
}

/** Handles the mic task deciding the utterance being recorded wasn't
    speech: OpenAI forgets the audio already sent, the recording is deleted,
    and the LISTEN never happened (its number is reused). The mic is already
    listening for speech again. */
void handleVoiceDiscarded() {
  if (!is_capturing || capture.mode != esp32va_measure::ListenMode::kVoice) {
    LOG_ERROR("A discarded utterance arrived without a hands-free recording in progress - ignored.\n");
    return;
  }
  if (capture.chunks_sent > 0) {
    webSocket.sendTXT(kClearMessage);
  }
  esp32va_command_recorder::cancelVoiceCommand();
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  Serial.printf("LISTEN #%u dropped - that didn't sound like speech. Still listening.\n", listen_cycle);
  is_capturing = false;
  listen_cycle--;
  esp32va_hands_free::noteUtteranceDiscarded(stats.discard_reason, samplesToMs(stats.captured_samples));
}

/** Takes up to one chunk of captured audio, saves the mic's own samples to
    the SD card, and streams them with the software gain applied as one
    input_audio_buffer.append - its base64 written straight into the JSON
    message - printing the chunk's level as @@MIC_LEVEL. */
void sendCaptureChunk() {
  unsigned long handling_started_ms = millis();
  uint32_t sample_count = esp32va_mic_capture::takeCaptureSamples(mic_chunk, kChunkSamples);
  if (sample_count == 0) {
    return;  // the mic task dropped it meanwhile - counted in its stats
  }

  size_t message_length = sizeof(kAppendMessagePrefix) - 1;
  memcpy(json_message, kAppendMessagePrefix, message_length);
  double chunk_sum_of_squares = 0.0;
  bool encoded = true;
  for (uint32_t block_start = 0; block_start < sample_count; block_start += kGainBlockSamples) {
    uint32_t block_count = min(kGainBlockSamples, sample_count - block_start);
    for (uint32_t i = 0; i < block_count; i++) {
      int32_t scaled = static_cast<int32_t>(mic_chunk[block_start + i]) * kSoftwareGain;
      if (scaled >= INT16_MAX) {
        scaled = INT16_MAX;
        capture.clipped_samples++;
      } else if (scaled <= INT16_MIN) {
        scaled = INT16_MIN;
        capture.clipped_samples++;
      }
      gain_block[i] = static_cast<int16_t>(scaled);
      capture.level_min = min(capture.level_min, gain_block[i]);
      capture.level_max = max(capture.level_max, gain_block[i]);
      chunk_sum_of_squares += static_cast<double>(scaled) * scaled;
    }
    size_t encoded_chars = 0;
    int b64_result = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(json_message + message_length),
                                           sizeof(json_message) - message_length, &encoded_chars,
                                           reinterpret_cast<const unsigned char*>(gain_block),
                                           block_count * sizeof(int16_t));
    if (b64_result != 0) {
      LOG_ERROR("base64 encode failed (code %d) - skipping this chunk.\n", b64_result);
      encoded = false;
      break;
    }
    message_length += encoded_chars;
  }
  capture.level_sum_of_squares += chunk_sum_of_squares;
  double chunk_rms = sqrt(chunk_sum_of_squares / sample_count);
  int chunk_level_dbfs = chunk_rms >= 1.0 ? static_cast<int>(lround(20.0 * log10(chunk_rms / 32768.0))) : -96;
  esp32va_status::printProtocolLine("@@MIC_LEVEL %d", chunk_level_dbfs);

  if (encoded) {
    memcpy(json_message + message_length, kAppendMessageSuffix, sizeof(kAppendMessageSuffix));  // with its '\0'
    message_length += sizeof(kAppendMessageSuffix) - 1;
    unsigned long send_started_ms = millis();
    webSocket.sendTXT(json_message, message_length);
    capture.total_send_ms += millis() - send_started_ms;
    capture.sent_json_bytes += message_length;
    capture.chunks_sent++;
  }
  esp32va_command_recorder::appendMicSamples(mic_chunk, sample_count);
  capture.samples_sent += sample_count;
  capture.chunks_handled++;
  unsigned long handling_ms = millis() - handling_started_ms;
  capture.slowest_chunk_ms = max(capture.slowest_chunk_ms, handling_ms);
  capture.total_chunk_ms += handling_ms;
}

/** Ends a capture whose audio has all been sent: gives the ring back to
    playback (the reply needs it), commits the audio and asks for a response,
    records the LISTEN's numbers, closes the SD recording and starts the
    reply timeout. Also says whether any mic audio was lost on the way. */
void finishCapture() {
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  esp32va_mic_capture::stopCapture();
  webSocket.sendTXT(kCommitMessage);
  webSocket.sendTXT(kResponseCreateMessage);
  unsigned long committed_ms = millis();
  response_in_flight = true;
  esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kRecordingDone);

  bool is_hands_free = capture.mode == esp32va_measure::ListenMode::kVoice;
  long speech_ms = -1;
  long end_of_speech_to_commit_ms = -1;
  if (is_hands_free) {
    speech_ms = static_cast<long>(samplesToMs(stats.speech_samples));
    if (stats.speech_end_ms != 0) {
      end_of_speech_to_commit_ms = static_cast<long>(committed_ms - stats.speech_end_ms);
    }
    esp32va_hands_free::noteUtteranceSent(static_cast<uint32_t>(speech_ms),
                                          stats.end_event == esp32va_vad::DetectorEvent::kMaxLengthReached);
  }
  Serial.printf("Done streaming %.1f s of audio - waiting for reply...\n", samplesToMs(capture.samples_sent) / 1000.0f);
  recordCaptureMeasurements(stats, speech_ms, end_of_speech_to_commit_ms);
  finishVoiceCommandRecording();

  is_capturing = false;
  awaiting_reply = true;
  last_reply_activity_ms = millis();
}

/** Moves the capture along, once per loop() pass: acts on the mic task's
    events (an utterance confirmed, a capture complete or discarded), sends
    at most one ~100 ms chunk of what's been captured - a partial one only
    once the capture is complete - and finishes the capture once it's all
    sent. */
void serviceCapture() {
  using esp32va_mic_capture::CaptureEvent;
  CaptureEvent event = esp32va_mic_capture::takeCaptureEvent();
  switch (event) {
    case CaptureEvent::kNone:
      break;
    case CaptureEvent::kVoiceStarted:
      handleVoiceStarted();
      break;
    case CaptureEvent::kCaptureEnded:
      LOG_DEBUG("The mic task says the recording is complete - sending the rest.\n");
      break;
    case CaptureEvent::kCaptureDiscarded:
      handleVoiceDiscarded();
      break;
    default:
      LOG_ERROR("Unexpected capture event %d - ignored.\n", (int)event);
      break;
  }
  if (!is_capturing) {
    return;
  }
  uint32_t samples_ready = esp32va_mic_capture::captureSamplesReady();
  bool capture_complete = esp32va_mic_capture::captureMode() == esp32va_mic_capture::CaptureMode::kDraining;
  if (samples_ready >= kChunkSamples || (capture_complete && samples_ready > 0)) {
    sendCaptureChunk();
  }
  if (esp32va_mic_capture::isCaptureDrained()) {
    finishCapture();
  }
}

/** True when a LIST/GET may run now: no turn in progress, nothing left to
    play, nothing left to save. A GET holds loop() for its whole transfer, so
    it must never overlap playback or recording. */
bool isIdleForFileTransfer() {
  return !is_capturing && !esp32va_mic_capture::isCaptureInProgress() && !awaiting_reply && ringUsedBytes() == 0 &&
         esp32va_reply_recorder::isRecorderIdle();
}

/** True when hands-free listening may run: the session is ready, the
    greeting has been asked for, and nothing is being recorded, sent, played
    or saved, with no response still on its way. */
bool boardCanListen() {
  return should_keep_running && session_ready && !should_send_opening_greeting && !is_capturing &&
         !awaiting_reply && !response_in_flight && !speech_in_progress && ringUsedBytes() == 0 &&
         esp32va_reply_recorder::isRecorderIdle();
}

/** True for the commands before which hands-free listening stops: BEEP
    makes a sound the mic would hear, and a GET holds loop() for its whole
    transfer. */
bool commandPausesHandsFree(const char* command) {
  const char* const kPausingWords[] = {"BEEP", "GET"};
  for (const char* word : kPausingWords) {
    size_t word_length = strlen(word);
    if (strncasecmp(command, word, word_length) == 0 && (command[word_length] == '\0' || command[word_length] == ' ')) {
      return true;
    }
  }
  return false;
}

/** Prints everything the laptop app shows about the board right now: every
    startup check, the assistant's state, Wi-Fi signal, volume, LED, the
    light sensor, hands-free, the timer and the clock - the answer to STATUS. */
void reprintFullStatus() {
  esp32va_status::reprintStatusSnapshot();
  esp32va_wifi::reportWifiSignal();
  esp32va_controls::announceControlStates();
  esp32va_light_sensor::announceLightState();
  esp32va_hands_free::announceHandsFreeState();
  esp32va_clock::announceTimerState();
  esp32va_clock::announceClock();
}

/** Starts a manual LISTEN (the command, or the app's Talk button) if the
    board can take one now; otherwise says why not. Works with hands-free
    on too - a fixed kManualRecordDurationSec, as before. */
void startManualListen() {
  if (!should_keep_running) {
    Serial.printf("LISTEN ignored - the sketch has halted (see the HALTED line above).\n");
  } else if (!session_ready) {
    Serial.printf("LISTEN ignored - not ready yet (still connecting/setting up).\n");
  } else if (is_capturing) {
    Serial.printf("LISTEN ignored - already recording.\n");
  } else if (awaiting_reply) {
    Serial.printf("LISTEN ignored - still waiting for or playing back a reply.\n");
  } else if (!esp32va_reply_recorder::isRecorderIdle()) {
    Serial.printf("LISTEN ignored - still saving the last reply to the SD card, try again in a moment.\n");
  } else if (!esp32va_mic_capture::startManualCapture(kSampleRateHz * kManualRecordDurationSec)) {
    Serial.printf("LISTEN ignored - the mic is busy (speech is being recorded, or audio still playing).\n");
  } else {
    beginListenTurn(esp32va_measure::ListenMode::kManual);
  }
}

/** ASK <text>: a question typed in the laptop app instead of spoken - for a
    noisy room, or a mic that won't cooperate. It goes into the same
    conversation as a text message, and the reply is spoken like the
    greeting: no LISTEN, so no recording and no @@TIMING. The text comes
    back as @@HEARD for the app's chat bubble. */
void askTypedQuestion(const String& text) {
  if (!session_ready) {
    Serial.printf("ASK ignored - not connected to OpenAI yet.\n");
    return;
  }
  if (!isIdleForFileTransfer() || response_in_flight || speech_in_progress) {
    Serial.printf("ASK ignored - the assistant is busy, try again when it is Ready.\n");
    return;
  }
  String question = text.substring(0, kMaxTypedQuestionLength);
  JsonDocument doc;
  doc["type"] = "conversation.item.create";
  JsonObject item = doc["item"].to<JsonObject>();
  item["type"] = "message";
  item["role"] = "user";
  JsonObject part = item["content"].to<JsonArray>().add<JsonObject>();
  part["type"] = "input_text";
  part["text"] = question;  // ArduinoJson escapes it
  String message;
  serializeJson(doc, message);
  webSocket.sendTXT(message);
  webSocket.sendTXT(kResponseCreateMessage);
  esp32va_status::reportTextLine("HEARD", question.c_str());
  unprompted_speech_detail = "typed";
  response_in_flight = true;  // like the greeting: hands-free waits for it to be heard
  last_reply_activity_ms = millis();
  LOG_INFO("Typed question sent: \"%s\"\n", question.c_str());
}

/** Checks Serial for a typed command. LIST / GET / DELETE <name> / DELETE_ALL answer the laptop
    app (see sd_file_transfer_utils.h) - with @@BUSY unless idle. STATUS
    reprints the board's whole status (status_report_utils.h), CLOCK just
    the board's local time (clock_timer_utils.h). VOLUME / MUTE
    / BEEP / LED / LED_BRIGHTNESS control the hardware
    (device_controls_utils.h), LIGHT [<value>] / LIGHT_BANDS set/show the
    simulated light sensor (light_sensor_utils.h), VAD / VAD_SENSITIVITY /
    VAD_HANGOVER set hands-free listening (hands_free_utils.h) - all at any
    time; BEEP and GET stop hands-free listening first. LISTEN starts a
    manual capture if the board can take one (startManualListen()). ASK
    <text> sends a typed question (askTypedQuestion()).
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
  bool pauses_hands_free = commandPausesHandsFree(command.c_str());
  if (pauses_hands_free) {
    esp32va_hands_free::holdOff();
  }
  bool handled = esp32va_sd_transfer::handleFileCommand(command.c_str(), isIdleForFileTransfer());
  if (!handled && command.equalsIgnoreCase("STATUS")) {
    reprintFullStatus();
    handled = true;
  }
  if (!handled && command.equalsIgnoreCase("CLOCK")) {
    esp32va_clock::announceClock();
    handled = true;
  }
  if (!handled) {
    handled = esp32va_controls::handleControlCommand(command.c_str(), isIdleForFileTransfer()) ||
              esp32va_light_sensor::handleLightCommand(command.c_str()) ||
              esp32va_hands_free::handleHandsFreeCommand(command.c_str());
  }
  if (pauses_hands_free) {
    esp32va_hands_free::holdOff();  // again: the cooldown counts from after the beep or the transfer
  }
  if (handled) {
    return;
  }
  if (command.length() > 4 && command.substring(0, 4).equalsIgnoreCase("ASK ")) {
    askTypedQuestion(command.substring(4));
    return;
  }
  if (!command.equalsIgnoreCase("LISTEN")) {
    Serial.printf("Unknown command \"%s\" - expected LISTEN, ASK <text>, STATUS, CLOCK, VAD on|off, VAD_SENSITIVITY <1-5>, VAD_HANGOVER <600-1500>, LIGHT <0-%d>, LIGHT_BANDS <evening> <day>, VOLUME <0-100>, MUTE on|off, BEEP, LED on|off, LED_BRIGHTNESS <0-100>, LIST, GET <name>, DELETE <name> or DELETE_ALL [replies|commands].\n",
             command.c_str(), esp32va_light_sensor::kMaxLightSensorValue);
    return;
  }
  startManualListen();
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
    PROJECT_OVERVIEW.md step 16's "reply timeout fired mid-reply" discovery.)
    The same silence ends a response nobody waits on (the greeting), so
    hands-free listening isn't held off forever by one that never finished. */
void checkReplyTimeout() {
  if (!awaiting_reply && !response_in_flight) {
    return;
  }
  if (millis() - last_reply_activity_ms < kReplyTimeoutMs) {
    return;
  }
  response_in_flight = false;  // a response that silent isn't coming any more
  if (!awaiting_reply) {
    LOG_ERROR("No word from the server for %lu ms about the last response - not waiting for it any more.\n",
              kReplyTimeoutMs);
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
    every loop() pass. */
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
    esp32va_status::reportPipelineState(PipelineState::kSpeaking, awaiting_reply ? nullptr : unprompted_speech_detail);
  } else if (is_capturing) {
    if (capture.mode == esp32va_measure::ListenMode::kVoice) {
      esp32va_status::reportPipelineState(PipelineState::kListening, "vad");
    } else {
      char listen_detail[kStateDetailBufferSize];
      snprintf(listen_detail, sizeof(listen_detail), "%u", (unsigned)kManualRecordDurationSec);
      esp32va_status::reportPipelineState(PipelineState::kListening, listen_detail);
    }
  } else if (awaiting_reply) {
    esp32va_status::reportPipelineState(PipelineState::kProcessing, nullptr);
  } else if (!esp32va_reply_recorder::isRecorderIdle()) {
    esp32va_status::reportPipelineState(PipelineState::kSaving, nullptr);
  } else if (esp32va_hands_free::isWaitingForSpeech()) {
    esp32va_status::reportPipelineState(PipelineState::kArmed, nullptr);
  } else {
    esp32va_status::reportPipelineState(PipelineState::kReady, nullptr);
  }
}

/** One-time setup: playback ring buffer, full-duplex mic+speaker I2S, onboard
    LED, (simulated) light sensor, the mic check and the mic task, SD card
    mount + self-test + recorders, Wi-Fi, time sync, then open the secure
    WebSocket connection - with a heap snapshot after each stage, and each
    startup check reported as @@STAGE for the laptop app. Also loads the
    saved volume / brightness / hands-free settings. Halts (step FAILED) if
    the SD card isn't usable or the mic task can't start. */
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
  esp32va_hands_free::setupHandsFree();
  esp32va_hands_free::announceHandsFreeState();
  esp32va_status::reportStage(esp32va_status::Stage::kSpeaker, esp32va_status::StageState::kOk,
                              "ready - check it by ear with Test beep");
  char stage_detail[kStateDetailBufferSize];
  bool mic_ok = checkMicrophoneSignal(stage_detail, sizeof(stage_detail));
  // After the mic check: it reads the mic itself, and from here on only the task does.
  if (!esp32va_mic_capture::beginMicCapture()) {
    esp32va_status::reportStage(esp32va_status::Stage::kMicrophone, esp32va_status::StageState::kFail,
                                "the mic task couldn't start (out of memory)");
    haltSketch("the mic task couldn't start - see the error above");
    return;
  }
  esp32va_status::reportStage(esp32va_status::Stage::kMicrophone,
                              mic_ok ? esp32va_status::StageState::kOk : esp32va_status::StageState::kFail,
                              stage_detail);
  esp32va_heap::recordHeapSnapshot("after ring buffer + I2S + mic task");
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
    watches for a typed command, moves a capture along (its events, its
    chunks, its commit), drains playback audio to the speaker, saves reply
    audio to the SD card, checks for a reply timeout / playback completion,
    lets hands-free listening start or stop, and keeps the laptop app's
    Wi-Fi and assistant-state display current. This is a soak-test sketch by
    design: it keeps running across as many LISTEN cycles and reconnects as
    it keeps getting fed, and only ever stops if the SD card is unusable or
    the connection itself can never be (re-)established - and even then
    keeps answering LIST/GET. */
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
    response_in_flight = true;  // hands-free waits for the greeting to be heard
    last_reply_activity_ms = millis();
  }

  checkForSerialCommand();
  serviceCapture();
  drainRingBufferToSpeaker();
  serviceReplyRecording();
  checkPlaybackComplete();
  checkReplyTimeout();
  esp32va_hands_free::serviceHandsFree(boardCanListen());
  esp32va_light_sensor::serviceLightSensor();  // a no-op until LIGHT_SENSOR_PIN is defined (config.h)
  // The timer rings whenever nothing is being recorded or played - even
  // with OpenAI unreachable, so a dropped connection never swallows it -
  // but it can only be announced out loud while the session is up.
  bool board_quiet = isIdleForFileTransfer() && !response_in_flight && !speech_in_progress;
  if (esp32va_clock::serviceTimer(board_quiet) && session_ready) {
    esp32va_clock::sendTimerAnnouncement(webSocket);
    unprompted_speech_detail = "timer";
    response_in_flight = true;  // like the greeting: hands-free waits for it to be heard
    last_reply_activity_ms = millis();
  }
  esp32va_wifi::monitorWifiConnection();
  updatePipelineState();
}
