#include "i2s_audio_utils.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>
#include "config.h"
#include "log_utils.h"

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kSampleRateHz = 24000;
constexpr uint32_t kI2sDmaBufLen = 300;
constexpr uint32_t kI2sDmaBufCount = 12;  // 12*300 = 3600 samples of ring capacity

// Holds raw mu-law bytes (NOT decoded PCM) - see header comment. At
// 8kHz/1 byte-per-sample, 32KB is ~4.1s of audio. Halved from 64KB in step
// 24: with the SD card mounted, 64KB left only ~9KB free once the OpenAI
// session was up, and the first reply/LISTEN ran the heap dry. A 48KB
// midpoint was tried too, but 32KB was kept to leave RAM for future
// features (see PROJECT_OVERVIEW.md step 24). Backpressure, not buffer
// size, is what paces long replies - a smaller ring only shrinks the
// cushion against network stalls.
constexpr uint32_t kRingBufferBytes = 32 * 1024;
// Pause reading more from the WebSocket once free space drops below 30%,
// resume once it's back above 60% (hysteresis). 30% of 32KB is ~1.2s of
// headroom when pausing kicks in - still more than what can already be in
// flight (one ~0.4s delta being decoded plus a TCP receive window's worth).
constexpr uint32_t kRingPauseThresholdBytes = (kRingBufferBytes * 3) / 10;
constexpr uint32_t kRingResumeThresholdBytes = (kRingBufferBytes * 6) / 10;

// Per-pass base64 decode size, aligned to base64's 4-character grouping -
// caps how much of one delta gets decoded at a time, regardless of how large
// the delta itself is.
constexpr uint32_t kDecodeInputChars = 1024;
constexpr uint32_t kDecodeOutputBufBytes = 800;  // >= 1024/4*3 = 768 decoded mu-law bytes
// mu-law is 8kHz; the shared full-duplex I2S peripheral runs at kSampleRateHz
// (24kHz) - repeat each decoded sample this many times.
constexpr int kOutputUpsampleFactor = 3;
// Drained from the ring to the speaker per call, in raw mu-law bytes - 320
// bytes = 40ms of source audio (960 samples once upsampled), a deliberately
// small chunk so i2s_write()'s blocking wait for DMA space stays short.
constexpr uint32_t kTxDrainMuLawBytes = 320;

uint8_t* ring_buffer = nullptr;
uint32_t ring_write_index = 0;
uint32_t ring_read_index = 0;
uint32_t ring_used_bytes = 0;

bool recording_tap_active = false;
uint32_t recording_tap_read_index = 0;
uint32_t recording_tap_pending_bytes = 0;

uint8_t decode_scratch[kDecodeOutputBufBytes];
uint8_t tx_mulaw_bytes[kTxDrainMuLawBytes];
int32_t tx_i2s_words[kTxDrainMuLawBytes * kOutputUpsampleFactor];

/** ITU-T G.711 mu-law sample -> linear PCM16. The classic public-domain
    bit-manipulation decode (not a lookup table). */
int16_t ulawToLinearPcm16(uint8_t ulaw_sample) {
  constexpr int kBias = 0x84;
  uint8_t inverted = ~ulaw_sample;
  int magnitude = ((inverted & 0x0F) << 3) + kBias;
  magnitude <<= (inverted & 0x70) >> 4;
  return static_cast<int16_t>((inverted & 0x80) ? (kBias - magnitude) : (magnitude - kBias));
}
}  // namespace

void setupI2sFullDuplex() {
  i2s_config_t i2s_config = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = kSampleRateHz,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 sends 24-bit data in a 32-bit frame; TX bus matches width
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,  // confirmed in step 3 for this board's mic
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = kI2sDmaBufCount,
    .dma_buf_len = kI2sDmaBufLen,
    .use_apll = false,
    .tx_desc_auto_clear = true,  // auto-fills silence between writes, avoiding TX buffer underrun noise
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,             // shared clock line, also wired to the mic
    .ws_io_num = I2S_LRC,               // shared clock line, also wired to the mic
    .data_out_num = I2S_DOUT,           // to MAX98357A DIN
    .data_in_num = I2S_MIC_SERIAL_DATA  // from INMP441 SD
  };

  esp_err_t install_result = i2s_driver_install(kI2sPort, &i2s_config, 0, nullptr);
  if (install_result != ESP_OK) {
    // Continuing to i2s_set_pin() after a failed install previously caused a
    // Guru Meditation crash-reboot loop for this exact driver (old step 8b).
    LOG_ERROR("i2s_driver_install failed (code %d) - halting.\n", (int)install_result);
    while (true) delay(1000);
  }
  i2s_set_pin(kI2sPort, &pin_config);
}

