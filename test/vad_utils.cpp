#include "vad_utils.h"

#include <math.h>
#include <string.h>

namespace esp32va_vad {

// Out-of-class definitions: needed before C++17 when these are odr-used.
constexpr uint32_t VoiceActivityDetector::kOnsetWindowFrames;
constexpr uint32_t VoiceActivityDetector::kRhythmWindowFrames;
constexpr uint32_t VoiceActivityDetector::kFloorWindowFrames;
constexpr uint32_t VoiceActivityDetector::kFloorWindowCount;
constexpr uint32_t VoiceActivityDetector::kVoicingDecimation;
constexpr uint32_t VoiceActivityDetector::kVoicingFrameSamples;
constexpr uint32_t VoiceActivityDetector::kVoicingBufferSamples;
constexpr uint32_t VoiceActivityDetector::kVoicingMinLag;
constexpr uint32_t VoiceActivityDetector::kVoicingMaxLag;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kButterworthQuality = 0.70710678f;  // 2nd-order Butterworth
// A 4th-order Butterworth low-pass is two 2nd-order sections with these Qs.
constexpr float kFourthOrderQualityA = 0.54119610f;
constexpr float kFourthOrderQualityB = 1.30656296f;
constexpr float kDcBlockerHz = 20.0f;
constexpr float kSpeechBandLowHz = 200.0f;
constexpr float kSpeechBandHighHz = 3400.0f;
constexpr float kVoicingBandHighHz = 1000.0f;

constexpr float kSilenceDbfs = -120.0f;  // reported for an all-zero frame
constexpr float kEnergyEpsilon = 1e-12f;
constexpr float kNoFloorYetDbfs = 1000.0f;  // "no data" marker in the floor history - above any real level

// Margin above the noise floor a frame needs to count as active, per
// sensitivity 1..5, and the level it needs in any case (dBFS) - so a very
// quiet room doesn't turn a distant TV into speech.
constexpr float kActivationMarginDb[kMaxSensitivity] = {16.0f, 13.0f, 10.0f, 8.0f, 6.0f};
constexpr float kMinActiveLevelDbfs[kMaxSensitivity] = {-55.0f, -60.0f, -65.0f, -70.0f, -75.0f};
// Inside an utterance the margin drops by this much, so the quiet end of a
// word doesn't count as silence (hysteresis) - but not below the minimum:
// closer to the floor than that, a fluctuating noise (rain, a vacuum
// cleaner) keeps an utterance open until the length cap.
constexpr float kContinuingMarginReliefDb = 3.0f;
constexpr float kMinContinuingMarginDb = 6.0f;

constexpr float kMinSpeechBandShare = 0.35f;      // below: mostly rumble or hiss
constexpr float kMaxSpeechZeroCrossingRate = 0.30f;  // above: hiss-like
constexpr float kMinVoicing = 0.60f;              // pitch-range autocorrelation of a voiced frame

// A syllable dip: the level fell kDipDepthDb below the loudest frame of the
// previous 200 ms - or, when that peak stood less than 15 dB above the floor
// (quiet speech in a noisy room), 60% of its height above the floor. A depth
// under kMinDipDepthDb is only a noise's wobble, never a dip.
constexpr float kDipDepthDb = 9.0f;
constexpr float kDipDepthShareOfPeakHeight = 0.6f;
constexpr float kMinDipDepthDb = 3.0f;

// Onset: of the last kOnsetWindowFrames frames (current one included and
// active), this many active and this many of those voiced.
constexpr uint32_t kOnsetMinActiveFrames = 8;   // 160 of 240 ms
constexpr uint32_t kOnsetMinVoicedFrames = 4;   //  80 ms of voicing
// Judging an utterance that ended, over its frames from the speech's start
// to its last active frame: it is discarded, not sent, with fewer voiced
// frames than kMinVoicedFramesToSend (a short "yes" has more), or fewer than
// kMinVoicedSharePercent of them voiced, or - when longer than
// kRhythmCheckMinFrames - fewer than kMinDipSharePercent of them syllable
// dips. Measured on recordings: speech dips on 13-40% of its frames, steady
// machines on under 5%, bells on under 11%.
constexpr uint32_t kMinVoicedFramesToSend = 6;   // 120 ms of voicing
constexpr uint32_t kMinVoicedSharePercent = 25;
constexpr uint32_t kRhythmCheckMinFrames = 50;   // 1 s - a shorter one may be a single syllable
constexpr uint32_t kMinDipSharePercent = 12;
// After restart(), frames that only feed the floor - no start allowed yet.
constexpr uint32_t kWarmupFrames = 15;          // 300 ms
// Frames after the start is confirmed before the length cap: the capture
// also holds kPreRollMs from before that moment.
constexpr uint32_t kMaxFramesAfterStart = (kMaxUtteranceMs - kPreRollMs) / kFrameMs;

/** Clamps value to [low, high]. */
uint32_t clampValue(uint32_t value, uint32_t low, uint32_t high) {
  return value < low ? low : (value > high ? high : value);
}

/** Energy ratio to dB, with all-zero input floored at kSilenceDbfs. */
float energyToDb(float mean_square) {
  return mean_square > kEnergyEpsilon ? 10.0f * log10f(mean_square) : kSilenceDbfs;
}

}  // namespace

