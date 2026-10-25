// ESP32 voice assistant - firmware (ESP32-WROOM-32, no PSRAM)
//
// Talk to it, and it answers out loud and works the board: the LED, the
// speaker volume, a timer, the clock, the part of the day (light sensor) and
// its own health. Speech is understood in the cloud (OpenAI Realtime API,
// over one TLS WebSocket); this board records, streams, plays and runs the
// tools. The tunable parameters: parameters.h (compile time) and the
// control center (run time - control_center/control_center.html);
// Parameters/parameters.md lists them, and the internal constants too. The
// assistant's instructions and tools: realtime_session_config.h.
//
// This sketch holds the conversation itself, in Arduino tabs (the IDE joins
// them after this file, alphabetically, into one translation unit):
//   ESP32.ino            - this file: includes, the shared flags and
//                          constants, every function's declaration, setup()
//                          and loop()
//   listen_turn.ino      - a LISTEN's capture -> send cycle, cancel, ASK
//   serial_commands.ino  - the typed commands and the BOOT button
//   server_events.ino    - the session, its server events, the tools' dispatch
//   state_machine.ino    - the transitions and the assistant's state
//                          (@@STATE, see updatePipelineState())
//   verdicts.ino         - PASSED / FAILED, measurements, recordings
// Everything else is in the *_utils modules next to it -
// Documentation/architecture.md has the map.
//
// How a LISTEN starts: the Talk button / LISTEN command / BOOT button (a
// fixed kManualRecordDurationSec), or by voice when hands-free is on
// (hands_free_utils.h: a voice activity detector on this board starts and
// ends the recording). The mic is read by its own task (mic_capture_utils.h)
// so a slow send delays audio instead of losing it.
//
// Robustness: without an SD card (or once one stops answering) it runs on
// without recordings; without
// Wi-Fi or OpenAI it goes offline (@@STATE offline) and keeps retrying while
// the timer and the controls keep working; the Wi-Fi network and the OpenAI
// API key can be changed from the control center; a question with no answer
// times out after kReplyTimeoutMs. Only a mic task that can't start halts it.
//
// Built step by step - this is step 30 and the improvements after it
// (Documentation/versions.md; the earlier sketches are in
// Documentation/development_steps/). The step's own verdict still prints:
// "===== HANDS-FREE PASSED =====" once after kHandsFreeTurnsToPass hands-free
// LISTENs in a row were answered with no mic audio lost, and
// "===== LISTEN #N PASSED/FAILED =====" for every question.

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "parameters.h"
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
#include "button_utils.h"
#include "realtime_session_config.h"

namespace {
// The session.update message (instructions, audio formats, tools) is in
// realtime_session_config.h; the other client events the sketch sends:
const char kCommitMessage[] = R"JSON({"type":"input_audio_buffer.commit"})JSON";
const char kResponseCreateMessage[] = R"JSON({"type":"response.create"})JSON";
// Forgets the audio already appended for an utterance that wasn't speech.
const char kClearMessage[] = R"JSON({"type":"input_audio_buffer.clear"})JSON";
const char kCancelResponseMessage[] = R"JSON({"type":"response.cancel"})JSON";
const char kAppendMessagePrefix[] = R"JSON({"type":"input_audio_buffer.append","audio":")JSON";
const char kAppendMessageSuffix[] = R"JSON("})JSON";

constexpr uint32_t kSerialBaudRate = esp32va_params::kSerialBaudRate;
constexpr uint32_t kSampleRateHz = esp32va_vad::kSampleRateHz;  // 24 kHz, defined once (vad_utils.h); kSessionUpdateMessage's input rate must match
constexpr uint32_t kManualRecordDurationSec = esp32va_params::kManualRecordDurationSec;  // LISTEN / Talk / the BOOT button
constexpr uint32_t kChunkSamples = 2400;    // ~100ms per input_audio_buffer.append - same chunk duration step 12 confirmed working
constexpr uint32_t kChunkMs = kChunkSamples * 1000 / kSampleRateHz;
// Mic levels are quiet without any gain (step 3). x4 until 2026-10-06, when a
// spoken "1, 2, 3" hit full scale (min -32768 / max 32767) - clipped before
// OpenAI heard it. The control center gets it from @@LIMITS (mic_gain).
constexpr int32_t kSoftwareGain = esp32va_params::kMicSoftwareGain;
constexpr unsigned int kMaxTypedQuestionBytes = esp32va_params::kMaxTypedQuestionBytes;  // ASK <text> - longer is cut
constexpr unsigned long kReplyTimeoutMs = esp32va_params::kReplyTimeoutMs;  // max silence (no server message) while awaiting_reply, not a total-reply-duration cap - see checkReplyTimeout()
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

