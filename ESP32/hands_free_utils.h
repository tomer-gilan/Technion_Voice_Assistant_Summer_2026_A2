#pragma once

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <stdint.h>
#include "parameters.h"
#include "vad_utils.h"

// Hands-free listening (step 30): when it's on, the board listens for speech
// by itself whenever it's idle, and records each utterance from just before
// it began until the speaker stops - no Talk button needed. This module
// holds its settings and decides WHEN the mic may listen; the listening
// itself is mic_capture_utils.h's task and vad_utils.h's detector, and the
// sketch turns each utterance into a turn.
//
// Half-duplex: the mic listens only once the board has been idle for
// kCooldownMs - nothing recorded, sent, played or saved for that long - so
// the speaker's own voice (and its echo in the room) is never heard as
// speech. A BEEP or a file transfer stops it first (holdOff()). After
// kMaxDiscardsInARow utterances in a row that weren't speech - discarded by
// the detector, or sent and transcribed by OpenAI as no words at all - it
// pauses for kDiscardPauseMs instead of streaming more noise to OpenAI.
//
// It can also be switched by voice, through the set_hands_free tool
// (handleSetHandsFreeCall() below): the same switch as VAD on / off, so the
// control center's switch follows (@@VAD ...).
//
// The laptop's speakers count as a sound too: while the control center
// plays a recording out loud (a reply or a voice command from the SD card)
// it says so with LAPTOP_PLAYBACK, and the mic doesn't listen for speech
// until the recording is paused or over - else it would take the recording
// for a question. The cooldown then counts from its end, as after the
// board's own speech.
//
// Serial commands (settings kept across reboots, settings_store_utils.h):
//   VAD <on|off>               hands-free on / off          -> @@VAD ...
//   VAD_SENSITIVITY <1-5>      1 = clear, loud speech only;
//                              5 = quieter speech, and more noise -> @@VAD ...
//   VAD_HANGOVER <600-1500>    ms of silence that ends an utterance -> @@VAD ...
// And one that isn't a setting (not saved, no @@ line):
//   LAPTOP_PLAYBACK on [<ms>]  the laptop plays a recording for about ms
//                              more (kMaxLaptopPlaybackHoldMs at most, and
//                              when ms is left out) - no listening meanwhile
//   LAPTOP_PLAYBACK off        it was paused or is over
// Status lines for the laptop app:
//   @@VAD <on|off> <sensitivity> <hangover_ms>   - the settings; part of STATUS
//   @@VAD_LEVEL <level> <floor> <threshold>      - dBFS, ~10 per second while
//       waiting for speech: the speech-band level, the noise floor, and the
//       level that would count as speech
//   @@VAD_EVENT started <pre-roll ms>
//   @@VAD_EVENT sent <speech ms> <silence|length>        - ended by silence or the length cap
//   @@VAD_EVENT discarded <short|unvoiced|steady> <ms>   - see vad_utils.h's DiscardReason
//   @@VAD_EVENT paused <ms>                              - the back-off above
// (The assistant's own state shows as @@STATE armed while it waits for
// speech, and @@STATE listening vad while it records one.)

namespace esp32va_hands_free {

constexpr uint32_t kCooldownMs = esp32va_params::kHandsFreeCooldownMs;  // idle this long before listening: the speaker's last 150 ms + echo
constexpr uint32_t kMaxDiscardsInARow = esp32va_params::kHandsFreeMaxDiscardsInARow;
constexpr uint32_t kDiscardPauseMs = esp32va_params::kHandsFreeDiscardPauseMs;
// The longest a LAPTOP_PLAYBACK on holds listening off. The control center
// sends how long its recording has left, so this only bounds a hold whose
// "off" never comes (the page was closed mid-playback).
constexpr uint32_t kMaxLaptopPlaybackHoldMs = 5 * 60 * 1000;

/** Loads the saved settings (off, sensitivity 3, 1000 ms by default) and
    applies them. Call once from setup(). */
void setupHandsFree();

/** Handles a typed Serial command if it's VAD, VAD_SENSITIVITY,
    VAD_HANGOVER (case-insensitive; applied or rejected, either way answered
    with @@VAD) or LAPTOP_PLAYBACK. Returns true if it was one of them. */
bool handleHandsFreeCommand(const char* command);

/** Turns hands-free on or off, saves it and announces @@VAD - what the VAD
    command (the control center's switch) does, and the set_hands_free tool
    too (by_voice: only the log says so). Turning it off stops a mic that's
    waiting for speech, never a capture under way. */
void setHandsFree(bool turn_on, bool by_voice);

/** Handles a finalized response.function_call_arguments.done event for the
    set_hands_free tool: {"on": true|false} switches hands-free like
    setHandsFree(), and the function_call_output tells the model what
    changed (or that it already was so). user_request_in_progress: a LISTEN
    or a typed question is being answered - without one there was nothing
    to ask for the switch (a tool call during the greeting or a timer
    announcement), so nothing changes and the output says so. Returns true
    if handled, or false (logged) if call_id is missing. */
bool handleSetHandsFreeCall(JsonDocument& doc, WebSocketsClient& webSocket, bool user_request_in_progress);

/** Prints the @@VAD line - part of the STATUS reply. */
void announceHandsFreeState();

/** Whether hands-free is turned on. */
bool isHandsFreeOn();

/** True while the laptop plays a recording out loud (LAPTOP_PLAYBACK on,
    not yet off and not yet run out) - the mic doesn't listen for speech
    meanwhile. The sketch drops a hands-free utterance that was already
    being recorded when it started. */
bool isLaptopPlaying();

/** Whether the mic is listening for speech right now (@@STATE armed). */
bool isWaitingForSpeech();

/** Call every loop() pass. board_can_listen: the session is ready and
    nothing is being recorded, sent, played or saved. Starts listening once
    that has held - and the laptop hasn't been playing a recording - for
    kCooldownMs (unless it's off or pausing after discards), stops the
    moment it no longer holds, and prints @@VAD_LEVEL while listening. Never
    touches a capture already under way. */
void serviceHandsFree(bool board_can_listen);

/** Stops listening right away, if it is, and restarts the cooldown - for
    something about to make a sound (BEEP) or hold loop() (a file transfer). */
void holdOff();

/** Prints @@VAD_EVENT started for an utterance just confirmed. */
void noteUtteranceStarted(uint32_t pre_roll_ms);

/** Prints @@VAD_EVENT sent for an utterance that became a turn.
    ended_at_length_cap: it hit the 10 s cap rather than ending in silence. */
void noteUtteranceSent(uint32_t speech_ms, bool ended_at_length_cap);

/** Prints @@VAD_EVENT discarded, and pauses listening after
    kMaxDiscardsInARow utterances in a row that weren't speech. */
void noteUtteranceDiscarded(esp32va_vad::DiscardReason reason, uint32_t length_ms);

/** Takes OpenAI's transcription of a sent utterance: words reset the count
    of utterances in a row that weren't speech; none (null, empty, or only
    spaces and punctuation) adds one, like a discard. */
void noteUtteranceTranscript(const char* transcript);

}  // namespace esp32va_hands_free
