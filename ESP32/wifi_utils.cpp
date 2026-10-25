#include "wifi_utils.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "parameters.h"
#include "secrets_include.h"
#include "command_parse_utils.h"
#include "log_utils.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace esp32va_wifi {

namespace {
constexpr unsigned long kStillWaitingReportMs = 15000;
constexpr unsigned long kSignalReportIntervalMs = 10000;
// While Wi-Fi is down the board starts each new attempt itself (see
// retryConnectingIfDue()): this long after the last one started, once it
// failed - or once it has gone on this long with no outcome at all.
constexpr unsigned long kRetryIntervalMs = 5000;
constexpr unsigned long kAttemptTimeoutMs = 15000;
// Down this long on the active network: one failed attempt toward the
// fallback to the other network (noteFailedAttempt()).
constexpr unsigned long kFailedAttemptAfterMs = esp32va_params::kWifiFailedAttemptAfterMs;
constexpr size_t kStageDetailBufferSize = 64;
constexpr time_t kPlausibleEpochSeconds = 8 * 3600 * 2;  // still near 1970 means not synced yet
const char kSsidSettingKey[] = "wifi_ssid";
const char kPasswordSettingKey[] = "wifi_pass";

char active_ssid[kMaxSsidLength + 1] = "";
char active_password[kMaxPasswordLength + 1] = "";
bool using_saved_network = false;
char pending_ssid[kMaxSsidLength + 1] = "";  // from WIFI_SSID, until WIFI_PASSWORD saves it
bool pending_ssid_set = false;

unsigned long last_signal_report_ms = 0;
unsigned long last_connect_start_ms = 0;
unsigned long down_on_network_since_ms = 0;  // toward the next noteFailedAttempt()
// From onWifiEvent(), on the core's event task. WiFi.status() alone isn't
// trusted: core 2.0.17 leaves it at WL_CONNECTED after a disconnect for
// reason AUTH_EXPIRE - on the board, Wi-Fi looked connected for 2 minutes
// after the hotspot was gone, until the core's IP-lost timer changed it.
volatile bool link_up = false;                // got an IP, and no disconnect since
volatile bool attempt_failed = false;         // the attempt under way ended without connecting
volatile uint8_t last_disconnect_reason = 0;  // the ESP-IDF reason code, for the log
bool wifi_was_connected = false;
bool time_sync_started = false;
bool time_synced = false;
uint32_t failed_attempts_in_a_row = 0;  // on the active network, since it last connected or was chosen

/** Makes SECRETS.h's network the active one. */
void useDefaultNetwork() {
  using_saved_network = false;
  snprintf(active_ssid, sizeof(active_ssid), "%s", WIFI_SSID);
  snprintf(active_password, sizeof(active_password), "%s", WIFI_PASSWORD);
}

/** Makes the network saved in flash the active one. Returns false, changing
    nothing, if no network was saved. */
bool useSavedNetwork() {
  char saved_ssid[kMaxSsidLength + 1];
  if (!esp32va_settings::loadTextSetting(kSsidSettingKey, saved_ssid, sizeof(saved_ssid))) {
    return false;
  }
  using_saved_network = true;
  snprintf(active_ssid, sizeof(active_ssid), "%s", saved_ssid);
  esp32va_settings::loadTextSetting(kPasswordSettingKey, active_password, sizeof(active_password));
  return true;
}

/** Loads the network to use: the one saved in flash, or SECRETS.h's. */
void loadActiveNetwork() {
  if (!useSavedNetwork()) {
    useDefaultNetwork();
  }
}

/** Keeps link_up and attempt_failed from the core's Wi-Fi events - on its
    event task, so flag writes only. ASSOC_LEAVE is the board's own doing
    (startConnecting() drops the attempt under way): it doesn't end the new
    attempt. */
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      link_up = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      link_up = false;
      last_disconnect_reason = info.wifi_sta_disconnected.reason;
      if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE) {
        attempt_failed = true;
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_STOP:
      link_up = false;
      break;
    default:
      break;
  }
}

/** Sets the active network up for the next attempt: begin() without its
    own connect, which does nothing while WiFi.status() still says
    WL_CONNECTED (see link_up). */
void configureActiveNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(active_ssid, active_password[0] != '\0' ? active_password : nullptr, 0, nullptr, false);
}

/** Starts an attempt on the configured network, dropping the one under way
    (reconnect() always connects, whatever WiFi.status() says). */
