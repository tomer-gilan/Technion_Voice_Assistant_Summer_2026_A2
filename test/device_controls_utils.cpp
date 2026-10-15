#include "device_controls_utils.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "board_state_utils.h"
#include "i2s_audio_utils.h"
#include "log_utils.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_controls {

namespace {

constexpr int kMaxPercent = 100;
constexpr int kDefaultVolumePercent = 100;      // as loud as every step before 28
constexpr int kDefaultLedBrightnessPercent = 100;
const char kVolumeSettingKey[] = "volume";
const char kMutedSettingKey[] = "muted";
const char kLedBrightnessSettingKey[] = "led_bright";

uint8_t volume_percent = kDefaultVolumePercent;
bool is_muted = false;

/** Prints "@@VOLUME <percent> <muted|unmuted>". */
void announceVolume() {
  esp32va_status::printProtocolLine("@@VOLUME %u %s", (unsigned)volume_percent, is_muted ? "muted" : "unmuted");
}

/** If command is `word` alone or `word <argument>` (case-insensitive),
    returns the argument with leading spaces skipped ("" if there's none);
    otherwise nullptr - so "LED_BRIGHTNESS 5" is not an LED command. */
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

/** Parses a whole number 0-100 into percent_out. False if text isn't one. */
bool parsePercent(const char* text, uint8_t* percent_out) {
  char* parse_end = nullptr;
  long value = strtol(text, &parse_end, 10);
  if (parse_end == text || *parse_end != '\0' || value < 0 || value > kMaxPercent) {
    return false;
  }
  *percent_out = static_cast<uint8_t>(value);
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

}  // namespace

void setupDeviceControls() {
  int32_t saved_volume = esp32va_settings::loadSetting(kVolumeSettingKey, kDefaultVolumePercent);
  int32_t saved_brightness = esp32va_settings::loadSetting(kLedBrightnessSettingKey, kDefaultLedBrightnessPercent);
  volume_percent = static_cast<uint8_t>(constrain(saved_volume, 0, kMaxPercent));
  is_muted = esp32va_settings::loadSetting(kMutedSettingKey, 0) != 0;
  setPlaybackVolume(volume_percent, is_muted);
  esp32va_board::setLedBrightness(static_cast<uint8_t>(constrain(saved_brightness, 0, kMaxPercent)));
  LOG_INFO("Speaker volume %u%%%s, LED brightness %u%% (saved settings).\n", (unsigned)volume_percent,
           is_muted ? " (muted)" : "", (unsigned)esp32va_board::ledBrightnessPercent());
}

bool handleControlCommand(const char* command, bool idle_for_beep) {
  const char* argument = nullptr;

  if ((argument = commandArgument(command, "VOLUME")) != nullptr) {
    uint8_t requested = 0;
    if (!parsePercent(argument, &requested)) {
      Serial.printf("VOLUME ignored - expected a whole number from 0 to %d, got \"%s\".\n", kMaxPercent, argument);
    } else {
      volume_percent = requested;
      setPlaybackVolume(volume_percent, is_muted);
      esp32va_settings::saveSetting(kVolumeSettingKey, volume_percent);
      LOG_INFO("Speaker volume set to %u%%.\n", (unsigned)volume_percent);
    }
    announceVolume();
    return true;
  }

  if ((argument = commandArgument(command, "MUTE")) != nullptr) {
    bool mute = false;
    if (!parseOnOff(argument, &mute)) {
      Serial.printf("MUTE ignored - expected on or off, got \"%s\".\n", argument);
    } else {
      is_muted = mute;
      setPlaybackVolume(volume_percent, is_muted);
      esp32va_settings::saveSetting(kMutedSettingKey, is_muted ? 1 : 0);
      LOG_INFO("Speaker %s.\n", is_muted ? "muted" : "unmuted");
    }
    announceVolume();
    return true;
  }

  if ((argument = commandArgument(command, "BEEP")) != nullptr) {
    if (!idle_for_beep) {
      esp32va_status::printProtocolLine("@@BEEP busy");
      Serial.printf("BEEP ignored - something is recording or playing right now.\n");
      return true;
    }
    playTestBeep();
    esp32va_status::printProtocolLine("@@BEEP played");
    LOG_INFO("Test beep played%s.\n", is_muted ? " (muted, so silent)" : "");
    return true;
  }

  if ((argument = commandArgument(command, "LED")) != nullptr) {
    bool turn_on = false;
    if (!parseOnOff(argument, &turn_on)) {
      Serial.printf("LED ignored - expected on or off, got \"%s\".\n", argument);
    } else {
      setLed(turn_on);
      LOG_INFO("LED turned %s (laptop app / typed command).\n", turn_on ? "on" : "off");
    }
    esp32va_board::announceLedState("gui");
    return true;
  }

  if ((argument = commandArgument(command, "LED_BRIGHTNESS")) != nullptr) {
    uint8_t requested = 0;
    if (!parsePercent(argument, &requested)) {
      Serial.printf("LED_BRIGHTNESS ignored - expected a whole number from 0 to %d, got \"%s\".\n", kMaxPercent,
                    argument);
    } else {
      esp32va_board::setLedBrightness(requested);
      esp32va_settings::saveSetting(kLedBrightnessSettingKey, requested);
      LOG_INFO("LED brightness set to %u%%.\n", (unsigned)requested);
    }
    esp32va_board::announceLedState("gui");
    return true;
  }

  return false;
}

void announceControlStates() {
  announceVolume();
  esp32va_board::announceLedState("status");
}

}  // namespace esp32va_controls
