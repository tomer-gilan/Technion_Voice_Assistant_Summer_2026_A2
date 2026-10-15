#include "measurement_utils.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include "heap_diagnostics_utils.h"
#include "status_report_utils.h"

namespace esp32va_measure {

namespace {

constexpr size_t kBootMilestoneCount = static_cast<size_t>(BootMilestone::kCount);
constexpr size_t kTurnEventCount = static_cast<size_t>(TurnEvent::kCount);
constexpr uint32_t kMuLawBytesPerSecond = 8000;  // 8kHz, 1 byte per sample - matches the session's audio/pcmu output
constexpr double kFullScaleSample = 32768.0;
constexpr double kSilenceFloorDbfs = -96.0;
constexpr size_t kToolNameBufferSize = 32;

unsigned long boot_milestone_ms[kBootMilestoneCount] = {};
bool boot_milestone_set[kBootMilestoneCount] = {};
bool boot_timing_reported = false;

struct TurnMeasurements {
  uint32_t listen_number;
  unsigned long started_ms;
  unsigned long event_ms[kTurnEventCount];
  bool event_set[kTurnEventCount];
  uint32_t sent_bytes;
  uint32_t send_ms;
  int32_t peak_abs_sample;
  double rms_sample;
  uint32_t clipped_samples;
  uint32_t chunk_max_ms;
  uint32_t chunk_avg_ms;
  uint32_t reply_base64_chars;
  uint32_t backpressure_pauses;
  int32_t heard_chars;  // -1 until the input transcript arrives
  char tool_name[kToolNameBufferSize];
  bool reported;
};

TurnMeasurements turn = {};
bool turn_active = false;

/** Milliseconds between two boot milestones, or -1 if either never happened. */
long bootStageMs(BootMilestone from, BootMilestone to) {
  size_t from_index = static_cast<size_t>(from);
  size_t to_index = static_cast<size_t>(to);
  if (!boot_milestone_set[from_index] || !boot_milestone_set[to_index]) return -1;
  return static_cast<long>(boot_milestone_ms[to_index] - boot_milestone_ms[from_index]);
}

/** Milliseconds from the end of recording to `event`, or -1 if either didn't happen. */
long msAfterRecording(TurnEvent event) {
  size_t reference = static_cast<size_t>(TurnEvent::kRecordingDone);
  size_t index = static_cast<size_t>(event);
  if (!turn.event_set[reference] || !turn.event_set[index]) return -1;
  return static_cast<long>(turn.event_ms[index] - turn.event_ms[reference]);
}

/** Milliseconds between two turn events, or -1 if either didn't happen. */
long msBetween(TurnEvent from, TurnEvent to) {
  size_t from_index = static_cast<size_t>(from);
  size_t to_index = static_cast<size_t>(to);
  if (!turn.event_set[from_index] || !turn.event_set[to_index]) return -1;
  return static_cast<long>(turn.event_ms[to_index] - turn.event_ms[from_index]);
}

/** A raw 16-bit sample level as dBFS, with silence floored at -96. */
double toDbfs(double sample_level) {
  return sample_level >= 1.0 ? 20.0 * log10(sample_level / kFullScaleSample) : kSilenceFloorDbfs;
}

}  // namespace

void markBootMilestone(BootMilestone milestone) {
  size_t index = static_cast<size_t>(milestone);
  if (index >= kBootMilestoneCount || boot_milestone_set[index]) return;
  boot_milestone_ms[index] = millis();
  boot_milestone_set[index] = true;
}

void reportBootTiming() {
  if (boot_timing_reported) return;
  boot_timing_reported = true;
  // millis() starts at boot, so the first stage is measured from 0.
  long hardware_ms = boot_milestone_set[static_cast<size_t>(BootMilestone::kHardwareReady)]
                         ? static_cast<long>(boot_milestone_ms[static_cast<size_t>(BootMilestone::kHardwareReady)])
                         : -1;
  long total_ms = boot_milestone_set[static_cast<size_t>(BootMilestone::kSessionReady)]
                      ? static_cast<long>(boot_milestone_ms[static_cast<size_t>(BootMilestone::kSessionReady)])
                      : -1;
  esp32va_status::printProtocolLine(
      "@@BOOT_TIMING hw_ms=%ld sd_ms=%ld wifi_ms=%ld time_ms=%ld connect_ms=%ld session_ms=%ld total_ms=%ld "
      "heap_free=%u heap_lowest=%u heap_largest=%u rssi=%d",
      hardware_ms, bootStageMs(BootMilestone::kHardwareReady, BootMilestone::kSdReady),
      bootStageMs(BootMilestone::kSdReady, BootMilestone::kWifiConnected),
      bootStageMs(BootMilestone::kWifiConnected, BootMilestone::kTimeSynced),
      bootStageMs(BootMilestone::kTimeSynced, BootMilestone::kSocketConnected),
      bootStageMs(BootMilestone::kSocketConnected, BootMilestone::kSessionReady), total_ms,
      (unsigned)esp32va_heap::freeHeapBytes(), (unsigned)esp32va_heap::lowestFreeHeapSinceBootBytes(),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (int)WiFi.RSSI());
}

void beginTurn(uint32_t listen_number) {
  turn = {};
  turn.listen_number = listen_number;
  turn.started_ms = millis();
  turn.heard_chars = -1;
  snprintf(turn.tool_name, sizeof(turn.tool_name), "-");
  turn_active = true;
}

void markTurnEvent(TurnEvent event) {
  size_t index = static_cast<size_t>(event);
  if (!turn_active || index >= kTurnEventCount || turn.event_set[index]) return;
  turn.event_ms[index] = millis();
  turn.event_set[index] = true;
}

void setCaptureStats(uint32_t sent_bytes, uint32_t send_ms, int32_t peak_abs_sample, double rms_sample,
                     uint32_t clipped_samples, uint32_t chunk_max_ms, uint32_t chunk_avg_ms) {
  turn.sent_bytes = sent_bytes;
  turn.send_ms = send_ms;
  turn.peak_abs_sample = peak_abs_sample;
  turn.rms_sample = rms_sample;
  turn.clipped_samples = clipped_samples;
  turn.chunk_max_ms = chunk_max_ms;
  turn.chunk_avg_ms = chunk_avg_ms;
}

void addReplyAudio(uint32_t base64_chars) {
  if (turn_active) turn.reply_base64_chars += base64_chars;
}

void noteBackpressurePause() {
  if (turn_active) turn.backpressure_pauses++;
}

void setHeardLength(uint32_t characters) {
  if (turn_active) turn.heard_chars = static_cast<int32_t>(characters);
}

void setToolName(const char* tool_name) {
  if (turn_active) snprintf(turn.tool_name, sizeof(turn.tool_name), "%s", tool_name);
}

void reportTurn(bool passed) {
  if (!turn_active || turn.reported) return;
  turn.reported = true;
  long recording_ms = turn.event_set[static_cast<size_t>(TurnEvent::kRecordingDone)]
                          ? static_cast<long>(turn.event_ms[static_cast<size_t>(TurnEvent::kRecordingDone)] -
                                              turn.started_ms)
                          : -1;
  // Each 4 base64 characters carry 3 bytes; padding makes this a slight overcount at most.
  uint32_t reply_audio_bytes = turn.reply_base64_chars / 4 * 3;
  uint32_t reply_audio_ms = reply_audio_bytes * 1000 / kMuLawBytesPerSecond;
  uint32_t upload_kbps = turn.send_ms > 0 ? turn.sent_bytes * 8 / turn.send_ms : 0;  // bits per ms = kbit/s
  esp32va_status::printProtocolLine(
      "@@TIMING n=%u result=%s first_event_after_rec_ms=%ld heard_after_rec_ms=%ld first_text_after_rec_ms=%ld "
      "first_audio_after_rec_ms=%ld tool=%s tool_after_rec_ms=%ld tool_roundtrip_ms=%ld "
      "response_done_after_rec_ms=%ld playback_done_after_rec_ms=%ld rec_ms=%ld sent_bytes=%u upload_kbps=%u "
      "reply_audio_bytes=%u reply_audio_ms=%u pauses=%u heard_chars=%ld mic_peak_dbfs=%.1f mic_rms_dbfs=%.1f "
      "clipped=%u chunk_max_ms=%u chunk_avg_ms=%u heap_free=%u heap_lowest=%u heap_largest=%u rssi=%d",
      (unsigned)turn.listen_number, passed ? "pass" : "fail", msAfterRecording(TurnEvent::kFirstServerEvent),
      msAfterRecording(TurnEvent::kHeardTranscript), msAfterRecording(TurnEvent::kFirstTranscript),
      msAfterRecording(TurnEvent::kFirstAudio), turn.tool_name, msAfterRecording(TurnEvent::kToolCall),
      msBetween(TurnEvent::kToolCall, TurnEvent::kToolResponseDone), msAfterRecording(TurnEvent::kResponseDone),
      msAfterRecording(TurnEvent::kPlaybackDone), recording_ms, (unsigned)turn.sent_bytes, (unsigned)upload_kbps,
      (unsigned)reply_audio_bytes, (unsigned)reply_audio_ms, (unsigned)turn.backpressure_pauses,
      (long)turn.heard_chars, toDbfs(turn.peak_abs_sample), toDbfs(turn.rms_sample),
      (unsigned)turn.clipped_samples, (unsigned)turn.chunk_max_ms, (unsigned)turn.chunk_avg_ms,
      (unsigned)esp32va_heap::freeHeapBytes(), (unsigned)esp32va_heap::lowestFreeHeapSinceBootBytes(),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (int)WiFi.RSSI());
}

}  // namespace esp32va_measure
