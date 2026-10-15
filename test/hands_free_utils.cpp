#include "hands_free_utils.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "log_utils.h"
#include "mic_capture_utils.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_hands_free {

namespace {

using esp32va_mic_capture::CaptureMode;

constexpr uint32_t kLevelReportIntervalMs = 100;
const char kEnabledSettingKey[] = "vad_on";
const char kSensitivitySettingKey[] = "vad_sens";
const char kHangoverSettingKey[] = "vad_hang";

bool hands_free_on = false;
uint8_t sensitivity = esp32va_vad::kDefaultSensitivity;
uint32_t hangover_ms = esp32va_vad::kDefaultHangoverMs;
unsigned long last_busy_ms = 0;  // the last time the board couldn't listen (or holdOff())
uint32_t discards_in_a_row = 0;  // utterances in a row that weren't speech (discarded, or no words heard)
bool is_paused = false;          // pausing after kMaxDiscardsInARow of them
unsigned long paused_since_ms = 0;
unsigned long last_level_report_ms = 0;

/** If command is `word` alone or `word <argument>` (case-insensitive),
    returns the argument with leading spaces skipped ("" if there's none);
    otherwise nullptr - so "VAD_HANGOVER 900" is not a VAD command. */
const char* commandArgument(const char* command, const char* word) {
  size_t word_length = strlen(word);
  if (strncasecmp(command, word, word_length) != 0) {
    return nullptr;
  }
  const char* rest = command + word_length;
  if (*rest != ' ' && *rest != '\0') {
    return nullptr;
  }
  while (*rest == ' ') {
    rest++;
  }
  return rest;
}

/** Parses a whole number from low to high into value_out. False if text isn't one. */
bool parseWholeNumber(const char* text, long low, long high, long* value_out) {
  char* parse_end = nullptr;
  long value = strtol(text, &parse_end, 10);
  if (parse_end == text || *parse_end != '\0' || value < low || value > high) {
    return false;
  }
  *value_out = value;
  return true;
}

/** Parses "on"/"off" (case-insensitive) into on_out. False if it's neither. */
bool parseOnOff(const char* text, bool* on_out) {
  if (strcasecmp(text, "on") == 0) {
    *on_out = true;
    return true;
  }
  if (strcasecmp(text, "off") == 0) {
    *on_out = false;
    return true;
  }
  return false;
}

/** Stops the mic if it's listening for speech - never a capture under way. */
void stopListening() {
  if (esp32va_mic_capture::captureMode() == CaptureMode::kArmed) {
    esp32va_mic_capture::stopCapture();
  }
}

/** Prints @@VAD_LEVEL, at most every kLevelReportIntervalMs. */
void reportLevel(unsigned long now_ms) {
  if (now_ms - last_level_report_ms < kLevelReportIntervalMs) {
    return;
  }
  esp32va_mic_capture::LevelReading reading;
  if (!esp32va_mic_capture::readLevel(&reading)) {
    return;  // no frame analyzed since listening started
  }
  last_level_report_ms = now_ms;
  esp32va_status::printProtocolLine("@@VAD_LEVEL %ld %ld %ld", lroundf(reading.level_dbfs),
                                    lroundf(reading.floor_dbfs), lroundf(reading.threshold_dbfs));
}

/** Counts one more utterance in a row that wasn't speech, and pauses
    listening (@@VAD_EVENT paused) once there have been kMaxDiscardsInARow. */
void countNonSpeechUtterance() {
  discards_in_a_row++;
  if (discards_in_a_row < kMaxDiscardsInARow) {
    return;
  }
  is_paused = true;
  paused_since_ms = millis();
  stopListening();
  esp32va_status::printProtocolLine("@@VAD_EVENT paused %u", (unsigned)kDiscardPauseMs);
  LOG_INFO("Hands-free: %u noises in a row - pausing for %u s.\n", (unsigned)discards_in_a_row,
           (unsigned)(kDiscardPauseMs / 1000));
}

/** True if text has at least one letter or digit (any non-ASCII byte counts,
    so Hebrew or accented words do too). */
bool hasWords(const char* text) {
  if (text == nullptr) {
    return false;
  }
  for (const unsigned char* character = reinterpret_cast<const unsigned char*>(text); *character != '\0';
       character++) {
    if (isalnum(*character) || *character >= 0x80) {
      return true;
    }
  }
  return false;
}

/** The protocol word for why an utterance was discarded. */
const char* discardReasonWord(esp32va_vad::DiscardReason reason) {
  switch (reason) {
    case esp32va_vad::DiscardReason::kTooShort:
      return "short";
    case esp32va_vad::DiscardReason::kTooUnvoiced:
      return "unvoiced";
    case esp32va_vad::DiscardReason::kTooSteady:
      return "steady";
    case esp32va_vad::DiscardReason::kNone:
      LOG_ERROR("An utterance was discarded without a reason.\n");
      return "unknown";
    default:
      LOG_ERROR("Unexpected discard reason %d.\n", (int)reason);
      return "unknown";
  }
}

}  // namespace

