#include "mic_capture_utils.h"

#include <Arduino.h>
#include "i2s_audio_utils.h"
#include "log_utils.h"

namespace esp32va_mic_capture {

namespace {

using esp32va_vad::DetectorEvent;
using esp32va_vad::kFrameSamples;

constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 3;  // above loop()'s 1, so a frame never waits for loop()
// loop()'s core: the task then always finishes a frame before loop() runs
// again, so loop() never sees half of one. (Wi-Fi runs on the other core.)
constexpr BaseType_t kTaskCore = 1;
constexpr uint32_t kPreRollSamples = esp32va_vad::kPreRollMs * esp32va_vad::kSampleRateHz / 1000;
// The oldest unsent sample is kept at least this far from the frame being written.
constexpr uint32_t kGuardSamples = 2 * kFrameSamples;
constexpr uint32_t kEventQueueSize = 4;

// Shared between the task and loop() - only touched under state_lock.
portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
CaptureMode mode = CaptureMode::kOff;
uint8_t* ring = nullptr;     // the borrowed playback ring (mu-law, a byte per sample), nullptr while not borrowed
uint32_t ring_capacity = 0;  // in samples
// Absolute sample positions, not ring indices: position p lives at
// ring[p % ring_capacity]. They wrap after ~50 hours of listening; the
// differences between them stay right regardless (unsigned arithmetic).
uint32_t written_position = 0;        // one past the newest sample in the ring
uint32_t read_position = 0;           // the next sample loop() takes
uint32_t capture_start_position = 0;  // where the current capture begins (pre-roll included)
uint32_t capture_end_position = 0;    // kManualCapture: where it will stop; kDraining: where it stopped
uint32_t armed_position = 0;          // where listening started - older ring contents aren't this session's mic audio
bool frame_write_in_progress = false;
bool detector_restart_requested = false;
bool detector_settings_changed = false;
uint8_t requested_sensitivity = esp32va_vad::kDefaultSensitivity;
uint32_t requested_hangover_ms = esp32va_vad::kDefaultHangoverMs;
CaptureEvent event_queue[kEventQueueSize];
uint32_t event_queue_first = 0;
uint32_t event_queue_count = 0;
CaptureStats stats = {};
LevelReading latest_level = {};
bool latest_level_valid = false;

// The task's own.
TaskHandle_t task_handle = nullptr;
esp32va_vad::VoiceActivityDetector detector;
int32_t raw_frame[kFrameSamples];  // static: too big for the task's stack
int16_t frame_samples[kFrameSamples];

/** Queues an event for loop(). Call with state_lock held. If loop() left
    kEventQueueSize untaken, the oldest is dropped (counted as unexpected). */
void pushEventLocked(CaptureEvent event) {
  if (event_queue_count == kEventQueueSize) {
    event_queue_first = (event_queue_first + 1) % kEventQueueSize;
    event_queue_count--;
    stats.unexpected_events++;
  }
  event_queue[(event_queue_first + event_queue_count) % kEventQueueSize] = event;
  event_queue_count++;
}

/** Forgets every queued event. Call with state_lock held. */
void clearEventsLocked() {
  event_queue_first = 0;
  event_queue_count = 0;
}

/** Where the samples loop() may take end right now (== read_position when
    there are none). Call with state_lock held. An if-chain, not a switch:
    nothing may be logged while the lock is held. */
uint32_t readyEndLocked() {
  if (mode == CaptureMode::kVoiceCapture) {
    return written_position;
  }
  if (mode == CaptureMode::kManualCapture) {
    // Its end is ahead of what's written until the last frame arrives.
    bool end_reached = static_cast<int32_t>(written_position - capture_end_position) >= 0;
    return end_reached ? capture_end_position : written_position;
  }
  if (mode == CaptureMode::kDraining) {
    return capture_end_position;
  }
  return read_position;  // kOff, kArmed: nothing to take
}

/** Keeps loop()'s unsent audio clear of the next frame to be written, by
    dropping its oldest samples when loop() falls too far behind. Call with
    state_lock held, while capturing. */
void dropOverflowLocked() {
  uint32_t backlog = written_position - read_position;
  if (backlog > stats.max_backlog_samples) {
    stats.max_backlog_samples = backlog;
  }
  uint32_t room = ring_capacity - kGuardSamples;
  if (backlog > room) {
    uint32_t lost = backlog - room;
    read_position += lost;
    stats.dropped_samples += lost;
  }
}

/** Copies what the detector just said about the utterance into stats. Call
    with state_lock held, right after an utterance's end event. */
void recordUtteranceLocked(DetectorEvent event, unsigned long last_active_frame_ms) {
  stats.speech_samples = (detector.utteranceFrames() - detector.framesSinceActivity()) * kFrameSamples;
  stats.voiced_frames = detector.utteranceVoicedFrames();
  stats.dip_frames = detector.utteranceDipFrames();
  stats.speech_end_ms = last_active_frame_ms;
  stats.end_event = event;
  stats.discard_reason = detector.discardReason();
}

/** Acts on a detector event for a frame handled in frame_mode. Call with
    state_lock held, after the frame was counted into written_position. */
void handleDetectorEventLocked(CaptureMode frame_mode, DetectorEvent event, unsigned long last_active_frame_ms) {
  if (event == DetectorEvent::kNone) {
    return;
  }
  if (frame_mode == CaptureMode::kArmed && event == DetectorEvent::kSpeechStarted) {
    // The capture starts kPreRollMs back - or as far back as there is
    // audio from this listening session, if that's less.
    uint32_t pre_roll = kPreRollSamples;
    uint32_t since_armed = written_position - armed_position;
    if (pre_roll > since_armed) {
      pre_roll = since_armed;
    }
    if (pre_roll > ring_capacity - kGuardSamples) {
      pre_roll = ring_capacity - kGuardSamples;
    }
    read_position = written_position - pre_roll;
    capture_start_position = read_position;
    uint32_t read_gap_so_far_ms = stats.max_read_gap_ms;  // the pre-roll was read while armed
    stats = {};
    stats.max_read_gap_ms = read_gap_so_far_ms;
    stats.pre_roll_samples = pre_roll;
    stats.max_backlog_samples = pre_roll;  // all of it waits for loop() right now
    mode = CaptureMode::kVoiceCapture;
    pushEventLocked(CaptureEvent::kVoiceStarted);
    return;
  }
  if (frame_mode == CaptureMode::kVoiceCapture &&
      (event == DetectorEvent::kSpeechEnded || event == DetectorEvent::kMaxLengthReached)) {
    capture_end_position = written_position;
    stats.captured_samples = capture_end_position - capture_start_position;
    recordUtteranceLocked(event, last_active_frame_ms);
    mode = CaptureMode::kDraining;
    pushEventLocked(CaptureEvent::kCaptureEnded);
    return;
  }
  if (frame_mode == CaptureMode::kVoiceCapture && event == DetectorEvent::kSpeechDiscarded) {
    stats.captured_samples = written_position - capture_start_position;
    recordUtteranceLocked(event, last_active_frame_ms);
    read_position = written_position;  // nothing more to take; keep listening
    mode = CaptureMode::kArmed;
    pushEventLocked(CaptureEvent::kCaptureDiscarded);
    return;
  }
  stats.unexpected_events++;
}

/** Stores one frame in the ring at an absolute position, as mu-law, wrapping. */
void writeFrameToRing(uint8_t* target, uint32_t capacity, uint32_t position, const int16_t* samples) {
  uint32_t offset = position % capacity;
  uint32_t first_part = capacity - offset < kFrameSamples ? capacity - offset : kFrameSamples;
  for (uint32_t i = 0; i < first_part; i++) {
    target[offset + i] = esp32va_audio::linearPcm16ToUlaw(samples[i]);
  }
  for (uint32_t i = first_part; i < kFrameSamples; i++) {
    target[i - first_part] = esp32va_audio::linearPcm16ToUlaw(samples[i]);
  }
}

/** The mic task: reads a frame every 20 ms, forever, and handles it as the
    mode says. Never prints - see the header comment. */
void micTask(void* /*unused*/) {
  unsigned long previous_read_ms = millis();
  unsigned long last_active_frame_ms = 0;
  for (;;) {
    esp32va_audio::readMicSamples(raw_frame, frame_samples, kFrameSamples);
    unsigned long now_ms = millis();
    uint32_t read_gap_ms = now_ms - previous_read_ms;
    previous_read_ms = now_ms;

    portENTER_CRITICAL(&state_lock);
    CaptureMode frame_mode = mode;
    bool keeps_frame = frame_mode == CaptureMode::kArmed || frame_mode == CaptureMode::kVoiceCapture ||
                       frame_mode == CaptureMode::kManualCapture;
    frame_write_in_progress = keeps_frame;
    if (keeps_frame && read_gap_ms > stats.max_read_gap_ms) {
      stats.max_read_gap_ms = read_gap_ms;
    }
    bool restart_detector = detector_restart_requested;
    detector_restart_requested = false;
    bool apply_settings = detector_settings_changed;
    detector_settings_changed = false;
    uint8_t sensitivity = requested_sensitivity;
    uint32_t hangover_ms = requested_hangover_ms;
    uint32_t write_position = written_position;
    uint8_t* target = ring;
    uint32_t capacity = ring_capacity;
    portEXIT_CRITICAL(&state_lock);
    if (!keeps_frame) {
      continue;  // kOff or kDraining: thrown away
    }

    if (apply_settings) {
      detector.setSensitivity(sensitivity);
      detector.setHangoverMs(hangover_ms);
    }
    if (restart_detector) {
      detector.restart();
    }
    DetectorEvent detector_event = DetectorEvent::kNone;
    bool detecting = frame_mode == CaptureMode::kArmed || frame_mode == CaptureMode::kVoiceCapture;
    if (detecting) {
      detector_event = detector.processFrame(frame_samples);
      if (detector.lastFrame().active) {
        last_active_frame_ms = now_ms;
      }
    }
    writeFrameToRing(target, capacity, write_position, frame_samples);

    portENTER_CRITICAL(&state_lock);
    frame_write_in_progress = false;
    if (mode != frame_mode) {
      // loop() changed the mode while this frame was being handled, and set
      // things up from written_position on - this frame doesn't count.
      portEXIT_CRITICAL(&state_lock);
      continue;
    }
    written_position += kFrameSamples;
    if (frame_mode == CaptureMode::kVoiceCapture || frame_mode == CaptureMode::kManualCapture) {
      dropOverflowLocked();
    }
    if (detecting) {
      const esp32va_vad::FrameAnalysis& analysis = detector.lastFrame();
      latest_level.level_dbfs = analysis.speech_band_dbfs;
      latest_level.floor_dbfs = analysis.noise_floor_dbfs;
      latest_level.threshold_dbfs = analysis.threshold_dbfs;
      latest_level.active = analysis.active;
      latest_level_valid = true;
    }
    handleDetectorEventLocked(frame_mode, detector_event, last_active_frame_ms);
    if (frame_mode == CaptureMode::kManualCapture &&
        static_cast<int32_t>(written_position - capture_end_position) >= 0) {
      stats.captured_samples = capture_end_position - capture_start_position;
      mode = CaptureMode::kDraining;
      pushEventLocked(CaptureEvent::kCaptureEnded);
    }
    portEXIT_CRITICAL(&state_lock);
  }
}

/** Makes sure the ring is borrowed for the task. False if it's busy
    (playing, or a reply is still being saved). */
bool borrowRing() {
  portENTER_CRITICAL(&state_lock);
  bool already_borrowed = ring != nullptr;
  portEXIT_CRITICAL(&state_lock);
  if (already_borrowed) {
    return true;
  }
  uint32_t capacity = 0;
  uint8_t* borrowed = esp32va_audio::lendRingToMic(&capacity);
  if (borrowed == nullptr) {
    return false;
  }
  portENTER_CRITICAL(&state_lock);
  ring = borrowed;
  ring_capacity = capacity;
  read_position = written_position;
  portEXIT_CRITICAL(&state_lock);
  return true;
}

/** True while a capture is running or still being taken. */
bool isCapturing(CaptureMode current_mode) {
  return current_mode == CaptureMode::kVoiceCapture || current_mode == CaptureMode::kManualCapture ||
         current_mode == CaptureMode::kDraining;
}

}  // namespace

bool beginMicCapture() {
  if (task_handle != nullptr) {
    return true;
  }
  BaseType_t created = xTaskCreatePinnedToCore(micTask, "mic_capture", kTaskStackBytes, nullptr, kTaskPriority,
                                               &task_handle, kTaskCore);
  if (created != pdPASS) {
    task_handle = nullptr;
    LOG_ERROR("Couldn't start the mic task (out of memory?) - nothing can be recorded.\n");
    return false;
  }
  LOG_INFO("Mic task started - it reads the mic every %u ms from now on.\n", (unsigned)esp32va_vad::kFrameMs);
  return true;
}

bool armVoiceTrigger(uint8_t sensitivity, uint32_t hangover_ms) {
  if (task_handle == nullptr || isCapturing(captureMode()) || !borrowRing()) {
    return false;
  }
  portENTER_CRITICAL(&state_lock);
  requested_sensitivity = sensitivity;
  requested_hangover_ms = hangover_ms;
  detector_settings_changed = true;
  detector_restart_requested = true;
  armed_position = written_position;
  read_position = written_position;
  stats = {};
  latest_level_valid = false;
  clearEventsLocked();
  mode = CaptureMode::kArmed;
  portEXIT_CRITICAL(&state_lock);
  return true;
}

void setDetectorSettings(uint8_t sensitivity, uint32_t hangover_ms) {
  portENTER_CRITICAL(&state_lock);
  requested_sensitivity = sensitivity;
  requested_hangover_ms = hangover_ms;
  detector_settings_changed = true;
  portEXIT_CRITICAL(&state_lock);
}

bool startManualCapture(uint32_t sample_count) {
  if (task_handle == nullptr || isCapturing(captureMode()) || !borrowRing()) {
    return false;
  }
  portENTER_CRITICAL(&state_lock);
  read_position = written_position;
  capture_start_position = written_position;
  capture_end_position = written_position + sample_count;
  stats = {};
  latest_level_valid = false;
  clearEventsLocked();
  mode = CaptureMode::kManualCapture;
  portEXIT_CRITICAL(&state_lock);
  return true;
}

void stopCapture() {
  portENTER_CRITICAL(&state_lock);
  mode = CaptureMode::kOff;
  clearEventsLocked();
  latest_level_valid = false;
  bool borrowed = ring != nullptr;
  portEXIT_CRITICAL(&state_lock);
  if (!borrowed) {
    return;
  }
  // On loop()'s core the task can't be in the middle of a frame here, but
  // wait one out anyway before playback gets the ring back.
  for (;;) {
    portENTER_CRITICAL(&state_lock);
    bool busy = frame_write_in_progress;
    portEXIT_CRITICAL(&state_lock);
    if (!busy) {
      break;
    }
    vTaskDelay(1);
  }
  portENTER_CRITICAL(&state_lock);
  ring = nullptr;
  ring_capacity = 0;
  portEXIT_CRITICAL(&state_lock);
  esp32va_audio::returnRingFromMic();
}

CaptureMode captureMode() {
  portENTER_CRITICAL(&state_lock);
  CaptureMode current = mode;
  portEXIT_CRITICAL(&state_lock);
  return current;
}

bool isCaptureInProgress() {
  return isCapturing(captureMode());
}

CaptureEvent takeCaptureEvent() {
  CaptureEvent event = CaptureEvent::kNone;
  portENTER_CRITICAL(&state_lock);
  if (event_queue_count > 0) {
    event = event_queue[event_queue_first];
    event_queue_first = (event_queue_first + 1) % kEventQueueSize;
    event_queue_count--;
  }
  portEXIT_CRITICAL(&state_lock);
  return event;
}

uint32_t captureSamplesReady() {
  portENTER_CRITICAL(&state_lock);
  uint32_t ready = readyEndLocked() - read_position;
  portEXIT_CRITICAL(&state_lock);
  return ready;
}

uint32_t takeCaptureSamples(int16_t* out, uint32_t max_samples) {
  portENTER_CRITICAL(&state_lock);
  uint32_t ready = readyEndLocked() - read_position;
  uint32_t count = ready < max_samples ? ready : max_samples;
  uint32_t start = read_position;
  const uint8_t* source = ring;
  uint32_t capacity = ring_capacity;
  portEXIT_CRITICAL(&state_lock);
  if (count == 0 || source == nullptr) {
    return 0;
  }
  uint32_t offset = start % capacity;
  uint32_t first_part = capacity - offset < count ? capacity - offset : count;
  for (uint32_t i = 0; i < first_part; i++) {
    out[i] = esp32va_audio::ulawToLinearPcm16(source[offset + i]);
  }
  for (uint32_t i = first_part; i < count; i++) {
    out[i] = esp32va_audio::ulawToLinearPcm16(source[i - first_part]);
  }
  // If the task skipped read_position ahead meanwhile (overflow) or the
  // capture was discarded, what was copied may be overwritten - drop it.
  portENTER_CRITICAL(&state_lock);
  bool intact = read_position == start;
  if (intact) {
    read_position = start + count;
  }
  portEXIT_CRITICAL(&state_lock);
  return intact ? count : 0;
}

bool isCaptureDrained() {
  portENTER_CRITICAL(&state_lock);
  bool drained = mode == CaptureMode::kDraining && read_position == capture_end_position;
  portEXIT_CRITICAL(&state_lock);
  return drained;
}

void getCaptureStats(CaptureStats* out) {
  portENTER_CRITICAL(&state_lock);
  *out = stats;
  portEXIT_CRITICAL(&state_lock);
}

bool readLevel(LevelReading* reading) {
  portENTER_CRITICAL(&state_lock);
  bool listening = latest_level_valid && (mode == CaptureMode::kArmed || mode == CaptureMode::kVoiceCapture);
  if (listening) {
    *reading = latest_level;
  }
  portEXIT_CRITICAL(&state_lock);
  return listening;
}

}  // namespace esp32va_mic_capture
