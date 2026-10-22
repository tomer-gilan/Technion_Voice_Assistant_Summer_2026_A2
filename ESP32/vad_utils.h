#pragma once

#include <stddef.h>
#include <stdint.h>
#include "parameters.h"

// Voice activity detection for hands-free listening (step 30): looks at the
// mic 20 ms at a time and decides when someone starts talking to the board
// and when they've finished. Pure computation - no Arduino, I2S or Serial -
// so tools/vad_replay.cpp runs this exact code on a laptop, over recordings
// downloaded from the SD card, to tune it without flashing.
//
// Per 20 ms frame:
//   - the speech band (200-3400 Hz) is filtered out of the signal, and its
//     level compared with an adaptive noise floor: the quietest frame of the
//     last ~2 s, so a steady air conditioner or fan raises the floor instead
//     of counting as speech;
//   - the frame is "active" when its speech-band level is at least the
//     sensitivity's margin above that floor, most of its energy sits in the
//     speech band (rumble and hiss don't), and its zero-crossing rate isn't
//     hiss-like;
//   - an active frame is also "voiced" when it repeats itself at a pitch
//     between 80 and 400 Hz (normalized autocorrelation) - vowels do, door
//     slams, knocks, typing, clicks and fans don't;
//   - a frame is a "syllable dip" when its level fell well below the loudest
//     frame of the 200 ms before it - speech rises and falls with its
//     syllables, 4-5 times a second; a machine, a bell or a tone doesn't.
// An utterance starts once most of the last ~240 ms were active AND some of
// them voiced - a bang or a knock dies away sooner and isn't voiced. It ends
// after hangover_ms with no voiced frame (short breaths between words don't
// end it, and neither does background noise that keeps going after the
// speaker stopped), or at the maximum length. Then it is judged, and discarded rather
// than sent when it had too little voicing (a cough, a chair scraping,
// running water) or, over 1 s long, no syllable rhythm (a machine starting
// up, a bell) - see DiscardReason.
//
// Levels are in dBFS of the samples as given: on the board that is the mic's
// own 16-bit samples, before the sketch's software gain - the same samples
// the voice command recordings hold.

namespace esp32va_vad {

constexpr uint32_t kSampleRateHz = 24000;
constexpr uint32_t kFrameMs = 20;
constexpr uint32_t kFrameSamples = kSampleRateHz * kFrameMs / 1000;  // 480

// Sensitivity 1 (needs the clearest, loudest speech) to 5 (reacts to
// quieter speech, and to more noise).
constexpr uint8_t kMinSensitivity = 1;
constexpr uint8_t kMaxSensitivity = 5;
constexpr uint8_t kDefaultSensitivity = esp32va_params::kDefaultVadSensitivity;

// How long a silence ends an utterance.
constexpr uint32_t kMinHangoverMs = esp32va_params::kMinVadHangoverMs;
constexpr uint32_t kMaxHangoverMs = esp32va_params::kMaxVadHangoverMs;
constexpr uint32_t kDefaultHangoverMs = esp32va_params::kDefaultVadHangoverMs;

// The longest utterance, lead-in included - a safety cap, so a noise the
// floor hasn't adapted to yet can't keep a recording open forever.
constexpr uint32_t kMaxUtteranceMs = 10000;

// How much audio from before the moment an utterance is confirmed goes into
// the recording, so its first word isn't cut: the confirmation itself takes
// ~240 ms of speech typically, up to ~500 ms for a soft start ("Mister...",
// "And so..." - quiet, low-pitched sounds count as speech only later).
constexpr uint32_t kPreRollMs = 500;

/** What one frame looked like. */
struct FrameAnalysis {
  float speech_band_dbfs;    // level of the 200-3400 Hz band
  float noise_floor_dbfs;    // adaptive floor of that level (quietest frame of the last ~2 s)
  float threshold_dbfs;      // the level this frame had to reach to count as active
  float speech_band_share;   // fraction of the frame's energy inside the speech band, 0-1
  float zero_crossing_rate;  // sign changes per sample of the 200 Hz high-passed signal, 0-1
  float voicing;             // strongest pitch-range autocorrelation, 0-1 (0 when not active)
  bool active;
  bool voiced;
};

/** What a frame changed about the utterance being tracked. */
enum class DetectorEvent : uint8_t {
  kNone,
  kSpeechStarted,     // an utterance began - speechStartFramesAgo() says where its speech began
  kSpeechEnded,       // hangover_ms of silence after speech-like sound: send it
  kSpeechDiscarded,   // it ended (silence or length cap) but didn't sound like speech: drop it - see discardReason()
  kMaxLengthReached,  // the utterance hit kMaxUtteranceMs and sounds like speech: send what there is
};

/** Why an utterance was judged not to be speech. */
enum class DiscardReason : uint8_t {
  kNone,
  kTooShort,     // under ~120 ms of voicing in all: a cough, a click, a single bark
  kTooUnvoiced,  // under a quarter of it voiced: laughter, running water, a creak, a sneeze
  kTooSteady,    // over 1 s with no syllable rhythm: a machine starting up, a bell, an alarm
};

/** Turns 20 ms frames of mic audio into utterance start/end events. One
    instance per audio stream; not thread-safe (feed it from one task). */
class VoiceActivityDetector {
 public:
  VoiceActivityDetector();

  /** Sets the sensitivity, clamped to kMinSensitivity..kMaxSensitivity. */
  void setSensitivity(uint8_t sensitivity);

  /** Sets how long a silence ends an utterance, clamped to
      kMinHangoverMs..kMaxHangoverMs. */
  void setHangoverMs(uint32_t hangover_ms);

  uint8_t sensitivity() const { return sensitivity_; }
  uint32_t hangoverMs() const { return hangover_frames_ * kFrameMs; }