// Static, not stack-local: too big for the Arduino loop task's stack.
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
  unsigned long total_sd_ms;  // appendMicSamples() - diagnostic, see sendCaptureChunk()
  unsigned long slowest_chunk_ms;
  unsigned long total_chunk_ms;
  uint32_t clipped_samples;
  int16_t level_min;  // gained samples - the level OpenAI hears
  int16_t level_max;
  double level_sum_of_squares;
};

WebSocketsClient webSocket;
bool should_keep_running = true;   // only false if the mic task can't start - see haltSketch()
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
bool dropping_reply_audio = false;  // CANCEL stopped a response that's still coming - its audio, text and tool calls are skipped
bool response_in_flight = false;   // a response.create (greeting or LISTEN) hasn't had its final response.done yet
bool server_response_running = false;  // between the server's response.created and its response.done
uint32_t listen_cycle = 0;         // deliberately never reset - keeps counting across reconnects for a soak test
uint32_t connection_count = 0;     // how many times WStype_CONNECTED has fired this run (1 = first connect, 2+ = reconnect)
unsigned long last_reply_activity_ms = 0;  // last time ANY server message arrived while a response was due - see checkReplyTimeout()
bool ws_reads_paused_for_backpressure = false;  // see i2s_audio_utils.h's esp32va_audio::ringShouldPauseReads()
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
unsigned long session_not_ready_since_ms = 0;  // when the session was last lost (or boot) - see isAssistantOffline()
bool setup_finished = false;  // setup() returned - "offline" means something only after that
bool openai_connection_started = false;  // see startOpenAiConnectionWhenNetworkReady()
bool offline_announced = false;  // went offline (sound asked for, retries slowed) - "back online" is due once ready
bool wifi_was_up = false;  // see dropDeadSocketOnWifiLoss()
// Wi-Fi just came back: OpenAI is retried every second for a short while,
// then at the offline pace again - see dropDeadSocketOnWifiLoss().
constexpr unsigned long kWifiReturnFastRetryMs = 10000;
bool wifi_return_fast_retry = false;
unsigned long wifi_returned_ms = 0;
uint32_t listen_started_by_last_press = 0;  // the LISTEN the latest BOOT press started, 0 if none - see runTechnicianCheck()

// Every function the sketch's other tabs define, declared here: the Arduino
// IDE appends the tabs after this file (alphabetically) into one translation
// unit, and its prototype generator skips functions inside a namespace - so
// with all of them declared up front, no tab depends on the order. They all
// stay in this anonymous namespace (each tab reopens it); only setup() and
// loop() must be global (the Arduino core calls them).

// listen_turn.ino - a LISTEN turn
uint32_t samplesToMs(uint32_t samples);
void abortCapture(const char* reason);
void beginListenTurn(esp32va_measure::ListenMode mode);
void handleVoiceStarted();
void handleVoiceDiscarded();
void cancelListenTurn();
void dropHandsFreeRecordingDuringLaptopPlayback();
void sendCaptureChunk();
void finishCapture();
void serviceCapture();
void playStartTick();
bool startManualListen();
void askTypedQuestion(const String& text);

// serial_commands.ino - typed commands and the BOOT button
bool commandPausesHandsFree(const char* command);
void reprintFullStatus();
void playRefusedTone();
void handleTalkButtonPress();
void runTechnicianCheck();
void checkForSerialCommand();

// server_events.ino - the OpenAI session's events
void handleServerMessage(uint8_t* payload, size_t length);
void reportOpenAiStageOffline();
void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);

// state_machine.ino - the state machine
void enterListening();
void enterAwaitingReply();
void noteCaptureAborted();
void noteListenDropped();
void noteListenCancelled();
void noteVerdictReached();
void noteResponseRequested();
void noteUnpromptedResponseRequested(const char* speech_detail);
void noteResponseCreated();
void noteCancelledResponseOver();
void noteFailedResponseDone();
void noteCancelledResponseAlreadyOver();
void noteResponseRejected();
void clearResponseFlags();
void noteSessionLost();
bool isIdleForFileTransfer();
bool boardCanListen();
void checkReplyTimeout();
void checkPlaybackComplete();
bool isAssistantOffline();
void dropDeadSocketOnWifiLoss();
void reconnectWithNewApiKey();
void updatePipelineState();
void reportAliveHeartbeat();
void startOpenAiConnectionWhenNetworkReady();