float VoiceActivityDetector::Biquad::process(float input) {
  float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
  x2 = x1;
  x1 = input;
  y2 = y1;
  y1 = output;
  return output;
}

void VoiceActivityDetector::designHighPass(Biquad* filter, float cutoff_hz, float quality) {
  // Bilinear-transform high-pass (the "Audio EQ Cookbook" form), normalized by a0.
  float omega = 2.0f * kPi * cutoff_hz / kSampleRateHz;
  float cos_omega = cosf(omega);
  float alpha = sinf(omega) / (2.0f * quality);
  float a0 = 1.0f + alpha;
  filter->b0 = (1.0f + cos_omega) / 2.0f / a0;
  filter->b1 = -(1.0f + cos_omega) / a0;
  filter->b2 = (1.0f + cos_omega) / 2.0f / a0;
  filter->a1 = -2.0f * cos_omega / a0;
  filter->a2 = (1.0f - alpha) / a0;
  filter->x1 = filter->x2 = filter->y1 = filter->y2 = 0.0f;
}

void VoiceActivityDetector::designLowPass(Biquad* filter, float cutoff_hz, float quality) {
  float omega = 2.0f * kPi * cutoff_hz / kSampleRateHz;
  float cos_omega = cosf(omega);
  float alpha = sinf(omega) / (2.0f * quality);
  float a0 = 1.0f + alpha;
  filter->b0 = (1.0f - cos_omega) / 2.0f / a0;
  filter->b1 = (1.0f - cos_omega) / a0;
  filter->b2 = (1.0f - cos_omega) / 2.0f / a0;
  filter->a1 = -2.0f * cos_omega / a0;
  filter->a2 = (1.0f - alpha) / a0;
  filter->x1 = filter->x2 = filter->y1 = filter->y2 = 0.0f;
}

VoiceActivityDetector::VoiceActivityDetector()
    : sensitivity_(kDefaultSensitivity),
      hangover_frames_(kDefaultHangoverMs / kFrameMs),
      decimation_phase_(0),
      floor_window_next_(0),
      floor_current_minimum_(kNoFloorYetDbfs),
      floor_current_frames_(0),
      rhythm_next_(0),
      onset_next_(0),
      onset_filled_(0),
      warmup_frames_left_(kWarmupFrames),
      in_utterance_(false),
      speech_start_frames_ago_(0),
      utterance_frames_(0),
      utterance_voiced_frames_(0),
      frames_since_activity_(0),
      frames_after_start_(0),
      utterance_dip_frames_(0),
      dip_frames_at_last_activity_(0),
      discard_reason_(DiscardReason::kNone),
      last_frame_() {
  designHighPass(&dc_blocker_, kDcBlockerHz, kButterworthQuality);
  designHighPass(&high_pass_, kSpeechBandLowHz, kButterworthQuality);
  designLowPass(&low_pass_a_, kSpeechBandHighHz, kFourthOrderQualityA);
  designLowPass(&low_pass_b_, kSpeechBandHighHz, kFourthOrderQualityB);
  designLowPass(&voicing_low_pass_a_, kVoicingBandHighHz, kFourthOrderQualityA);
  designLowPass(&voicing_low_pass_b_, kVoicingBandHighHz, kFourthOrderQualityB);
  memset(voicing_buffer_, 0, sizeof(voicing_buffer_));
  for (uint32_t index = 0; index < kFloorWindowCount; index++) {
    floor_window_minima_[index] = kNoFloorYetDbfs;
  }
  for (uint32_t index = 0; index < kRhythmWindowFrames; index++) {
    rhythm_levels_[index] = kSilenceDbfs;
  }
  clearOnsetWindow();
  last_frame_.speech_band_dbfs = kSilenceDbfs;
  last_frame_.noise_floor_dbfs = kSilenceDbfs;
  last_frame_.threshold_dbfs = kSilenceDbfs;
}