uint32_t readMicChunkGained(int32_t* raw_scratch, int16_t* pcm_out, uint32_t max_samples, int32_t gain,
                             int16_t* level_min, int16_t* level_max, double* level_sum_of_squares) {
  size_t bytes_read = 0;
  i2s_read(kI2sPort, raw_scratch, max_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
  uint32_t samples_read = bytes_read / sizeof(int32_t);

  for (uint32_t i = 0; i < samples_read; i++) {
    int16_t sample16 = static_cast<int16_t>(raw_scratch[i] >> 16);
    int32_t scaled = static_cast<int32_t>(sample16) * gain;
    if (scaled > INT16_MAX) scaled = INT16_MAX;
    if (scaled < INT16_MIN) scaled = INT16_MIN;
    pcm_out[i] = static_cast<int16_t>(scaled);
    *level_min = min(*level_min, pcm_out[i]);
    *level_max = max(*level_max, pcm_out[i]);
    *level_sum_of_squares += static_cast<double>(pcm_out[i]) * pcm_out[i];
  }
  return samples_read;
}

void initPlaybackRingBuffer() {
  ring_buffer = static_cast<uint8_t*>(malloc(kRingBufferBytes));
  if (ring_buffer == nullptr) {
    LOG_ERROR("Failed to allocate %u-byte ring buffer - halting.\n", (unsigned)kRingBufferBytes);
    while (true) delay(1000);
  }
}

void resetRingBuffer() {
  ring_write_index = 0;
  ring_read_index = 0;
  ring_used_bytes = 0;
  ringStopRecordingTap();
}

uint32_t ringUsedBytes() {
  return ring_used_bytes;
}

uint32_t ringFreeBytes() {
  uint32_t held_bytes = ring_used_bytes;
  if (recording_tap_active && recording_tap_pending_bytes > held_bytes) {
    held_bytes = recording_tap_pending_bytes;
  }
  return kRingBufferBytes - held_bytes;
}

void ringStartRecordingTap() {
  recording_tap_active = true;
  recording_tap_read_index = ring_write_index;
  recording_tap_pending_bytes = 0;
}

void ringStopRecordingTap() {
  recording_tap_active = false;
  recording_tap_pending_bytes = 0;
}

uint32_t ringRecordingTapPendingBytes() {
  return recording_tap_pending_bytes;
}

uint32_t ringReadRecordingTap(uint8_t* out, uint32_t max_len) {
  uint32_t n = min(max_len, recording_tap_pending_bytes);
  for (uint32_t i = 0; i < n; i++) {
    out[i] = ring_buffer[recording_tap_read_index];
    recording_tap_read_index = (recording_tap_read_index + 1) % kRingBufferBytes;
  }
  recording_tap_pending_bytes -= n;
  return n;
}

namespace {
/** Copies len bytes into the ring buffer, wrapping as needed. Drops (and
    logs) the whole segment rather than partially writing or blocking if it
    doesn't fit - a full ring means playback can't keep up with the network,
    which is worth knowing about, not silently smoothing over. */
void ringWrite(const uint8_t* data, uint32_t len) {
  if (len > ringFreeBytes()) {
    LOG_ERROR("Ring buffer overflow - dropping %u bytes of decoded audio.\n", (unsigned)len);
    return;
  }
  for (uint32_t i = 0; i < len; i++) {
    ring_buffer[ring_write_index] = data[i];
    ring_write_index = (ring_write_index + 1) % kRingBufferBytes;
  }
  ring_used_bytes += len;
  if (recording_tap_active) {
    recording_tap_pending_bytes += len;
  }
}

/** Copies up to max_len bytes out of the ring buffer into out, wrapping as
    needed. Returns how many bytes were actually available and copied. */
uint32_t ringRead(uint8_t* out, uint32_t max_len) {
  uint32_t n = min(max_len, ring_used_bytes);
  for (uint32_t i = 0; i < n; i++) {
    out[i] = ring_buffer[ring_read_index];
    ring_read_index = (ring_read_index + 1) % kRingBufferBytes;
  }
  ring_used_bytes -= n;
  return n;
}
}  // namespace

void decodeAndRingAudioDelta(const uint8_t* b64_data, size_t b64_len) {
  size_t offset = 0;
  while (offset < b64_len) {
    size_t take = min((size_t)kDecodeInputChars, b64_len - offset);
    take -= take % 4;  // stay aligned to base64's 4-character groups
    if (take == 0) break;

    size_t decoded_len = 0;
    int result = mbedtls_base64_decode(decode_scratch, sizeof(decode_scratch), &decoded_len,
                                        b64_data + offset, take);
    if (result != 0) {
      LOG_ERROR("base64 decode failed (code %d) - dropping remainder of this delta.\n", result);
      return;
    }
    ringWrite(decode_scratch, decoded_len);
    offset += take;
  }
}

void drainRingBufferToSpeaker() {
  uint32_t n = ringRead(tx_mulaw_bytes, kTxDrainMuLawBytes);
  if (n == 0) return;

  uint32_t word_index = 0;
  for (uint32_t i = 0; i < n; i++) {
    int32_t widened = static_cast<int32_t>(ulawToLinearPcm16(tx_mulaw_bytes[i])) << 16;  // re-align for the 32-bit-wide TX bus
    for (int rep = 0; rep < kOutputUpsampleFactor; rep++) {
      tx_i2s_words[word_index++] = widened;
    }
  }
  size_t bytes_written = 0;
  i2s_write(kI2sPort, tx_i2s_words, word_index * sizeof(int32_t), &bytes_written, portMAX_DELAY);
}

bool ringShouldPauseReads(bool currently_paused) {
  if (currently_paused) {
    return ringFreeBytes() <= kRingResumeThresholdBytes;
  }
  return ringFreeBytes() < kRingPauseThresholdBytes;
}