void setupHandsFree() {
  hands_free_on = esp32va_settings::loadSetting(kEnabledSettingKey, 0) != 0;
  int32_t saved_sensitivity =
      esp32va_settings::loadSetting(kSensitivitySettingKey, esp32va_vad::kDefaultSensitivity);
  int32_t saved_hangover = esp32va_settings::loadSetting(kHangoverSettingKey, esp32va_vad::kDefaultHangoverMs);
  sensitivity = static_cast<uint8_t>(
      constrain(saved_sensitivity, (int32_t)esp32va_vad::kMinSensitivity, (int32_t)esp32va_vad::kMaxSensitivity));
  hangover_ms = static_cast<uint32_t>(
      constrain(saved_hangover, (int32_t)esp32va_vad::kMinHangoverMs, (int32_t)esp32va_vad::kMaxHangoverMs));
  esp32va_mic_capture::setDetectorSettings(sensitivity, hangover_ms);
  last_busy_ms = millis();
  LOG_INFO("Hands-free listening %s - sensitivity %u, ends after %u ms of silence (saved settings).\n",
           hands_free_on ? "on" : "off", (unsigned)sensitivity, (unsigned)hangover_ms);
}

bool handleHandsFreeCommand(const char* command) {
  const char* argument = nullptr;

  if ((argument = commandArgument(command, "VAD")) != nullptr) {
    bool turn_on = false;
    if (!parseOnOff(argument, &turn_on)) {
      Serial.printf("VAD ignored - expected on or off, got \"%s\".\n", argument);
    } else {
      hands_free_on = turn_on;
      esp32va_settings::saveSetting(kEnabledSettingKey, turn_on ? 1 : 0);
      discards_in_a_row = 0;
      is_paused = false;
      if (!turn_on) {
        stopListening();
      }
      LOG_INFO("Hands-free listening turned %s.\n", turn_on ? "on" : "off");
    }
    announceHandsFreeState();
    return true;
  }

  if ((argument = commandArgument(command, "VAD_SENSITIVITY")) != nullptr) {
    long requested = 0;
    if (!parseWholeNumber(argument, esp32va_vad::kMinSensitivity, esp32va_vad::kMaxSensitivity, &requested)) {
      Serial.printf("VAD_SENSITIVITY ignored - expected a whole number from %u to %u, got \"%s\".\n",
                    (unsigned)esp32va_vad::kMinSensitivity, (unsigned)esp32va_vad::kMaxSensitivity, argument);
    } else {
      sensitivity = static_cast<uint8_t>(requested);
      esp32va_settings::saveSetting(kSensitivitySettingKey, sensitivity);
      esp32va_mic_capture::setDetectorSettings(sensitivity, hangover_ms);
      LOG_INFO("Hands-free sensitivity set to %u.\n", (unsigned)sensitivity);
    }
    announceHandsFreeState();
    return true;
  }

  if ((argument = commandArgument(command, "VAD_HANGOVER")) != nullptr) {
    long requested = 0;
    if (!parseWholeNumber(argument, esp32va_vad::kMinHangoverMs, esp32va_vad::kMaxHangoverMs, &requested)) {
      Serial.printf("VAD_HANGOVER ignored - expected milliseconds from %u to %u, got \"%s\".\n",
                    (unsigned)esp32va_vad::kMinHangoverMs, (unsigned)esp32va_vad::kMaxHangoverMs, argument);
    } else {
      hangover_ms = static_cast<uint32_t>(requested);
      esp32va_settings::saveSetting(kHangoverSettingKey, static_cast<int32_t>(hangover_ms));
      esp32va_mic_capture::setDetectorSettings(sensitivity, hangover_ms);
      LOG_INFO("Hands-free: an utterance now ends after %u ms of silence.\n", (unsigned)hangover_ms);
    }
    announceHandsFreeState();
    return true;
  }

  return false;
}