// verdicts.ino - verdicts, measurements, recordings
void reportStepVerdict(bool passed, const char* failure_reason);
void handleFinishedRecording(const esp32va_reply_recorder::FinishedRecording& finished);
void finishVoiceCommandRecording();
const char* firstFailedStartupCheck();
void countTowardStepVerdict(bool turn_passed);
void printNextTurnHint();
void markReplyReceived();
void markReplyFailed(const char* reason);
void haltSketch(const char* reason);
void recordCaptureMeasurements(const esp32va_mic_capture::CaptureStats& stats, long speech_ms,
                               long end_of_speech_to_commit_ms);
void serviceReplyRecording();

}  // namespace

/** One-time setup: playback ring buffer, full-duplex mic+speaker I2S, onboard
    LED, (simulated) light sensor, the mic check and the mic task, SD card
    mount + self-test + recorders, Wi-Fi, time sync, then open the secure
    WebSocket connection - with a heap snapshot after each stage, and each
    startup check reported as @@STAGE for the laptop app. Also loads the
    saved volume / brightness / hands-free settings and the API key. Halts
    only if the mic task can't start; without an SD card it runs on without
    recordings. */
void setup() {
  Serial.begin(kSerialBaudRate);
  esp32va_heap::startCountingFailedAllocations();
  LOG_INFO("Boot.\n");
  // Every check starts out pending - this also resets a laptop app that was
  // already connected when the board restarted.
  esp32va_status::reportPipelineState(esp32va_status::PipelineState::kBooting, nullptr);
  esp32va_status::reprintStatusSnapshot();
  esp32va_heap::recordHeapSnapshot("boot");

  esp32va_audio::initPlaybackRingBuffer();
  esp32va_board::setupBoardState();
  esp32va_light_sensor::setupLightSensor();
  esp32va_light_sensor::announceLightState();
  esp32va_status::reportStage(esp32va_status::Stage::kMicrophone, esp32va_status::StageState::kActive, nullptr);
  esp32va_audio::setupI2sFullDuplex();
  esp32va_controls::setupDeviceControls();
  esp32va_controls::announceControlStates();
  esp32va_button::setupTalkButton();
  esp32va_button::announceButtonState();
  esp32va_hands_free::setupHandsFree();
  esp32va_hands_free::announceHandsFreeState();
  esp32va_openai::loadApiKey();  // before any connection - the saved key, or SECRETS.h's
  esp32va_openai::announceApiKey();
  esp32va_info::announceLimits();
  esp32va_status::reportStage(esp32va_status::Stage::kSpeaker, esp32va_status::StageState::kOk,
                              "ready - check it by ear with Test beep");
  char stage_detail[kStateDetailBufferSize];
  bool mic_ok = esp32va_audio::checkMicrophoneSignal(stage_detail, sizeof(stage_detail));
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
  esp32va_sd_card::setSdCardUsable(sd_card_usable);
  if (!sd_card_usable) {
    // Recordings are a feature, not a requirement: the assistant runs on
    // without them (the recorders skip their file work, LIST/GET answer
    // with an error line).
    esp32va_status::reportStage(esp32va_status::Stage::kSdCard, esp32va_status::StageState::kFail,
                                "no card (or not FAT32) - running without recordings");
    Serial.printf("No usable SD card - running without recordings. Insert one and press Reset to record again.\n");
  } else {
    char sd_summary[kStateDetailBufferSize / 2];
    esp32va_sd_card::describeSdCard(sd_summary, sizeof(sd_summary));
    snprintf(stage_detail, sizeof(stage_detail), "%s, self-test passed", sd_summary);
    esp32va_status::reportStage(esp32va_status::Stage::kSdCard, esp32va_status::StageState::kOk, stage_detail);
    // Now, not on request: loop() - which answers LIST - only starts after
    // Wi-Fi and time sync, 10+ s from here.
    esp32va_sd_transfer::announceRecordingList();
  }
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kSdReady);

  // Neither waits for good: without Wi-Fi (or the clock) the board goes on
  // offline, and loop() starts the OpenAI connection once both are up.
  if (esp32va_wifi::connectToWifi(esp32va_params::kWifiBootWaitMs)) {
    esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kWifiConnected);
    if (esp32va_wifi::syncSystemTime(esp32va_params::kTimeSyncBootWaitMs)) {
      esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kTimeSynced);
    }
  }
  esp32va_heap::recordHeapSnapshot("after Wi-Fi + time sync");

  webSocket.onEvent(onWebSocketEvent);
  startOpenAiConnectionWhenNetworkReady();
  if (!openai_connection_started) {
    Serial.printf("The OpenAI connection starts by itself once Wi-Fi and the clock are up. Meanwhile the timer "
                  "and the controls work.\n");
  }
  session_not_ready_since_ms = millis();
  wifi_was_up = esp32va_wifi::isWifiConnected();  // so loop() doesn't take boot for "Wi-Fi is back"
  setup_finished = true;
}