void startConnecting() {
  link_up = false;
  attempt_failed = false;
  if (!WiFi.reconnect()) {
    LOG_ERROR("Couldn't start connecting to Wi-Fi \"%s\" - trying again shortly.\n", active_ssid);
    attempt_failed = true;
  }
  last_connect_start_ms = millis();
}

/** While Wi-Fi is down: starts the next attempt once the last one failed
    and kRetryIntervalMs has passed since it started, or once it has gone on
    for kAttemptTimeoutMs with no outcome. The board does this itself, with
    the core's auto-reconnect off: on the board, the core's retries stopped
    (or never started) while the hotspot was off, and nothing reconnected
    until the next restart up to a minute later. */
void retryConnectingIfDue() {
  unsigned long attempt_ms = millis() - last_connect_start_ms;
  if (attempt_ms < kRetryIntervalMs || (!attempt_failed && attempt_ms < kAttemptTimeoutMs)) {
    return;
  }
  LOG_DEBUG("Wi-Fi still down (last disconnect reason %u) - trying \"%s\" again.\n",
            (unsigned)last_disconnect_reason, active_ssid);
  startConnecting();
}

/** Reports "connecting to <ssid>" as the Wi-Fi stage. */
void reportConnecting() {
  char stage_detail[kStageDetailBufferSize];
  snprintf(stage_detail, sizeof(stage_detail), "connecting to %s", active_ssid);
  esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kActive, stage_detail);
}

/** Why Wi-Fi isn't connected, in a few words. */
const char* notConnectedReason(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:
      return "network not found - check the name, and that it's 2.4 GHz";
    case WL_CONNECT_FAILED:
      return "connection failed - check the password";
    case WL_DISCONNECTED:
      return "not connected yet - check the password and signal";
    default:
      return "still trying";
  }
}

/** Reports the connection as up: the stage, the IP and the signal. */
void reportConnected() {
  wifi_was_connected = true;
  failed_attempts_in_a_row = 0;
  esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kOk,
                              WiFi.localIP().toString().c_str());
  reportWifiSignal();
  last_signal_report_ms = millis();
}

/** Starts SNTP once - the clock then keeps itself in sync. */
void startTimeSync() {
  if (time_sync_started) {
    return;
  }
  time_sync_started = true;
  esp32va_status::reportStage(esp32va_status::Stage::kTimeSync, esp32va_status::StageState::kActive, nullptr);
  // The time zone only affects localtime_r() (the greeting, "what time is
  // it?") - SNTP itself always syncs to UTC. TIMEZONE_POSIX (parameters.h)
  // wins when defined: configTzTime() sets TZ and calls tzset(), so summer
  // time follows; otherwise the fixed TIMEZONE_UTC_OFFSET_SEC.
#ifdef TIMEZONE_POSIX
  configTzTime(TIMEZONE_POSIX, esp32va_params::kNtpServer1, esp32va_params::kNtpServer2);
#else
  configTime(TIMEZONE_UTC_OFFSET_SEC, 0, esp32va_params::kNtpServer1, esp32va_params::kNtpServer2);
#endif
}

/** Checks whether the clock has been set; reports it the first time. */
bool checkTimeSynced() {
  if (time_synced) {
    return true;
  }
  time_t now = time(nullptr);
  if (now < kPlausibleEpochSeconds) {
    return false;
  }
  time_synced = true;
  Serial.printf("Time synced: %s", asctime(gmtime(&now)));
  esp32va_status::reportStage(esp32va_status::Stage::kTimeSync, esp32va_status::StageState::kOk, nullptr);
  return true;
}

using esp32va_cmd::commandArgument;  // command_parse_utils.h

/** WIFI_PASSWORD's password as typed: everything after the word and exactly
    one space on the untrimmed line, so spaces at either end of the password
    are kept. "" if there is none. */
const char* rawPasswordArgument(const char* raw_line) {
  const char kWord[] = "WIFI_PASSWORD";
  const size_t word_length = sizeof(kWord) - 1;
  while (*raw_line == ' ' || *raw_line == '\t') {
    raw_line++;
  }
  if (strncasecmp(raw_line, kWord, word_length) != 0 || raw_line[word_length] != ' ') {
    return "";
  }
  return raw_line + word_length + 1;
}

/** Switches to the active network now, telling the user and the app. */
void switchNetwork() {
  announceWifiNetwork();
  Serial.printf("Connecting to Wi-Fi \"%s\"...\n", active_ssid);
  wifi_was_connected = false;
  failed_attempts_in_a_row = 0;
  down_on_network_since_ms = millis();
  reportConnecting();
  configureActiveNetwork();
  startConnecting();
}

