#pragma once

// Every hard-coded parameter of the firmware - changing one of these needs a
// compile and an upload. Settings the user can change at run time (volume,
// LED brightness, light thresholds, hands-free, Wi-Fi network...) are set from
// the control center instead and kept in flash; see Parameters/parameters.md.
// Secrets (Wi-Fi password, API key) are in SECRETS.h, not here.
//
// Pure C++ on purpose (no Arduino headers): tools/vad_replay.cpp compiles
// vad_utils on the laptop, and anything it includes must build there too.

#include <stdint.h>

// ----- Pins -----
// The mic and the amplifier share one I2S port (full duplex): the same bit
// clock and word select lines go to both.
#define I2S_BCLK    26  // bit clock: MAX98357A BCLK + INMP441 SCK
#define I2S_LRC     25  // word select: MAX98357A LRC + INMP441 WS
#define I2S_DOUT    22  // to the MAX98357A DIN
#define I2S_MIC_SERIAL_CLOCK     I2S_BCLK
#define I2S_MIC_LEFT_RIGHT_CLOCK I2S_LRC
#define I2S_MIC_SERIAL_DATA      21  // from the INMP441 SD (its L/R pin to GND: left channel)

#define SD_CS       5
#define SD_SCLK     18
#define SD_MISO     19
#define SD_MOSI     23

// The onboard LED - the "light" the assistant switches. GPIO 2 is a strapping
// pin with no external pull-up.
#define LED_PIN          2
#define LED_ACTIVE_HIGH  1

// The onboard BOOT button, used as an optional Talk button (off until the
// BUTTON on command - see button_utils.h). Active low, with the board's own
// pull-up.
#define TALK_BUTTON_PIN  0

// The light sensor: uncomment (an ADC1 pin, 32-39) once an LDR is wired as a
// voltage divider - LDR and a 10 kOhm resistor between 3.3V and GND, the
// middle to this pin. Until then the light level is simulated from the
// control center (LIGHT <value>).
// #define LIGHT_SENSOR_PIN 34

// ----- OpenAI -----
// gpt-realtime-2.1-mini is the cheaper line ($10/1M input, $20/1M output
// audio tokens). gpt-realtime-2.1 is the bigger model.
#define OPENAI_MODEL "gpt-realtime-2.1-mini"

// ----- Clock -----
// A fixed offset from UTC for the board's local time. For automatic summer
// time instead, define a POSIX TZ string - for Israel:
// #define TIMEZONE_POSIX "IST-2IDT,M3.5.5/2,M10.5.0"
#define TIMEZONE_UTC_OFFSET_SEC (2 * 3600)  // UTC+2

namespace esp32va_params {

// ----- Serial -----
constexpr uint32_t kSerialBaudRate = 115200;  // must match control_center.html and tools/serial_logger.ps1

// ----- Listening -----
constexpr uint32_t kManualRecordDurationSec = 3;  // Talk / LISTEN / the BOOT button record this long
// Software gain on the mic before sending. x4 clipped a spoken "1, 2, 3"
// at full scale; x2 doesn't. Must match control_center.html's BOARD_MIC_GAIN.
constexpr int32_t kMicSoftwareGain = 2;
constexpr uint32_t kHandsFreeCooldownMs = 600;  // after the board speaks, the mic waits this long (its own echo)

// ----- Replies -----
// No word from the server for this long while a reply is due fails the
// LISTEN (an inactivity timeout, not a cap on the reply's length).
constexpr unsigned long kReplyTimeoutMs = 15000;

// ----- Connection -----
constexpr unsigned long kWifiBootWaitMs = 20000;  // setup() waits this long for Wi-Fi, then goes on without it
constexpr unsigned long kTimeSyncBootWaitMs = 15000;  // and this long for the clock (NTP)
// Failed OpenAI connections in a row (backing off 1, 2, 4, 8... s) before
// the board reports itself offline. It keeps retrying after that, every
// kOfflineRetryIntervalMs, for as long as it takes.
constexpr uint32_t kOpenAiAttemptsBeforeOffline = 5;
constexpr unsigned long kInitialReconnectBackoffMs = 1000;
constexpr unsigned long kOfflineRetryIntervalMs = 30000;

// ----- Defaults for the run-time settings (until changed and saved) -----
constexpr int kDefaultVolumePercent = 100;
constexpr int kDefaultLedBrightnessPercent = 100;
constexpr int kDefaultEveningLightMin = 1000;  // light level 0-6142: below this it is night
constexpr int kDefaultDayLightMin = 3000;      // and from this up, day
// The quietest volume a voice command can set: at 0 the assistant couldn't
// say that it did it. (The control center's slider still goes down to 0.)
constexpr int kMinVoiceVolumePercent = 10;
constexpr int kVoiceVolumeStepPercent = 20;  // "louder" / "quieter"

// ----- Timer -----
constexpr long kMaxTimerSeconds = 3600;

// ----- SD card -----
constexpr uint32_t kMaxKeptRecordings = 5;  // newest replies and voice commands kept, each
constexpr uint32_t kSdSpiFrequencyHz = 4000000;

// ----- Talk button -----
// Sampled every kButtonSampleMs; a press counts once kButtonDebounceSamples
// samples in a row agree (a saturating counter, not a timer that any glitch
// resets).
constexpr uint32_t kButtonSampleMs = 5;
constexpr int kButtonDebounceSamples = 6;  // 30 ms

}  // namespace esp32va_params