/** Services the WebSocket connection (unless paused for backpressure - see
    i2s_audio_utils.h's esp32va_audio::ringShouldPauseReads() - or Wi-Fi is
    down), sends session.update once
    flagged ready, sends the one-time opening greeting once flagged ready,
    watches for a typed command, moves a capture along (its events, its
    chunks, its commit), drains playback audio to the speaker, saves reply
    audio to the SD card, checks for a reply timeout / playback completion,
    lets hands-free listening start or stop, and keeps the laptop app's
    Wi-Fi and assistant-state display current. It keeps running across as
    many LISTEN cycles and reconnects as it keeps getting fed, and only ever
    stops if the mic task can't start (haltSketch()) - and even then keeps
    answering LIST/GET. */
void loop() {
  reportAliveHeartbeat();  // halted too: it still answers LIST/GET
  if (!should_keep_running) {
    serviceReplyRecording();  // closes a recording cut short by the halt
    esp32va_sd_card::serviceCardCheck(isIdleForFileTransfer());
    checkForSerialCommand();
    return;
  }

  // Backpressure pause/resume can fire many times over one long reply - too
  // frequent to be useful at LOG_INFO (see Documentation/decisions.md step 17's
  // logging doctrine), so this is LOG_DEBUG-only.
  bool should_pause = esp32va_audio::ringShouldPauseReads(ws_reads_paused_for_backpressure);
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
  // Not while Wi-Fi is down: the library would keep trying to reconnect to
  // OpenAI, and every try that fails raises a disconnect event - counted as
  // one more failed attempt. dropDeadSocketOnWifiLoss() closes the socket
  // when Wi-Fi goes, and reconnects at once when it's back.
  if (!ws_reads_paused_for_backpressure && esp32va_wifi::isWifiConnected()) {
    webSocket.loop();
  }

  if (should_send_session_update) {
    should_send_session_update = false;
    LOG_DEBUG("Sending session.update...\n");
    webSocket.sendTXT(esp32va_session::kSessionUpdateMessage);
  }

  if (should_send_opening_greeting) {
    should_send_opening_greeting = false;
    if (offline_announced) {
      // Booted offline: the "back online" sound goes before the greeting -
      // updatePipelineState() would only ask for it after this, and it would
      // wait for the greeting to end.
      offline_announced = false;
      esp32va_controls::requestStatusSound(esp32va_controls::StatusSound::kOnline);
      esp32va_hands_free::holdOff();  // as below: the mic mustn't take it for speech
      esp32va_controls::playPendingStatusSound();
      esp32va_hands_free::holdOff();
    }
    esp32va_greeting::sendOpeningGreeting(webSocket);
    noteResponseRequested();  // hands-free waits for the greeting to be heard
  }

  // Before the commands: a LIST / GET in this same pass then already knows
  // whether a card that just failed a recording is still there.
  esp32va_sd_card::serviceCardCheck(isIdleForFileTransfer());
  checkForSerialCommand();
  if (esp32va_button::takeButtonPress()) {
    handleTalkButtonPress();
  }
  if (esp32va_button::takeButtonLongPress()) {
    runTechnicianCheck();
  }
  if (esp32va_hands_free::isLaptopPlaying()) {
    dropHandsFreeRecordingDuringLaptopPlayback();  // the mic would take the recording for a question
  }
  serviceCapture();
  esp32va_audio::drainRingBufferToSpeaker();
  serviceReplyRecording();
  checkPlaybackComplete();
  checkReplyTimeout();
  esp32va_hands_free::serviceHandsFree(boardCanListen());
  esp32va_light_sensor::serviceLightSensor();  // a no-op until LIGHT_SENSOR_PIN is defined (parameters.h)
  // The timer rings whenever nothing is being recorded or played - even
  // with OpenAI unreachable, so a dropped connection never swallows it -
  // but it can only be announced out loud while the session is up.
  bool board_quiet = isIdleForFileTransfer() && !response_in_flight && !speech_in_progress;
  if (esp32va_clock::serviceTimer(board_quiet) && session_ready) {
    esp32va_clock::sendTimerAnnouncement(webSocket);
    noteUnpromptedResponseRequested("timer");  // like the greeting: hands-free waits for it to be heard
  }
  if (board_quiet && !response_in_flight && esp32va_controls::hasPendingStatusSound()) {
    esp32va_hands_free::holdOff();  // as for BEEP: the mic mustn't take it for speech
    esp32va_controls::playPendingStatusSound();
    esp32va_hands_free::holdOff();  // and the cooldown counts from after it
  }
  esp32va_wifi::monitorWifiConnection();
  dropDeadSocketOnWifiLoss();
  startOpenAiConnectionWhenNetworkReady();
  updatePipelineState();
}