/** Counts a connection attempt on the active network that ran out of time.
    After kWifiAttemptsBeforeFallback of them in a row, if a network was
    saved from the control center, switches to the other network: from the
    saved one (a mistyped password, a hotspot that's gone) to SECRETS.h's,
    and back the same way. In RAM only - the saved network stays in flash
    for the next boot. Returns true if it switched (and started connecting). */
bool noteFailedAttempt() {
  failed_attempts_in_a_row++;
  if (failed_attempts_in_a_row < esp32va_params::kWifiAttemptsBeforeFallback) {
    return false;
  }
  if (using_saved_network) {
    LOG_ERROR("Saved Wi-Fi \"%s\" failed %u times in a row - trying the default network instead (the saved one is "
              "kept).\n",
              active_ssid, (unsigned)failed_attempts_in_a_row);
    useDefaultNetwork();
  } else if (useSavedNetwork()) {
    LOG_ERROR("Default Wi-Fi failed %u times in a row - trying the saved network \"%s\" again.\n",
              (unsigned)failed_attempts_in_a_row, active_ssid);
  } else {
    return false;  // only the default network exists - keep trying it
  }
  switchNetwork();
  return true;
}

}  // namespace

bool connectToWifi(unsigned long wait_ms) {
  // Progress is printed directly (not via LOG_INFO) so it stays visible at
  // LOG_LEVEL_ERROR - otherwise a Wi-Fi that never connects looks like an
  // endless line of dots with no explanation.
  WiFi.onEvent(onWifiEvent);
  WiFi.setAutoReconnect(false);  // the board retries by itself - see retryConnectingIfDue()
  loadActiveNetwork();
  announceWifiNetwork();
  Serial.printf("Connecting to Wi-Fi \"%s\"...\n", active_ssid);
  reportConnecting();
  down_on_network_since_ms = millis();
  configureActiveNetwork();
  startConnecting();

  unsigned long started_ms = millis();
  unsigned long last_report_ms = started_ms;
  while (!isWifiConnected()) {
    if (millis() - started_ms >= wait_ms) {
      Serial.println();
      wl_status_t status = WiFi.status();
      LOG_ERROR("Wi-Fi \"%s\" not connected after %lu s (status %d: %s).\n", active_ssid, wait_ms / 1000,
                (int)status, notConnectedReason(status));
      Serial.printf("Going on without Wi-Fi - still trying in the background. Set another network from the "
                    "control center (Wi-Fi card) if this one is wrong.\n");
      char stage_detail[kStageDetailBufferSize];
      snprintf(stage_detail, sizeof(stage_detail), "%s - still trying", notConnectedReason(status));
      esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kFail, stage_detail);
      down_on_network_since_ms = millis();  // the boot wait counts as the first failed attempt
      noteFailedAttempt();
      return false;
    }
    delay(300);
    Serial.print(".");  // raw progress indicator, not a log message - always visible
    retryConnectingIfDue();
    if (millis() - last_report_ms >= kStillWaitingReportMs) {
      last_report_ms = millis();
      wl_status_t status = WiFi.status();
      Serial.println();
      LOG_ERROR("Wi-Fi \"%s\" not connected after %lu s (status %d: %s). Still retrying...\n", active_ssid,
                (millis() - started_ms) / 1000, (int)status, notConnectedReason(status));
    }
  }

  Serial.println();
  Serial.printf("Wi-Fi connected. IP address: %s\n", WiFi.localIP().toString().c_str());
  reportConnected();
  return true;
}

bool syncSystemTime(unsigned long wait_ms) {
  // SNTP sync is required before the WSS handshake: TLS cert validation
  // fails without a correct system clock.
  Serial.printf("Syncing time...\n");
  startTimeSync();
  unsigned long started_ms = millis();
  while (!checkTimeSynced()) {
    if (millis() - started_ms >= wait_ms) {
      Serial.println();
      LOG_ERROR("Time not synced after %lu s - the network may block NTP (UDP port 123). Still trying.\n",
                wait_ms / 1000);
      esp32va_status::reportStage(esp32va_status::Stage::kTimeSync, esp32va_status::StageState::kActive,
                                  "no answer from the time servers - still trying");
      return false;
    }
    delay(300);
    Serial.print(".");  // raw progress indicator, not a log message - always visible
  }
  return true;
}

bool isWifiConnected() {
  return link_up && WiFi.status() == WL_CONNECTED;
}

bool isTimeSynced() {
  return checkTimeSynced();
}

