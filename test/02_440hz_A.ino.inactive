// Test: plays a 1-second 440Hz (A4) sine tone through the MAX98357A I2S DAC.
// Sanity check for the speaker/DAC wiring before any mic or network code.

#include <math.h>
#include <driver/i2s.h>
#include "config.h"

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kSampleRateHz = 44100;
constexpr float kToneFrequencyHz = 440.0f;
constexpr uint32_t kToneDurationMs = 1000;
constexpr int16_t kAmplitude = 8000;  // headroom below int16 max to avoid clipping
constexpr uint32_t kChunkSamples = 256;
}  // namespace

/** Configures and starts I2S0 as a stereo master output wired to the MAX98357A. */
void setupI2sOutput() {
  i2s_config_t i2s_config = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = kSampleRateHz,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(kI2sPort, &i2s_config, 0, nullptr);
  i2s_set_pin(kI2sPort, &pin_config);
}

/** Generates and streams one second of a pure sine tone at kToneFrequencyHz. */
void playToneOneSecond() {
  const uint32_t total_samples = kSampleRateHz * kToneDurationMs / 1000;
  int16_t buffer[kChunkSamples * 2];  // interleaved L/R, both channels identical

  uint32_t samples_written = 0;
  while (samples_written < total_samples) {
    uint32_t samples_this_chunk = min(kChunkSamples, total_samples - samples_written);

    for (uint32_t i = 0; i < samples_this_chunk; i++) {
      uint32_t sample_index = samples_written + i;
      float phase = 2.0f * PI * kToneFrequencyHz * sample_index / kSampleRateHz;
      int16_t sample = static_cast<int16_t>(kAmplitude * sinf(phase));
      buffer[2 * i] = sample;      // left
      buffer[2 * i + 1] = sample;  // right (duplicated for full-volume mono)
    }

    size_t bytes_written = 0;
    i2s_write(kI2sPort, buffer, samples_this_chunk * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    samples_written += samples_this_chunk;
  }
}

/** One-shot bring-up: configure I2S, play the test tone, then tear down. */
void setup() {
  Serial.begin(115200);
  Serial.println("[440hz_A] Boot: configuring I2S output for the MAX98357A DAC.");

  setupI2sOutput();
  Serial.println("[440hz_A] I2S ready. Playing 440Hz tone for 1 second.");

  playToneOneSecond();

  Serial.println("[440hz_A] Tone finished. Tearing down I2S driver.");
  i2s_driver_uninstall(kI2sPort);
  Serial.println("[440hz_A] Done. Nothing more to do (loop is idle).");
}

/** Idle — this is a one-shot test, all work happens in setup(). */
void loop() {
}