void VoiceActivityDetector::setSensitivity(uint8_t sensitivity) {
  sensitivity_ = static_cast<uint8_t>(clampValue(sensitivity, kMinSensitivity, kMaxSensitivity));
}

void VoiceActivityDetector::setHangoverMs(uint32_t hangover_ms) {
  hangover_frames_ = clampValue(hangover_ms, kMinHangoverMs, kMaxHangoverMs) / kFrameMs;
}

void VoiceActivityDetector::restart() {
  in_utterance_ = false;
  speech_start_frames_ago_ = 0;
  utterance_frames_ = 0;
  utterance_voiced_frames_ = 0;
  frames_since_activity_ = 0;
  frames_after_start_ = 0;
  utterance_dip_frames_ = 0;
  dip_frames_at_last_activity_ = 0;
  discard_reason_ = DiscardReason::kNone;
  clearOnsetWindow();
  warmup_frames_left_ = kWarmupFrames;
  // The audio before the gap is unrelated to what comes next.
  memset(voicing_buffer_, 0, sizeof(voicing_buffer_));
  decimation_phase_ = 0;
  for (uint32_t index = 0; index < kRhythmWindowFrames; index++) {
    rhythm_levels_[index] = kSilenceDbfs;
  }
  rhythm_next_ = 0;
}

void VoiceActivityDetector::clearOnsetWindow() {
  memset(onset_active_, 0, sizeof(onset_active_));
  memset(onset_voiced_, 0, sizeof(onset_voiced_));
  memset(onset_dip_, 0, sizeof(onset_dip_));
  onset_next_ = 0;
  onset_filled_ = 0;
}

float VoiceActivityDetector::updateNoiseFloor(float level_dbfs) {
  if (level_dbfs < floor_current_minimum_) {
    floor_current_minimum_ = level_dbfs;
  }
  floor_current_frames_++;
  float floor_dbfs = floor_current_minimum_;
  for (uint32_t index = 0; index < kFloorWindowCount; index++) {
    if (floor_window_minima_[index] < floor_dbfs) {
      floor_dbfs = floor_window_minima_[index];
    }
  }
  if (floor_current_frames_ >= kFloorWindowFrames) {
    // Close this sub-window: the oldest one drops out of the history, which
    // is how the floor rises again once a noise has stopped... or started.
    floor_window_minima_[floor_window_next_] = floor_current_minimum_;
    floor_window_next_ = (floor_window_next_ + 1) % kFloorWindowCount;
    floor_current_minimum_ = kNoFloorYetDbfs;
    floor_current_frames_ = 0;
  }
  return floor_dbfs;
}

float VoiceActivityDetector::measureVoicing() const {
  // Normalized autocorrelation r(lag) = sum x[n]x[n+lag] / sqrt(E0 * Elag),
  // over the same span length for every lag in the 80-400 Hz pitch range.
  const uint32_t span = kVoicingBufferSamples - kVoicingMaxLag;
  const float* samples = voicing_buffer_;
  float energy_start = 0.0f;
  for (uint32_t n = 0; n < span; n++) {
    energy_start += samples[n] * samples[n];
  }
  if (energy_start <= kEnergyEpsilon) {
    return 0.0f;
  }
  float energy_lagged = 0.0f;
  for (uint32_t n = 0; n < span; n++) {
    energy_lagged += samples[n + kVoicingMinLag] * samples[n + kVoicingMinLag];
  }
  float strongest = 0.0f;
  for (uint32_t lag = kVoicingMinLag; lag <= kVoicingMaxLag; lag++) {
    float correlation = 0.0f;
    for (uint32_t n = 0; n < span; n++) {
      correlation += samples[n] * samples[n + lag];
    }
    float normalizer = energy_start * energy_lagged;
    if (normalizer > kEnergyEpsilon) {
      float normalized = correlation / sqrtf(normalizer);
      if (normalized > strongest) {
        strongest = normalized;
      }
    }
    if (lag < kVoicingMaxLag) {
      // Slide the lagged span one sample later for the next lag.
      energy_lagged += samples[span + lag] * samples[span + lag] - samples[lag] * samples[lag];
    }
  }
  return strongest > 1.0f ? 1.0f : strongest;
}