void announceHandsFreeState() {
  esp32va_status::printProtocolLine("@@VAD %s %u %u", hands_free_on ? "on" : "off", (unsigned)sensitivity,
                                    (unsigned)hangover_ms);
}

bool isHandsFreeOn() {
  return hands_free_on;
}

bool isWaitingForSpeech() {
  return esp32va_mic_capture::captureMode() == CaptureMode::kArmed;
}

void serviceHandsFree(bool board_can_listen) {
  unsigned long now_ms = millis();
  if (!board_can_listen) {
    last_busy_ms = now_ms;
  }
  CaptureMode mode = esp32va_mic_capture::captureMode();
  if (mode == CaptureMode::kArmed) {
    if (!hands_free_on || !board_can_listen) {
      esp32va_mic_capture::stopCapture();
      return;
    }
    reportLevel(now_ms);
    return;
  }
  if (mode != CaptureMode::kOff || !hands_free_on || !board_can_listen) {
    return;  // a capture is under way (the sketch's business), or nothing to listen for
  }
  if (is_paused) {
    if (now_ms - paused_since_ms < kDiscardPauseMs) {
      return;
    }
    is_paused = false;
    discards_in_a_row = 0;
    LOG_INFO("Hands-free: listening for speech again.\n");
  }
  if (now_ms - last_busy_ms < kCooldownMs) {
    return;
  }
  if (esp32va_mic_capture::armVoiceTrigger(sensitivity, hangover_ms)) {
    last_level_report_ms = 0;
    LOG_DEBUG("Hands-free: waiting for speech.\n");
  } else {
    // Shouldn't happen while board_can_listen holds - try again after another cooldown.
    LOG_DEBUG("Hands-free: the mic couldn't start listening yet (playback buffer busy).\n");
    last_busy_ms = now_ms;
  }
}

void holdOff() {
  last_busy_ms = millis();
  stopListening();
}

void noteUtteranceStarted(uint32_t pre_roll_ms) {
  esp32va_status::printProtocolLine("@@VAD_EVENT started %u", (unsigned)pre_roll_ms);
}

void noteUtteranceSent(uint32_t speech_ms, bool ended_at_length_cap) {
  esp32va_status::printProtocolLine("@@VAD_EVENT sent %u %s", (unsigned)speech_ms,
                                    ended_at_length_cap ? "length" : "silence");
}

void noteUtteranceDiscarded(esp32va_vad::DiscardReason reason, uint32_t length_ms) {
  const char* reason_word = discardReasonWord(reason);
  esp32va_status::printProtocolLine("@@VAD_EVENT discarded %s %u", reason_word, (unsigned)length_ms);
  LOG_INFO("Hands-free: ignored %.1f s of sound that didn't sound like speech (%s).\n", length_ms / 1000.0f,
           reason_word);
  countNonSpeechUtterance();
}

void noteUtteranceTranscript(const char* transcript) {
  if (hasWords(transcript)) {
    discards_in_a_row = 0;
    return;
  }
  LOG_INFO("Hands-free: OpenAI heard no words in that utterance - counted as noise.\n");
  countNonSpeechUtterance();
}

}  // namespace esp32va_hands_free
