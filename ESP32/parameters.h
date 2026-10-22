#pragma once

// The firmware's hard-coded parameters - everything a user might want to
// tune; changing one of these needs a compile and an upload. Algorithm
// internals (the voice detector's thresholds, buffer sizes, tones...) stay
// next to their code; Parameters/parameters.md lists them too. Settings the
// user can change at run time (volume, LED brightness, light thresholds,
// hands-free, Wi-Fi network...) are set from the control center instead and
// kept in flash; see Parameters/parameters.md.
// Secrets (Wi-Fi password, API key) are in SECRETS.h, not here.
// This file is this project's config_parameters.h (the rubric's name for it).
// The assistant's instructions and tools are in realtime_session_config.h.
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
#define I2S_MIC_SERIAL_DATA      21  // from the INMP441 SD (read as I2S_CHANNEL_FMT_ONLY_RIGHT - step 3)

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
#define OPENAI_VOICE "alloy"  // the assistant's voice
// Writes down what the user said (the "You said" / chat bubble text) - a
// separate model from the one that answers; it doesn't change the answer.
#define OPENAI_TRANSCRIPTION_MODEL "gpt-4o-mini-transcribe"

// ----- Clock -----
// The board's local time zone, as a POSIX TZ string: Israel, with summer
// time switched automatically (IST = UTC+2, IDT = UTC+3 from the last Friday
// of March 02:00 to the last Sunday of October 02:00 - Israel's own rule,
// the Friday before the last Sunday, is the same date through 2027).
// While it is defined, TIMEZONE_UTC_OFFSET_SEC is not used.
#define TIMEZONE_POSIX "IST-2IDT,M3.5.5/2,M10.5.0"
// Without TIMEZONE_POSIX: a fixed offset from UTC, no summer time.
#define TIMEZONE_UTC_OFFSET_SEC (2 * 3600)  // UTC+2