float VoiceActivityDetector::activationMarginDb(bool continuing) const {
  float margin = kActivationMarginDb[sensitivity_ - kMinSensitivity];
  if (!continuing) {
    return margin;
  }
  float relieved = margin - kContinuingMarginReliefDb;
  float lowest = margin < kMinContinuingMarginDb ? margin : kMinContinuingMarginDb;
  return relieved > lowest ? relieved : lowest;
}

bool VoiceActivityDetector::updateRhythm(float level_dbfs, float floor_dbfs) {
  float recent_peak = kSilenceDbfs;
  for (uint32_t index = 0; index < kRhythmWindowFrames; index++) {
    if (rhythm_levels_[index] > recent_peak) {
      recent_peak = rhythm_levels_[index];
    }
  }
  rhythm_levels_[rhythm_next_] = level_dbfs;
  rhythm_next_ = (rhythm_next_ + 1) % kRhythmWindowFrames;
  float depth_needed = (recent_peak - floor_dbfs) * kDipDepthShareOfPeakHeight;
  if (depth_needed > kDipDepthDb) {
    depth_needed = kDipDepthDb;
  }
  return depth_needed >= kMinDipDepthDb && level_dbfs <= recent_peak - depth_needed;
}

DiscardReason VoiceActivityDetector::judgeUtterance() const {
  // From the speech's start to its last active frame: the silence that
  // ended it says nothing about what it was.
  uint32_t speech_frames = utterance_frames_ - frames_since_activity_;
  if (utterance_voiced_frames_ < kMinVoicedFramesToSend) {
    return DiscardReason::kTooShort;
  }
  if (utterance_voiced_frames_ * 100 < speech_frames * kMinVoicedSharePercent) {
    return DiscardReason::kTooUnvoiced;
  }
  if (speech_frames >= kRhythmCheckMinFrames && dip_frames_at_last_activity_ * 100 < speech_frames * kMinDipSharePercent) {
    return DiscardReason::kTooSteady;
  }
  return DiscardReason::kNone;
}

bool VoiceActivityDetector::updateOnsetWindow(bool active, bool voiced, bool dip) {
  onset_active_[onset_next_] = active;
  onset_voiced_[onset_next_] = voiced;
  onset_dip_[onset_next_] = dip;
  onset_next_ = (onset_next_ + 1) % kOnsetWindowFrames;
  if (onset_filled_ < kOnsetWindowFrames) {
    onset_filled_++;
  }
  if (warmup_frames_left_ > 0) {
    warmup_frames_left_--;
    return false;
  }
  if (!active) {
    return false;
  }
  uint32_t active_count = 0;
  uint32_t voiced_count = 0;
  for (uint32_t index = 0; index < kOnsetWindowFrames; index++) {
    active_count += onset_active_[index] ? 1 : 0;
    voiced_count += onset_voiced_[index] ? 1 : 0;
  }
  return active_count >= kOnsetMinActiveFrames && voiced_count >= kOnsetMinVoicedFrames;
}

