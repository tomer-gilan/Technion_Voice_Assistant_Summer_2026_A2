#pragma once

#include <stdint.h>

// Machine-readable status lines for the laptop app (control_center/control_center.html),
// so it can show the board's startup checks and what the assistant is doing
// right now without parsing the human log. Same convention as the file
// protocol in sd_file_transfer_utils.h: lines start with "@@", are printed
// raw, and are never gated behind the log level (at LOG_LEVEL_ERROR most of
// the INFO narrative never reaches Serial, but these always do).
//
//   @@STAGE <stage> <pending|active|ok|fail> [detail]
//       stage: sd | mic | speaker | wifi | time | openai - one per startup
//       check. detail is free text to the end of the line.
//   @@STATE <state> [detail]
//       state: booting | ready | armed | listening | processing | tool |
//              speaking | saving | reconnecting | offline | halted - what
//              the assistant is doing (armed: hands-free, waiting for
//              speech; offline: no Wi-Fi or no OpenAI for a while, still
//              retrying - the timer and the controls keep working).
//   @@RESULT <pass|fail> <LISTEN number> [reason]   - a LISTEN cycle's verdict
//   @@MIC_LEVEL <dBFS>       - the mic's level, every ~100 ms while listening
//   @@HEARD <text>           - what the user said (input transcription)
//   @@SAID <text>            - what the assistant said (output transcript)
//   @@WIFI <RSSI dBm>        - signal strength, on connect and every 10 s
//   @@ALIVE <uptime s>       - every kAliveIntervalMs from loop(): not hung
//
// Other modules add their own state lines the same way (@@LED, @@VOLUME in
// device_controls_utils.h, @@LIGHT in light_sensor_utils.h, @@BOOT_TIMING /
// @@TIMING in measurement_utils.h, @@TIMER / @@CLOCK in clock_timer_utils.h, @@HEAP in heap_diagnostics_utils.h, @@VAD /
// @@VAD_LEVEL / @@VAD_EVENT in hands_free_utils.h, @@WIFI_NET in
// wifi_utils.h, @@OPENAI_KEY in openai_realtime_utils.h, @@BUTTON in
// button_utils.h, @@SOUNDS in
// device_controls_utils.h, @@LIMITS in board_info_utils.h). The STATUS
// serial command reprints all of them, so an app that connects mid-session
// gets the full picture.

namespace esp32va_status {

/** One startup check, in the order they run. */
enum class Stage : uint8_t { kSdCard, kMicrophone, kSpeaker, kWifi, kTimeSync, kOpenAi, kCount };

/** Where a startup check is. kPending until it starts. */
enum class StageState : uint8_t { kPending, kActive, kOk, kFail };

/** What the assistant is doing right now. */
enum class PipelineState : uint8_t {
  kBooting,
  kReady,
  kArmed,  // hands-free (step 30): listening for speech to start
  kListening,
  kProcessing,
  kTool,
  kSpeaking,
  kSaving,
  kReconnecting,
  kOffline,  // no Wi-Fi, or OpenAI unreachable for a while - still retrying; timer and controls work
  kHalted,
};

/** Records a startup check's new state and prints its @@STAGE line.
    detail may be nullptr or empty; it's cut to fit a small fixed buffer. */
void reportStage(Stage stage, StageState state, const char* detail);

/** Whether a startup check last reported kOk. */
bool isStageOk(Stage stage);

/** Records and prints @@STATE - only when the state or its detail changed
    since the last call, so it's safe to call every loop() pass. */
void reportPipelineState(PipelineState state, const char* detail);

/** printf-style: prints one whole protocol line, e.g.
    printProtocolLine("@@WIFI %d", rssi). Adds the newline itself. For the
    other modules' @@ lines, so they all respect setRawLineOpen() below. */
void printProtocolLine(const char* format, ...) __attribute__((format(printf, 1, 2)));

/** Prints "@@<tag> <text>" with any line breaks in text turned into spaces,
    so a multi-line transcript still arrives as one protocol line. */
void reportTextLine(const char* tag, const char* text);

/** Tells this module a raw, not-yet-terminated line is being printed (the
    streamed transcript) - true after each piece of it, false once it ends
    with a newline. While one is open, every @@ line first ends it with a
    newline, since the app only reads "@@" at the very start of a line. */
void setRawLineOpen(bool open);

/** Ends the raw line with a newline if one is open - for the end of the
    streamed transcript, or other text that must start on a line of its own. */
void endRawLine();

/** Prints every startup check's last @@STAGE line and the current @@STATE
    again - the board's half of the STATUS command. */
void reprintStatusSnapshot();

}  // namespace esp32va_status
