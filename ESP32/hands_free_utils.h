#pragma once

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
// Serial commands (settings kept across reboots, settings_store_utils.h):
//   VAD <on|off>               hands-free on / off          -> @@VAD ...
//   VAD_SENSITIVITY <1-5>      1 = clear, loud speech only;
//                              5 = quieter speech, and more noise -> @@VAD ...
//   VAD_HANGOVER <600-1500>    ms of silence that ends an utterance -> @@VAD ...
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

/** Loads the saved settings (off, sensitivity 3, 1000 ms by default) and
    applies them. Call once from setup(). */
void setupHandsFree();

/** Handles a typed Serial command if it's VAD, VAD_SENSITIVITY or
    VAD_HANGOVER (case-insensitive). Returns true if it was one of them
    (applied or rejected - either way answered with @@VAD). */
bool handleHandsFreeCommand(const char* command);

/** Prints the @@VAD line - part of the STATUS reply. */
void announceHandsFreeState();

/** Whether hands-free is turned on. */
bool isHandsFreeOn();

/** Whether the mic is listening for speech right now (@@STATE armed). */
bool isWaitingForSpeech();

/** Call every loop() pass. board_can_listen: the session is ready and
    nothing is being recorded, sent, played or saved. Starts listening once
    that has held for kCooldownMs (unless it's off or pausing after
    discards), stops the moment it no longer holds, and prints @@VAD_LEVEL
    while listening. Never touches a capture already under way. */
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
