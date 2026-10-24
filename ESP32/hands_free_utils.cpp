#include "hands_free_utils.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "command_parse_utils.h"
#include "log_utils.h"
#include "mic_capture_utils.h"
#include "openai_realtime_utils.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_hands_free {

namespace {

using esp32va_mic_capture::CaptureMode;

constexpr uint32_t kLevelReportIntervalMs = 100;
constexpr long kLongestLaptopHoldAcceptedMs = 24L * 3600 * 1000;  // a LAPTOP_PLAYBACK hold past this is a typo, not a recording
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
bool laptop_playing = false;     // LAPTOP_PLAYBACK on, until off or laptop_hold_ms ran out
unsigned long laptop_playing_since_ms = 0;
uint32_t laptop_hold_ms = 0;

using esp32va_cmd::commandArgument;  // command_parse_utils.h
using esp32va_cmd::parseOnOff;

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

/** LAPTOP_PLAYBACK on [<ms>] | off (see the header): holds listening off
    while the laptop plays a recording out loud, or lets it listen again. */
void handleLaptopPlaybackCommand(const char* argument) {
  bool turn_on = false;
  const char* hold_text = "";
  if (strncasecmp(argument, "on ", 3) == 0) {
    turn_on = true;
    hold_text = argument + 3;
    while (*hold_text == ' ') {
      hold_text++;
    }
  } else if (!parseOnOff(argument, &turn_on)) {
    Serial.printf("LAPTOP_PLAYBACK ignored - expected on [<ms>] or off, got \"%s\".\n", argument);
    return;
  }
  long hold_ms = kMaxLaptopPlaybackHoldMs;
  if (*hold_text != '\0' && !parseWholeNumber(hold_text, 1, kLongestLaptopHoldAcceptedMs, &hold_ms)) {
    Serial.printf("LAPTOP_PLAYBACK ignored - expected milliseconds after on, got \"%s\".\n", hold_text);
    return;
  }
  if (!turn_on) {
    if (laptop_playing) {
      laptop_playing = false;
      LOG_INFO("The laptop's recording is paused or over - hands-free may listen again.\n");
    }
    return;  // the cooldown already counts from the last pass that held listening off
  }
  laptop_hold_ms = static_cast<uint32_t>(min(hold_ms, static_cast<long>(kMaxLaptopPlaybackHoldMs)));
  laptop_playing_since_ms = millis();
  if (!laptop_playing) {
    LOG_INFO("The laptop is playing a recording - hands-free doesn't listen until it's over (%lu s at most).\n",
             (unsigned long)(laptop_hold_ms / 1000));
  }
  laptop_playing = true;
  holdOff();  // a mic waiting for speech stops right away, not on the next serviceHandsFree()
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
      announceHandsFreeState();
    } else {
      setHandsFree(turn_on, false);
    }
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

  if ((argument = commandArgument(command, "LAPTOP_PLAYBACK")) != nullptr) {
    handleLaptopPlaybackCommand(argument);
    return true;
  }

  return false;
}

void setHandsFree(bool turn_on, bool by_voice) {
  hands_free_on = turn_on;
  esp32va_settings::saveSetting(kEnabledSettingKey, turn_on ? 1 : 0);
  discards_in_a_row = 0;
  is_paused = false;
  if (!turn_on) {
    stopListening();
  }
  LOG_INFO("Hands-free listening turned %s%s.\n", turn_on ? "on" : "off", by_voice ? " (voice command)" : "");
  announceHandsFreeState();
}

bool handleSetHandsFreeCall(JsonDocument& doc, WebSocketsClient& webSocket, bool user_request_in_progress) {
  const char* call_id = doc["call_id"] | (const char*)nullptr;
  const char* arguments_json = doc["arguments"] | "{}";
  if (call_id == nullptr) {
    LOG_ERROR("Function call missing call_id.\n");
    return false;
  }
  if (!user_request_in_progress) {
    // Still answer the call, like the other tools - otherwise the model
    // waits on it and the user hears nothing.
    LOG_ERROR("set_hands_free called while no question was being answered - hands-free not changed.\n");
    esp32va_openai::sendFunctionCallOutput(webSocket, call_id,
                                           "Error: no request from the user to act on - hands-free was not changed.");
    return true;
  }
  JsonDocument args_doc;
  DeserializationError err = deserializeJson(args_doc, arguments_json);
  JsonVariant on_value = args_doc["on"];
  if (err || !on_value.is<bool>()) {
    LOG_ERROR("set_hands_free called without a valid on: %s\n", arguments_json);
    esp32va_openai::sendFunctionCallOutput(webSocket, call_id,
                                           "Error: on must be true or false - hands-free was not changed.");
    return true;
  }
  bool turn_on = on_value.as<bool>();
  const char* output = nullptr;
  if (turn_on == hands_free_on) {
    LOG_INFO("Hands-free is already %s (voice command) - nothing changed.\n", turn_on ? "on" : "off");
    output = turn_on ? "Hands-free was already on - the user can just start talking, no button needed."
                     : "Hands-free was already off - the user presses Talk or the button to ask.";
  } else {
    setHandsFree(turn_on, true);
    output = turn_on ? "Hands-free is now on - from now on the user can just start talking, no button needed."
                     : "Hands-free is now off - from now on the user presses Talk or the button to ask.";
  }
  esp32va_openai::sendFunctionCallOutput(webSocket, call_id, output);
  LOG_DEBUG("Sent function_call_output.\n");
  return true;
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

bool isLaptopPlaying() {
  if (laptop_playing && millis() - laptop_playing_since_ms >= laptop_hold_ms) {
    laptop_playing = false;  // its "off" never came (the page closed mid-playback?)
    LOG_INFO("The laptop's recording should be over by now - hands-free may listen again.\n");
  }
  return laptop_playing;
}

void serviceHandsFree(bool board_can_listen) {
  if (isLaptopPlaying()) {
    board_can_listen = false;  // the mic would hear the recording - and the cooldown counts from its end
  }
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