void reportWifiSignal() {
  if (!isWifiConnected()) {
    return;
  }
  esp32va_status::printProtocolLine("@@WIFI %d", (int)WiFi.RSSI());
}

void announceWifiNetwork() {
  esp32va_status::printProtocolLine("@@WIFI_NET %s %s", using_saved_network ? "saved" : "default", active_ssid);
}

bool handleWifiCommand(const char* command, const char* raw_line) {
  const char* argument = nullptr;
  if ((argument = commandArgument(command, "WIFI_SSID")) != nullptr) {
    if (*argument == '\0' || strlen(argument) > kMaxSsidLength) {
      Serial.printf("WIFI_SSID ignored - expected a network name of 1 to %u characters.\n",
                    (unsigned)kMaxSsidLength);
      return true;
    }
    snprintf(pending_ssid, sizeof(pending_ssid), "%s", argument);
    pending_ssid_set = true;
    Serial.printf("Wi-Fi network name set to \"%s\" - now send WIFI_PASSWORD <password> (or WIFI_PASSWORD alone "
                  "for an open network) to save it and connect.\n",
                  pending_ssid);
    return true;
  }
  if ((argument = commandArgument(command, "WIFI_PASSWORD")) != nullptr) {
    if (!pending_ssid_set) {
      Serial.printf("WIFI_PASSWORD ignored - send WIFI_SSID <name> first.\n");
      return true;
    }
    const char* raw_password = rawPasswordArgument(raw_line);  // a password may start or end with a space
    if (*argument != '\0' && *raw_password != '\0') {
      argument = raw_password;
    }
    size_t password_length = strlen(argument);
    if (password_length > kMaxPasswordLength || (password_length > 0 && password_length < kMinPasswordLength)) {
      Serial.printf("WIFI_PASSWORD ignored - a WPA2 password is %u to %u characters (or none for an open "
                    "network).\n",
                    (unsigned)kMinPasswordLength, (unsigned)kMaxPasswordLength);
      return true;
    }
    snprintf(active_ssid, sizeof(active_ssid), "%s", pending_ssid);
    snprintf(active_password, sizeof(active_password), "%s", argument);
    pending_ssid_set = false;
    esp32va_settings::saveTextSetting(kSsidSettingKey, active_ssid);
    esp32va_settings::saveTextSetting(kPasswordSettingKey, active_password);
    using_saved_network = true;
    LOG_INFO("Saved Wi-Fi network \"%s\" (password: %u characters).\n", active_ssid, (unsigned)password_length);
    switchNetwork();
    return true;
  }
  if ((argument = commandArgument(command, "WIFI_FORGET")) != nullptr) {
    esp32va_settings::removeSetting(kSsidSettingKey);
    esp32va_settings::removeSetting(kPasswordSettingKey);
    pending_ssid_set = false;
    loadActiveNetwork();
    LOG_INFO("Forgot the saved Wi-Fi network - back to the default one.\n");
    switchNetwork();
    return true;
  }
  if ((argument = commandArgument(command, "WIFI")) != nullptr) {
    announceWifiNetwork();
    return true;
  }
  return false;
}

void monitorWifiConnection() {
  bool connected = isWifiConnected();
  if (connected != wifi_was_connected) {
    if (connected) {
      LOG_INFO("Wi-Fi is back. IP address: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("Wi-Fi connected to \"%s\".\n", active_ssid);
      reportConnected();
    } else {
      wifi_was_connected = false;
      down_on_network_since_ms = millis();
      LOG_ERROR("Wi-Fi connection lost (reason %u) - reconnecting.\n", (unsigned)last_disconnect_reason);
      esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kFail,
                                  "connection lost - reconnecting");
    }
  }
  if (!connected && millis() - down_on_network_since_ms >= kFailedAttemptAfterMs) {
    down_on_network_since_ms = millis();
    if (!noteFailedAttempt()) {
      LOG_INFO("Wi-Fi still down after %lu s on \"%s\" - still trying.\n", kFailedAttemptAfterMs / 1000,
               active_ssid);
      esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kFail,
                                  notConnectedReason(WiFi.status()));
    }
  }
  if (!connected) {
    retryConnectingIfDue();
  }
  if (connected && !time_synced) {
    startTimeSync();
    checkTimeSynced();
  }
  if (connected && millis() - last_signal_report_ms >= kSignalReportIntervalMs) {
    last_signal_report_ms = millis();
    reportWifiSignal();
  }
}

}  // namespace esp32va_wifi