  /** Starts listening afresh: forgets any utterance or onset in progress
      and allows no new start for the first ~300 ms (the floor settles). The
      noise floor learned so far is kept - it's the same room. Call whenever
      frames resume after a gap, e.g. after the board stopped speaking. */
  void restart();

  /** Analyzes the next kFrameSamples samples and returns what changed. */
  DetectorEvent processFrame(const int16_t* samples);

  /** The analysis of the last frame processed. */
  const FrameAnalysis& lastFrame() const { return last_frame_; }

  /** True from kSpeechStarted until the utterance's end event. */
  bool inUtterance() const { return in_utterance_; }

  /** Right after kSpeechStarted: how many frames before the frame that
      confirmed it the speech began (0 = that frame itself). */
  uint32_t speechStartFramesAgo() const { return speech_start_frames_ago_; }

  /** Frames since the utterance's speech began, the current one included. */
  uint32_t utteranceFrames() const { return utterance_frames_; }

  /** Voiced frames since the utterance's speech began. */
  uint32_t utteranceVoicedFrames() const { return utterance_voiced_frames_; }

  /** Frames since the last voiced one, while in an utterance (0 = this
      frame was voiced). */
  uint32_t framesSinceActivity() const { return frames_since_activity_; }

  /** Syllable dips from the utterance's start to its last active frame. */
  uint32_t utteranceDipFrames() const { return dip_frames_at_last_activity_; }

  /** Right after kSpeechDiscarded: why. kNone after any other event. */
  DiscardReason discardReason() const { return discard_reason_; }

 private:
  /** A second-order IIR filter section (direct form I), float state. */
  struct Biquad {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    float process(float input);
  };

  static constexpr uint32_t kOnsetWindowFrames = 12;     // 240 ms
  static constexpr uint32_t kRhythmWindowFrames = 10;    // 200 ms: a syllable dip is measured against this window's peak
  static constexpr uint32_t kFloorWindowFrames = 25;     // 0.5 s per floor sub-window
  static constexpr uint32_t kFloorWindowCount = 4;       // ~2 s of floor history
  // The pitch search runs on the band below 1 kHz, where a voice's harmonics
  // repeat most cleanly, at 4 kHz.
  static constexpr uint32_t kVoicingDecimation = 6;      // 24 kHz -> 4 kHz
  static constexpr uint32_t kVoicingFrameSamples = kFrameSamples / kVoicingDecimation;  // 80
  static constexpr uint32_t kVoicingBufferSamples = 2 * kVoicingFrameSamples;          // 40 ms
  static constexpr uint32_t kVoicingMinLag = 10;         // 400 Hz at 4 kHz
  static constexpr uint32_t kVoicingMaxLag = 50;         // 80 Hz at 4 kHz

  static void designHighPass(Biquad* filter, float cutoff_hz, float quality);
  static void designLowPass(Biquad* filter, float cutoff_hz, float quality);

  /** Updates the floor history with this frame's level and returns the floor. */
  float updateNoiseFloor(float level_dbfs);

  /** Strongest normalized autocorrelation over the pitch range, 0-1. */
  float measureVoicing() const;

  /** The margin above the floor a frame needs, for this sensitivity, inside
      or outside an utterance. */
  float activationMarginDb(bool continuing) const;

  /** True when this frame's level fell well below the loudest of the
      previous kRhythmWindowFrames; then adds the level to that window. */
  bool updateRhythm(float level_dbfs, float floor_dbfs);

  /** Counts this frame into the onset window; true when it confirms a start. */
  bool updateOnsetWindow(bool active, bool voiced, bool dip);

  /** Decides whether the utterance just ended sounded like speech:
      DiscardReason::kNone when it did. */
  DiscardReason judgeUtterance() const;

  /** Forgets the onset window, so the next utterance needs an onset of its own. */
  void clearOnsetWindow();

  uint8_t sensitivity_;
  uint32_t hangover_frames_;

  Biquad dc_blocker_;   // ~20 Hz high-pass, for the frame's total energy
  Biquad high_pass_;    // 200 Hz
  Biquad low_pass_a_;   // 3400 Hz, two sections = 4th-order Butterworth
  Biquad low_pass_b_;
  Biquad voicing_low_pass_a_;  // 1000 Hz, 4th-order, ahead of the 4 kHz decimation
  Biquad voicing_low_pass_b_;

  float voicing_buffer_[kVoicingBufferSamples];  // 200-1000 Hz band at 4 kHz, oldest first
  uint32_t decimation_phase_;

  float floor_window_minima_[kFloorWindowCount];
  uint32_t floor_window_next_;
  float floor_current_minimum_;
  uint32_t floor_current_frames_;

  float rhythm_levels_[kRhythmWindowFrames];  // speech-band levels of the last 200 ms, a ring
  uint32_t rhythm_next_;

  bool onset_active_[kOnsetWindowFrames];
  bool onset_voiced_[kOnsetWindowFrames];
  bool onset_dip_[kOnsetWindowFrames];
  uint32_t onset_next_;
  uint32_t onset_filled_;
  uint32_t warmup_frames_left_;

  bool in_utterance_;
  uint32_t speech_start_frames_ago_;
  uint32_t utterance_frames_;
  uint32_t utterance_voiced_frames_;
  uint32_t frames_since_activity_;
  uint32_t frames_after_start_;  // frames since the one that confirmed the start, for the length cap
  uint32_t utterance_dip_frames_;
  uint32_t dip_frames_at_last_activity_;  // dips after the last active frame are just the silence that ends it
  DiscardReason discard_reason_;

  FrameAnalysis last_frame_;
};

}  // namespace esp32va_vad
