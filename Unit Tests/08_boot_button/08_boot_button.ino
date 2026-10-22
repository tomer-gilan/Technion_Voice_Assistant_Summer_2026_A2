// Unit test 08 - the BOOT button (GPIO 0) as a Talk button
// Reads the onboard BOOT button with the same saturating-counter debounce
// the firmware uses (button_utils.cpp), and counts both the debounced
// presses and every raw level change - the raw count shows how much noise
// the pin picks up.
//
// It also tests a suspicion: with the I2S driver's mck_io_num left at 0, the
// driver puts the I2S master clock out on GPIO 0 - which would make the
// button pin toggle by itself. Set kStartI2sWithMclkOnGpio0 to true to start
// I2S that way (as the firmware did before the fix) and watch the raw count.
//
// PASS: press BOOT 5 times - "===== BOOT BUTTON TEST PASSED =====" once 5
// debounced presses are counted. While nobody touches it, "raw changes" per
// second should stay at 0 (with kStartI2sWithMclkOnGpio0 false).

#include <driver/i2s.h>

#define TALK_BUTTON_PIN 0  // the onboard BOOT button: active low, pulled up on the board

namespace {
constexpr uint32_t kSerialBaudRate = 115200;
constexpr bool kStartI2sWithMclkOnGpio0 = false;  // true: reproduce the old I2S setup (MCLK on GPIO 0)
constexpr uint32_t kSampleMs = 5;                 // as parameters.h's kButtonSampleMs
constexpr int kDebounceSamples = 6;               // as parameters.h's kButtonDebounceSamples (30 ms)
constexpr int kPressesToPass = 5;
constexpr uint32_t kReportIntervalMs = 1000;

unsigned long last_sample_ms = 0;
unsigned long last_report_ms = 0;
int debounce_count = 0;
bool is_pressed = false;
int presses = 0;
int last_raw_level = HIGH;
uint32_t raw_changes_this_second = 0;
bool verdict_printed = false;

/** Starts I2S the way the firmware did before the MCLK fix: no mck_io_num
    in the pin config, so it is 0 - GPIO 0. */
void startI2sWithDefaultMclkPin() {
  i2s_config_t i2s_config = {};
  i2s_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  i2s_config.sample_rate = 24000;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_config.dma_buf_count = 4;
  i2s_config.dma_buf_len = 256;
  i2s_pin_config_t pin_config = {};  // mck_io_num = 0
  pin_config.bck_io_num = 26;
  pin_config.ws_io_num = 25;
  pin_config.data_out_num = 22;
  pin_config.data_in_num = 21;
  if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr) != ESP_OK ||
      i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) {
    Serial.println("I2S didn't start - the MCLK check isn't valid this run.");
    return;
  }
  Serial.println("I2S started with mck_io_num = 0 (the old setup). Watch the raw changes without touching BOOT.");
}
}  // namespace

/** Sets the pin up (and I2S, when testing the MCLK suspicion). */
void setup() {
  Serial.begin(kSerialBaudRate);
  delay(500);
  pinMode(TALK_BUTTON_PIN, INPUT_PULLUP);
  last_raw_level = digitalRead(TALK_BUTTON_PIN);
  if (kStartI2sWithMclkOnGpio0) {
    startI2sWithDefaultMclkPin();
  }
  Serial.printf("Unit test 08 - BOOT button. Press it %d times. (Don't hold it while pressing Reset - that "
                "starts the bootloader.)\n",
                kPressesToPass);
}

/** Samples the button every kSampleMs, debounces it, and reports once a second. */
void loop() {
  if (millis() - last_sample_ms >= kSampleMs) {
    last_sample_ms = millis();
    int raw_level = digitalRead(TALK_BUTTON_PIN);
    if (raw_level != last_raw_level) {
      raw_changes_this_second++;
      last_raw_level = raw_level;
    }
    debounce_count = raw_level == LOW ? min(debounce_count + 1, kDebounceSamples) : max(debounce_count - 1, 0);
    if (!is_pressed && debounce_count == kDebounceSamples) {
      is_pressed = true;
      presses++;
      Serial.printf("Press %d of %d.\n", presses, kPressesToPass);
    } else if (is_pressed && debounce_count == 0) {
      is_pressed = false;
    }
  }
  if (millis() - last_report_ms >= kReportIntervalMs) {
    last_report_ms = millis();
    if (raw_changes_this_second > 0) {
      Serial.printf("Raw level changes in the last second: %u\n", (unsigned)raw_changes_this_second);
    }
    raw_changes_this_second = 0;
  }
  if (!verdict_printed && presses >= kPressesToPass) {
    verdict_printed = true;
    Serial.println("===== BOOT BUTTON TEST PASSED =====");
  }
}
