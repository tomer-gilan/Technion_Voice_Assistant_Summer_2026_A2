// Step 4 - Mic to Speaker Loopback
// Cycles forever: records a fixed-duration clip from the INMP441 into RAM
// (onboard LED on), then plays it back through the MAX98357A (LED off).
// Confirms both peripherals work together over the single, physically
// shared I2S clock bus (BCLK/LRC are wired to both modules - see
// PROJECT_OVERVIEW.md "Pins & Connections"), using one I2S port rather
// than two independent masters, which would otherwise fight over the same
// clock pins.
//
// Note: buffering a whole clip in RAM is fine for this bring-up test (no
// Wi-Fi/TLS loaded yet, so there's plenty of free heap), but the real
// Gemini pipeline streams audio in small live chunks instead - see the
// "Memory" gotcha in PROJECT_OVERVIEW.md.

#include <stdint.h>
#include <driver/i2s.h>
#include "config.h"

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kSampleRateHz = 16000;
constexpr uint32_t kRecordDurationSec = 2;
constexpr uint32_t kRecordSamples = kSampleRateHz * kRecordDurationSec;
constexpr uint32_t kChunkSamples = 256;
constexpr int32_t kSoftwareGain = 4;  // mic levels are otherwise quiet - see step 3 findings

int16_t audio_buffer[kRecordSamples];  // ~2s of mono 16-bit audio, static to avoid stack overflow
}  // namespace

/** Configures I2S0 as a full-duplex master: RX from the INMP441, TX to the MAX98357A. */
void setupI2sFullDuplex() {
  i2s_config_t i2s_config = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = kSampleRateHz,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    // Confirmed in step 3: this board's INMP441 needs RIGHT, not LEFT.
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = kChunkSamples,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,             // shared clock line, also wired to the mic
    .ws_io_num = I2S_LRC,               // shared clock line, also wired to the mic
    .data_out_num = I2S_DOUT,           // to MAX98357A DIN
    .data_in_num = I2S_MIC_SERIAL_DATA  // from INMP441 SD
  };

  i2s_driver_install(kI2sPort, &i2s_config, 0, nullptr);
  i2s_set_pin(kI2sPort, &pin_config);
}

/** Drives the onboard LED, respecting this board's active-high/low polarity. */
void setLed(bool on) {
  digitalWrite(LED_PIN, (on == LED_ACTIVE_HIGH) ? HIGH : LOW);
}

/** Converts one raw 32-bit mic word into a gain-adjusted, clamped 16-bit sample. */
int16_t processMicSample(int32_t raw_sample) {
  int16_t sample16 = static_cast<int16_t>(raw_sample >> 16);
  int32_t scaled = static_cast<int32_t>(sample16) * kSoftwareGain;
  if (scaled > INT16_MAX) scaled = INT16_MAX;
  if (scaled < INT16_MIN) scaled = INT16_MIN;
  return static_cast<int16_t>(scaled);
}

/** Records kRecordDurationSec of mic audio into audio_buffer, LED on throughout. */
void recordClip() {
  setLed(true);
  Serial.printf("[mic_loopback][Step 4] Recording %us clip...\n", kRecordDurationSec);

  int32_t raw_chunk[kChunkSamples];
  uint32_t samples_recorded = 0;

  while (samples_recorded < kRecordSamples) {
    uint32_t samples_wanted = min(kChunkSamples, kRecordSamples - samples_recorded);
    size_t bytes_read = 0;
    i2s_read(kI2sPort, raw_chunk, samples_wanted * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    uint32_t samples_read = bytes_read / sizeof(int32_t);

    for (uint32_t i = 0; i < samples_read; i++) {
      audio_buffer[samples_recorded + i] = processMicSample(raw_chunk[i]);
    }
    samples_recorded += samples_read;
  }

  Serial.println("[mic_loopback][Step 4] Recording done.");
}

/** Plays audio_buffer back through the speaker, LED off throughout. */
void playbackClip() {
  setLed(false);
  Serial.println("[mic_loopback][Step 4] Playing back...");

  int32_t tx_chunk[kChunkSamples];
  uint32_t samples_played = 0;

  while (samples_played < kRecordSamples) {
    uint32_t samples_this_chunk = min(kChunkSamples, kRecordSamples - samples_played);
    for (uint32_t i = 0; i < samples_this_chunk; i++) {
      tx_chunk[i] = static_cast<int32_t>(audio_buffer[samples_played + i]) << 16;  // re-align for the 32-bit-wide TX bus
    }
    size_t bytes_written = 0;
    i2s_write(kI2sPort, tx_chunk, samples_this_chunk * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    samples_played += samples_this_chunk;
  }

  Serial.println("[mic_loopback][Step 4] Playback done.");
}

/** One-time setup: start Serial, the onboard LED, and the I2S bus. */
void setup() {
  Serial.begin(115200);
  Serial.println("[mic_loopback][Step 4] Boot: configuring I2S and onboard LED.");

  pinMode(LED_PIN, OUTPUT);
  setLed(false);
  setupI2sFullDuplex();

  Serial.printf("[mic_loopback][Step 4] Ready. Each cycle: %us recording (LED on), then playback (LED off).\n",
                kRecordDurationSec);
}

/** Repeats record-then-playback cycles forever. */
void loop() {
  recordClip();
  playbackClip();
}