DetectorEvent VoiceActivityDetector::processFrame(const int16_t* samples) {
  float total_energy = 0.0f;
  float band_energy = 0.0f;
  uint32_t zero_crossings = 0;
  bool previous_positive = high_pass_.y1 >= 0.0f;
  const uint32_t voicing_shift = kVoicingFrameSamples;
  // Make room for this frame's decimated samples at the end of the buffer.
  memmove(voicing_buffer_, voicing_buffer_ + voicing_shift,
          (kVoicingBufferSamples - voicing_shift) * sizeof(voicing_buffer_[0]));
  float* voicing_out = voicing_buffer_ + (kVoicingBufferSamples - voicing_shift);
  uint32_t voicing_written = 0;

  for (uint32_t n = 0; n < kFrameSamples; n++) {
    float sample = samples[n] / 32768.0f;
    float without_dc = dc_blocker_.process(sample);
    float high_passed = high_pass_.process(sample);
    float band = low_pass_b_.process(low_pass_a_.process(high_passed));
    total_energy += without_dc * without_dc;
    band_energy += band * band;
    bool positive = high_passed >= 0.0f;
    if (positive != previous_positive) {
      zero_crossings++;
    }
    previous_positive = positive;
    float voicing_band = voicing_low_pass_b_.process(voicing_low_pass_a_.process(high_passed));
    if (decimation_phase_ == 0 && voicing_written < voicing_shift) {
      voicing_out[voicing_written++] = voicing_band;
    }
    decimation_phase_ = (decimation_phase_ + 1) % kVoicingDecimation;
  }
  while (voicing_written < voicing_shift) {
    voicing_out[voicing_written++] = 0.0f;  // only if the decimation phase drifted - keeps the buffer aligned
  }

  FrameAnalysis& frame = last_frame_;
  frame.speech_band_dbfs = energyToDb(band_energy / kFrameSamples);
  frame.noise_floor_dbfs = updateNoiseFloor(frame.speech_band_dbfs);
  float level_needed = frame.noise_floor_dbfs + activationMarginDb(in_utterance_);
  float absolute_minimum = kMinActiveLevelDbfs[sensitivity_ - kMinSensitivity];
  frame.threshold_dbfs = level_needed > absolute_minimum ? level_needed : absolute_minimum;
  frame.speech_band_share = total_energy > kEnergyEpsilon ? band_energy / total_energy : 0.0f;
  frame.zero_crossing_rate = static_cast<float>(zero_crossings) / kFrameSamples;
  frame.active = frame.speech_band_dbfs >= frame.threshold_dbfs && frame.speech_band_share >= kMinSpeechBandShare &&
                 frame.zero_crossing_rate <= kMaxSpeechZeroCrossingRate;
  // The pitch search is the costly part - only for frames that could be speech.
  frame.voicing = frame.active ? measureVoicing() : 0.0f;
  frame.voiced = frame.active && frame.voicing >= kMinVoicing;
  bool dip = updateRhythm(frame.speech_band_dbfs, frame.noise_floor_dbfs);
  discard_reason_ = DiscardReason::kNone;

  if (!in_utterance_) {
    if (!updateOnsetWindow(frame.active, frame.voiced, dip)) {
      return DetectorEvent::kNone;
    }
    // Confirmed: the speech began at the oldest active frame still in the
    // window; what the window saw from there on counts toward the utterance.
    uint32_t oldest_active_age = 0;
    for (uint32_t age = 0; age < onset_filled_; age++) {
      if (onset_active_[(onset_next_ + kOnsetWindowFrames - 1 - age) % kOnsetWindowFrames]) {
        oldest_active_age = age;
      }
    }
    uint32_t voiced_count = 0;
    uint32_t dip_count = 0;
    for (uint32_t age = 0; age <= oldest_active_age; age++) {
      uint32_t index = (onset_next_ + kOnsetWindowFrames - 1 - age) % kOnsetWindowFrames;
      voiced_count += onset_voiced_[index] ? 1 : 0;
      dip_count += onset_dip_[index] ? 1 : 0;
    }
    in_utterance_ = true;
    speech_start_frames_ago_ = oldest_active_age;
    utterance_frames_ = oldest_active_age + 1;
    utterance_voiced_frames_ = voiced_count;
    utterance_dip_frames_ = dip_count;
    dip_frames_at_last_activity_ = dip_count;  // this frame is active - the onset needs it to be
    frames_since_activity_ = 0;
    frames_after_start_ = 0;
    return DetectorEvent::kSpeechStarted;
  }

  utterance_frames_++;
  frames_after_start_++;
  if (frame.voiced) {
    utterance_voiced_frames_++;
  }
  if (dip) {
    utterance_dip_frames_++;
  }
  if (frame.active) {
    frames_since_activity_ = 0;
    dip_frames_at_last_activity_ = utterance_dip_frames_;
  } else {
    frames_since_activity_++;
  }

  bool at_length_cap = frames_after_start_ >= kMaxFramesAfterStart;
  if (!at_length_cap && frames_since_activity_ < hangover_frames_) {
    return DetectorEvent::kNone;
  }
  discard_reason_ = judgeUtterance();
  in_utterance_ = false;
  clearOnsetWindow();  // the next utterance needs a fresh onset of its own
  if (discard_reason_ != DiscardReason::kNone) {
    return DetectorEvent::kSpeechDiscarded;
  }
  return at_length_cap ? DetectorEvent::kMaxLengthReached : DetectorEvent::kSpeechEnded;
}

}  // namespace esp32va_vad