namespace esp32va_params {

// ----- Serial -----
constexpr uint32_t kSerialBaudRate = 115200;  // must match control_center.html and tools/serial_logger.ps1
// loop() prints "@@ALIVE <uptime s>" this often, so the control center can
// tell a running board from a hung one (it waits 15 s - keep this well under).
constexpr uint32_t kAliveIntervalMs = 5000;

// ----- Listening -----
constexpr uint32_t kManualRecordDurationSec = 3;  // Talk / LISTEN / the BOOT button record this long
// Software gain on the mic before sending. x4 clipped a spoken "1, 2, 3"
// at full scale; x2 doesn't. Must match control_center.html's BOARD_MIC_GAIN.
constexpr int32_t kMicSoftwareGain = 2;
constexpr uint32_t kHandsFreeCooldownMs = 600;  // after the board speaks, the mic waits this long (its own echo)
// Hands-free: this many sounds in a row that turned out not to be speech
// pause listening for kHandsFreeDiscardPauseMs, instead of streaming more
// noise to OpenAI.
constexpr uint32_t kHandsFreeMaxDiscardsInARow = 3;
constexpr uint32_t kHandsFreeDiscardPauseMs = 5000;
// Hands-free sensitivity (VAD_SENSITIVITY, 1-5) until changed and saved:
// 1 needs the clearest, loudest speech, 5 also reacts to quieter speech
// (and to more noise).
constexpr uint8_t kDefaultVadSensitivity = 3;
// How long a silence ends a hands-free sentence (VAD_HANGOVER): the range
// the control center allows, and the default until changed and saved.
constexpr uint32_t kMinVadHangoverMs = 600;
constexpr uint32_t kMaxVadHangoverMs = 1500;
constexpr uint32_t kDefaultVadHangoverMs = 1000;
// ASK <text>: a longer typed question is cut. Bytes, not characters - UTF-8,
// so a Hebrew letter takes 2 (about 100 of them fit).
constexpr unsigned int kMaxTypedQuestionBytes = 200;

// ----- Replies -----
// No word from the server for this long while a reply is due fails the
// LISTEN (an inactivity timeout, not a cap on the reply's length).
constexpr unsigned long kReplyTimeoutMs = 15000;
// A reply recording whose audio stopped and whose end never came (e.g. the
// question timed out) is closed after this long anyway, so it doesn't hold
// off the next LISTEN.
constexpr unsigned long kStalledReplyRecordingTimeoutMs = 20000;

// ----- Connection -----
constexpr unsigned long kWifiBootWaitMs = 20000;  // setup() waits this long for Wi-Fi, then goes on without it
constexpr unsigned long kTimeSyncBootWaitMs = 15000;  // and this long for the clock (NTP)
// Wi-Fi down this long since the last attempt started: start over on the
// network (the ESP32 core retries by itself after most failures, but not a
// failed password) - a slow backstop, well apart from the core's retries.
constexpr unsigned long kWifiRestartConnectAfterMs = 60000;
// The time servers (SNTP) - the clock TLS needs, and the board's local time.
constexpr char kNtpServer1[] = "pool.ntp.org";
constexpr char kNtpServer2[] = "time.nist.gov";
// Failed attempts in a row (the boot wait, then one every
// kWifiRestartConnectAfterMs) on a Wi-Fi network saved from the control
// center before the board tries SECRETS.h's network instead - and back the
// same way. In RAM only: the saved network stays in flash for the next boot.
constexpr uint32_t kWifiAttemptsBeforeFallback = 2;
// Failed OpenAI connections in a row (backing off 1, 2, 4, 8... s) before
// the board reports itself offline. It keeps retrying after that, every
// kOfflineRetryIntervalMs, for as long as it takes.
constexpr uint32_t kOpenAiAttemptsBeforeOffline = 5;
constexpr unsigned long kInitialReconnectBackoffMs = 1000;
constexpr unsigned long kOfflineRetryIntervalMs = 30000;
// A WebSocket ping to OpenAI every kHeartbeatIntervalMs; kHeartbeatMissedPongs
// pongs in a row not back within kHeartbeatPongTimeoutMs close the connection,
// so a half-open one (Wi-Fi up, internet gone) is noticed. 0 switches it off.
constexpr uint32_t kHeartbeatIntervalMs = 15000;
constexpr uint32_t kHeartbeatPongTimeoutMs = 10000;  // generous: a pong can wait behind a reply's audio
constexpr uint8_t kHeartbeatMissedPongs = 2;

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
constexpr int kTimerAlarmBeeps = 3;          // when the timer rings: this many beeps, with the LED blinking
constexpr uint32_t kTimerAlarmGapMs = 150;   // LED off, silence, between two beeps

// ----- SD card -----
constexpr uint32_t kMaxKeptRecordings = 5;  // newest replies and voice commands kept, each
constexpr uint32_t kSdSpiFrequencyHz = 4000000;

// ----- Talk button -----
// Sampled every kButtonSampleMs; a press counts once kButtonDebounceSamples
// samples in a row agree (a saturating counter, not a timer that any glitch
// resets).
constexpr uint32_t kButtonSampleMs = 5;
constexpr int kButtonDebounceSamples = 6;  // 30 ms

// ----- Button sounds -----
// Always on (not the SOUNDS setting); silent only while muted. A short high
// tick just before a manual recording starts (BOOT, Talk, LISTEN - not
// hands-free, where speech has already begun), and a short low tone when a
// BOOT press can't start a LISTEN (offline, not ready, busy...).
constexpr uint32_t kStartToneHz = 1320;
constexpr uint32_t kStartToneMs = 60;
constexpr uint32_t kRefusedToneHz = 220;
constexpr uint32_t kRefusedToneMs = 120;
// Technician beep check: BOOT held this long (with the button switched on)
// plays one beep per startup check - Wi-Fi, OpenAI, SD card, mic, in that
// order - high = ok, low = failed, at least kTechnicianGapMs of silence between.
constexpr uint32_t kTechnicianHoldMs = 2000;
constexpr uint32_t kTechnicianOkToneHz = 880;
constexpr uint32_t kTechnicianFailToneHz = 220;
constexpr uint32_t kTechnicianToneMs = 150;
constexpr uint32_t kTechnicianGapMs = 150;

}  // namespace esp32va_params
