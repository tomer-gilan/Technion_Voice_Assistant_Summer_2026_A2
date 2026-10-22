#include "device_controls_utils.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "board_state_utils.h"
#include "i2s_audio_utils.h"
#include "log_utils.h"
#include "parameters.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_controls {

namespace {

constexpr int kMaxPercent = 100;
constexpr int kDefaultVolumePercent = esp32va_params::kDefaultVolumePercent;
constexpr int kDefaultLedBrightnessPercent = esp32va_params::kDefaultLedBrightnessPercent;
const char kVolumeSettingKey[] = "volume";
const char kMutedSettingKey[] = "muted";
const char kLedBrightnessSettingKey[] = "led_bright";
const char kSoundsSettingKey[] = "sounds_on";

// Status sounds: two short tones each (a single one for a failed question).
constexpr uint32_t kStatusToneMs = 120;
constexpr uint32_t kStatusToneGapMs = 60;
constexpr uint32_t kLowToneHz = 440;
constexpr uint32_t kHighToneHz = 660;
constexpr uint32_t kFailedToneHz = 330;
constexpr uint32_t kFailedToneMs = 250;

constexpr int kVoiceVolumeStepPercent = esp32va_params::kVoiceVolumeStepPercent;  // "louder" / "quieter" by voice
constexpr size_t kToolResponseBufferSize = 256;  // function_call_output messages are tiny - no audio involved
char tool_response_message[kToolResponseBufferSize];

uint8_t volume_percent = kDefaultVolumePercent;
bool is_muted = false;
bool status_sounds_on = false;
StatusSound pending_sound = StatusSound::kNone;

/** Prints "@@SOUNDS <on|off>". */
void announceSounds() {
  esp32va_status::printProtocolLine("@@SOUNDS %s", status_sounds_on ? "on" : "off");
}

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
  status_sounds_on = esp32va_settings::loadSetting(kSoundsSettingKey, 0) != 0;
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

  if ((argument = commandArgument(command, "SOUNDS")) != nullptr) {
    bool turn_on = false;
    if (!parseOnOff(argument, &turn_on)) {
      Serial.printf("SOUNDS ignored - expected on or off, got \"%s\".\n", argument);
    } else {
      status_sounds_on = turn_on;
      if (!turn_on) {
        pending_sound = StatusSound::kNone;
      }
      esp32va_settings::saveSetting(kSoundsSettingKey, turn_on ? 1 : 0);
      LOG_INFO("Status sounds turned %s.\n", turn_on ? "on" : "off");
    }
    announceSounds();
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

void setAndSaveLedBrightness(uint8_t percent) {
  esp32va_board::setLedBrightness(percent);
  esp32va_settings::saveSetting(kLedBrightnessSettingKey, esp32va_board::ledBrightnessPercent());
}

bool handleSetVolumeCall(JsonDocument& doc, WebSocketsClient& webSocket) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  const char* arguments_json = doc["arguments"] | "{}";
  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }

  JsonDocument args_doc;
  DeserializationError err = deserializeJson(args_doc, arguments_json);
  bool understood = true;
  int requested = volume_percent;
  JsonVariant percent = args_doc["percent"];
  const char* step = args_doc["step"] | "";
  if (err) {
    LOG_ERROR("set_volume args parse failed: %s\n", err.c_str());
    LOG_DEBUG("Failed arguments string: %s\n", arguments_json);
    understood = false;
  } else if (percent.is<int>() || percent.is<float>()) {  // 30, or 30.0 if the model sends it that way
    requested = static_cast<int>(lroundf(constrain(percent.as<float>(), 0.0f, static_cast<float>(kMaxPercent))));
  } else if (strcasecmp(step, "up") == 0) {
    requested = volume_percent + kVoiceVolumeStepPercent;
  } else if (strcasecmp(step, "down") == 0) {
    requested = volume_percent - kVoiceVolumeStepPercent;
  } else {
    LOG_ERROR("set_volume called with neither percent nor step up/down: %s\n", arguments_json);
    understood = false;
  }
  if (!understood) {
    // Still answer the call, so the model says something instead of going quiet.
    snprintf(tool_response_message, kToolResponseBufferSize,
             R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"Error: give percent (0-100) or step up or down"}})JSON",
             call_id);
    webSocket.sendTXT(tool_response_message);
    return true;
  }
  // Not below kMinVoiceVolumePercent by voice: at 0 the assistant couldn't
  // even say that it did it.
  volume_percent = static_cast<uint8_t>(constrain(requested, esp32va_params::kMinVoiceVolumePercent, kMaxPercent));
  is_muted = false;
  setPlaybackVolume(volume_percent, is_muted);
  esp32va_settings::saveSetting(kVolumeSettingKey, volume_percent);
  esp32va_settings::saveSetting(kMutedSettingKey, 0);
  LOG_INFO("Speaker volume set to %u%% (voice command).\n", (unsigned)volume_percent);
  announceVolume();

  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"Volume set to %u percent"}})JSON",
           call_id, (unsigned)volume_percent);
  webSocket.sendTXT(tool_response_message);
  LOG_DEBUG("Sent function_call_output.\n");
  return true;
}

void announceControlStates() {
  announceVolume();
  esp32va_board::announceLedState("status");
  announceSounds();
}

void requestStatusSound(StatusSound sound) {
  if (status_sounds_on) {
    pending_sound = sound;
  }
}

bool hasPendingStatusSound() {
  return pending_sound != StatusSound::kNone;
}

void playPendingStatusSound() {
  StatusSound sound = pending_sound;
  pending_sound = StatusSound::kNone;
  switch (sound) {
    case StatusSound::kNone:
      return;
    case StatusSound::kOffline:
      playTone(kHighToneHz, kStatusToneMs);
      delay(kStatusToneGapMs);
      playTone(kLowToneHz, kStatusToneMs);
      break;
    case StatusSound::kOnline:
      playTone(kLowToneHz, kStatusToneMs);
      delay(kStatusToneGapMs);
      playTone(kHighToneHz, kStatusToneMs);
      break;
    case StatusSound::kFailed:
      playTone(kFailedToneHz, kFailedToneMs);
      break;
    default:
      LOG_ERROR("Unexpected status sound %d - not played.\n", (int)sound);
      return;
  }
  LOG_DEBUG("Status sound %d played%s.\n", (int)sound, is_muted ? " (muted, so silent)" : "");
}

}  // namespace esp32va_controls
