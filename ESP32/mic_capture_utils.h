#pragma once

#include <stdint.h>
#include "vad_utils.h"

// The mic, read without pause by a FreeRTOS task of its own (step 30), so a
// slow WebSocket send or SD write in loop() only delays when audio is sent,
// never whether it's captured - before step 30, loop() read the mic itself
// between sends, and a send that outlasted the I2S driver's 150 ms buffer
// lost audio ("Mic audio was probably dropped" in most step 29 turns).
//
// Every 20 ms the task reads one frame and, depending on the mode:
//   - kOff: throws it away - nothing to listen for, or the board is talking
//     (its own voice must reach neither the detector nor OpenAI);
//   - kArmed: feeds it to the voice activity detector (vad_utils.h) and
//     keeps it in a circular buffer, so once speech is confirmed the capture
//     can start kPreRollMs earlier and the first word isn't cut;
//   - kVoiceCapture: keeps it as a FIFO from there on, until the detector
//     says the utterance ended (kCaptureEnded) or wasn't speech
//     (kCaptureDiscarded, then back to kArmed);
//   - kManualCapture: keeps a fixed number of samples from the moment it
//     started, as a FIFO (the LISTEN command and the Talk button);
//   - kDraining: the capture ended - throws new frames away while loop()
//     takes the rest.
// loop() takes the audio with takeCaptureSamples() and learns what happened
// from takeCaptureEvent(). The task never prints anything: a line from it
// could land in the middle of a GET's binary transfer.
//
// No buffer of its own: the FIFO is the playback ring buffer, borrowed while
// it's empty (i2s_audio_utils.h's lendRingToMic()). It holds the samples as
// 8-bit mu-law, like the replies - ~1.36 s of 24 kHz audio, twice what 16-bit
// samples would fit: room for the pre-roll plus the send stalls measured on
// the board (step 29: a 100 ms chunk took ~80 ms to send on average, up to
// ~470 ms). Half-duplex makes the borrowing safe: the mic only listens while
// nothing plays, and a reply only starts arriving after the capture ended
// and stopCapture() gave the ring back. If loop() still falls further behind
// than the ring holds, the oldest unsent audio is dropped
// (CaptureStats::dropped_samples) - at the start of a capture that's the
// pre-roll's silence, the least useful part.
//
// Samples are the mic's own 16-bit values (the I2S word >> 16), before the
// sketch's software gain. The detector analyzes them as read; what loop()
// takes - and the voice command recordings hold - went through mu-law and
// back (G.711 quality, ~38 dB signal-to-noise).

namespace esp32va_mic_capture {

/** What the task does with each frame - see the header comment. */
enum class CaptureMode : uint8_t { kOff, kArmed, kVoiceCapture, kManualCapture, kDraining };

/** Something that happened to a capture, for loop() to act on. */
enum class CaptureEvent : uint8_t {
  kNone,
  kVoiceStarted,      // kArmed -> kVoiceCapture: speech confirmed, its audio (pre-roll first) is ready to take
  kCaptureEnded,      // the voice or manual capture is complete -> kDraining: take the rest, then stopCapture()
  kCaptureDiscarded,  // the utterance didn't sound like speech -> back to kArmed: drop what was taken
};

/** How the current (or last) capture went. */
struct CaptureStats {
  uint32_t captured_samples;     // samples in the capture, pre-roll included (so far, while it runs)
  uint32_t pre_roll_samples;     // voice: of those, from before the start was confirmed
  uint32_t speech_samples;       // voice: from where the speech began to the end of its last active frame
  uint32_t voiced_frames;        // voice: frames the detector found voiced
  uint32_t dip_frames;           // voice: syllable dips the detector counted
  uint32_t dropped_samples;      // lost because loop() fell further behind than the ring holds
  uint32_t max_backlog_samples;  // the furthest loop() fell behind
  uint32_t max_read_gap_ms;      // longest time between two of the task's reads - at or over
                                 // micBufferCapacityMs(), the I2S driver itself dropped audio
  unsigned long speech_end_ms;   // voice: millis() when the last active frame was read (0 = none yet)
  esp32va_vad::DetectorEvent end_event;        // voice: kSpeechEnded / kMaxLengthReached / kSpeechDiscarded
  esp32va_vad::DiscardReason discard_reason;   // voice: why it was discarded
  uint32_t unexpected_events;    // detector events that made no sense in the mode they came in (should stay 0)
};

/** The detector's view of the latest frame, for the laptop app's meter. */
struct LevelReading {
  float level_dbfs;      // speech-band level
  float floor_dbfs;      // the noise floor it's compared with
  float threshold_dbfs;  // what a frame needs to count as speech right now
  bool active;
};

/** Starts the mic task, in kOff. Call once from setup(), after
    setupI2sFullDuplex(), initPlaybackRingBuffer() and the startup mic check
    (which reads the mic directly). Returns false (logged) if the task
    couldn't be created. */
bool beginMicCapture();

/** Hands-free: borrows the ring and starts listening for speech (kArmed),
    with a fresh detector warm-up (~300 ms before a start can be confirmed;
    the noise floor learned so far is kept). Returns false if the ring is
    busy (still playing or being saved) or a capture is in progress. */
bool armVoiceTrigger(uint8_t sensitivity, uint32_t hangover_ms);

/** Changes the detector's settings, from its next frame on - no restart. */
void setDetectorSettings(uint8_t sensitivity, uint32_t hangover_ms);

/** Starts a fixed-length capture of sample_count samples from now
    (kManualCapture), borrowing the ring unless it's already lent (kArmed).
    Returns false if the ring is busy or a voice capture is in progress. */
bool startManualCapture(uint32_t sample_count);

/** Stops whatever the task is doing (kOff) and gives the ring back to
    playback, empty. Anything not taken yet is lost. Safe to call in any mode. */
void stopCapture();

/** The task's current mode. */
CaptureMode captureMode();

/** True from the start of a voice or manual capture until stopCapture() -
    including a voice capture whose kVoiceStarted loop() hasn't taken yet. */
bool isCaptureInProgress();

/** The oldest event not taken yet (kNone if there's none). */
CaptureEvent takeCaptureEvent();

/** Samples of the current capture ready to take right now. */
uint32_t captureSamplesReady();

/** Copies up to max_samples of the capture's audio, oldest first, into out
    and returns how many. 0 when there's nothing ready, or when the task had
    to drop what was being copied (the ring overflowed meanwhile). */
uint32_t takeCaptureSamples(int16_t* out, uint32_t max_samples);

/** True once a capture has ended (kDraining) and all of it was taken. */
bool isCaptureDrained();

/** Copies the current (or last) capture's numbers into *stats. */
void getCaptureStats(CaptureStats* stats);

/** Copies the detector's view of the latest frame into *reading - true
    while it's listening (kArmed or kVoiceCapture) and has analyzed one
    since, false otherwise. */
bool readLevel(LevelReading* reading);

}  // namespace esp32va_mic_capture
