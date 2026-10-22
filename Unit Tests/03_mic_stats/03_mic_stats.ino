// Step 3 - Mic Input Capture
// Reads raw I2S audio from the INMP441 and prints periodic min/max/RMS
// stats to Serial, so mic wiring can be confirmed before anything
// downstream (loopback, streaming) is built.
//
// PASS: within kVerdictDeadlineMs of boot, talk or clap near the mic -
// "===== MIC TEST PASSED =====" once a window's RMS is clearly above the
// quietest window seen. If that never happens: "===== MIC TEST FAILED: ... ====="
// (all zeros usually means the wrong channel or a loose data wire). The verdict
// prints once; the stats keep running after it.

#include <stdint.h>
#include <math.h>
#include <driver/i2s.h>
// Pins - the same as ESP32/parameters.h (each unit test stands alone, so it
// carries its own copy).
#define I2S_MIC_SERIAL_CLOCK     26  // INMP441 SCK
#define I2S_MIC_LEFT_RIGHT_CLOCK 25  // INMP441 WS
#define I2S_MIC_SERIAL_DATA      21  // INMP441 SD

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_1;  // kept separate from the speaker's I2S_NUM_0 (see step 4)
constexpr uint32_t kSampleRateHz = 16000;   // matches the Gemini Live API input rate
constexpr uint32_t kSamplesPerReport = kSampleRateHz / 2;  // ~0.5s reporting window
constexpr uint32_t kReadChunkSamples = 256;
constexpr uint32_t kVerdictDeadlineMs = 15000;  // time to talk before a FAILED verdict
constexpr double kMinRmsRiseFactor = 2.0;       // speech must be this many times the quietest window
constexpr double kMinRmsRiseAbsolute = 20.0;    // and at least this much above it (raw 16-bit units)

double quietest_rms = 1e9;
bool saw_nonzero_sample = false;
bool verdict_printed = false;
}  // namespace

/** Configures and starts I2S1 as a mono master input wired to the INMP441. */
void setupI2sInput() {
  i2s_config_t i2s_config = {
    .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = kSampleRateHz,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 sends 24-bit data in a 32-bit frame
    // Confirmed by testing: this board's INMP441 L/R pin is strapped to
    // VDD (right channel) - ONLY_LEFT reads flat zero.
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    // No master clock pin: left unset it defaults to 0, and the driver then
    // puts MCLK out on GPIO0 (the BOOT button). Nothing here uses MCLK.
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA
  };

  i2s_driver_install(kI2sPort, &i2s_config, 0, nullptr);
  i2s_set_pin(kI2sPort, &pin_config);
}

/** Converts a raw 32-bit I2S word from the INMP441 into a 16-bit PCM sample. */
int16_t convertRawSampleTo16Bit(int32_t raw_sample) {
  return static_cast<int16_t>(raw_sample >> 16);
}

/** Prints the PASSED/FAILED verdict once, as soon as it is known. */
void reportVerdictIfKnown(double latest_rms) {
  if (verdict_printed) {
    return;
  }
  if (saw_nonzero_sample && latest_rms >= quietest_rms * kMinRmsRiseFactor &&
      latest_rms - quietest_rms >= kMinRmsRiseAbsolute) {
    verdict_printed = true;
    Serial.println("===== MIC TEST PASSED =====");
  } else if (millis() >= kVerdictDeadlineMs) {
    verdict_printed = true;
    if (!saw_nonzero_sample) {
      Serial.println("===== MIC TEST FAILED: the mic only returned zeros (check wiring and the L/R channel) =====");
    } else {
      Serial.println("===== MIC TEST FAILED: the level never rose when talking (too quiet, or you didn't make a sound) =====");
    }
  }
}

/** Reads one reporting window of mic audio and prints min/max/RMS to Serial. */
void captureAndReportWindow() {
  int32_t raw_buffer[kReadChunkSamples];
  int16_t min_sample = INT16_MAX;
  int16_t max_sample = INT16_MIN;
  double sum_of_squares = 0.0;
  uint32_t samples_collected = 0;

  while (samples_collected < kSamplesPerReport) {
    size_t bytes_read = 0;
    i2s_read(kI2sPort, raw_buffer, sizeof(raw_buffer), &bytes_read, portMAX_DELAY);
    uint32_t samples_read = bytes_read / sizeof(int32_t);

    for (uint32_t i = 0; i < samples_read; i++) {
      int16_t sample = convertRawSampleTo16Bit(raw_buffer[i]);
      min_sample = min(min_sample, sample);
      max_sample = max(max_sample, sample);
      sum_of_squares += static_cast<double>(sample) * sample;
    }

    samples_collected += samples_read;
  }

  double rms = sqrt(sum_of_squares / samples_collected);
  if (min_sample != 0 || max_sample != 0) {
    saw_nonzero_sample = true;
  }
  quietest_rms = min(quietest_rms, rms);
  Serial.printf("[mic_stats][Step 3] window=%u samples  min=%d  max=%d  rms=%.1f\n",
                samples_collected, min_sample, max_sample, rms);
  reportVerdictIfKnown(rms);
}

/** One-time setup: start Serial and the mic's I2S input. */
void setup() {
  Serial.begin(115200);
  Serial.println("[mic_stats][Step 3] Boot: configuring I2S input for the INMP441 mic.");

  setupI2sInput();

  Serial.println("[mic_stats][Step 3] I2S ready. Reporting mic level stats every ~0.5s.");
  Serial.println("[mic_stats][Step 3] Stay quiet for a second, then talk or clap near the mic (15 s to do it).");
}

/** Continuously captures a reporting window and logs its stats, forever. */
void loop() {
  captureAndReportWindow();
}
