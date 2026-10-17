#include "wifi_utils.h"

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "log_utils.h"
#include "status_report_utils.h"

namespace {
constexpr unsigned long kStillWaitingReportMs = 15000;
constexpr unsigned long kSignalReportIntervalMs = 10000;
constexpr size_t kStageDetailBufferSize = 64;

unsigned long last_signal_report_ms = 0;
bool wifi_was_connected = false;
}  // namespace

void connectToWifi() {
  // Connection progress is printed directly (not via LOG_INFO) so it stays
  // visible at LOG_LEVEL_ERROR - otherwise a Wi-Fi that never connects looks
  // like an endless line of dots with no explanation.
  Serial.printf("Connecting to Wi-Fi \"%s\"...\n", WIFI_SSID);
  char stage_detail[kStageDetailBufferSize];
  snprintf(stage_detail, sizeof(stage_detail), "connecting to %s", WIFI_SSID);
  esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kActive, stage_detail);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long started_ms = millis();
  unsigned long last_report_ms = started_ms;
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");  // raw progress indicator, not a log message - always visible
    if (millis() - last_report_ms >= kStillWaitingReportMs) {
      last_report_ms = millis();
      wl_status_t status = WiFi.status();
      const char *reason = status == WL_NO_SSID_AVAIL   ? "network not found - check the name, and that it's 2.4 GHz"
                           : status == WL_CONNECT_FAILED ? "connection failed - check the password"
                           : status == WL_DISCONNECTED   ? "not connected yet - check the password and signal"
                                                         : "still trying";
      Serial.println();
      LOG_ERROR("Wi-Fi \"%s\" not connected after %lu s (status %d: %s). Still retrying...\n", WIFI_SSID,
                (millis() - started_ms) / 1000, (int)status, reason);
      snprintf(stage_detail, sizeof(stage_detail), "%s - still retrying", reason);
      esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kActive, stage_detail);
    }
  }

  Serial.println();
  Serial.printf("Wi-Fi connected. IP address: %s\n", WiFi.localIP().toString().c_str());
  esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kOk,
                              WiFi.localIP().toString().c_str());
  wifi_was_connected = true;
  esp32va_wifi::reportWifiSignal();
  last_signal_report_ms = millis();
}

void syncSystemTime() {
  // SNTP sync is required before the WSS handshake: TLS cert validation
  // fails without a correct system clock.
  Serial.printf("Syncing time...\n");
  esp32va_status::reportStage(esp32va_status::Stage::kTimeSync, esp32va_status::StageState::kActive, nullptr);
  // TIMEZONE_UTC_OFFSET_SEC only affects localtime_r() elsewhere (e.g. step
  // 22's greeting) - SNTP itself, and this function's own gmtime()-based log
  // line below, are unaffected and stay in UTC.
  // Optional: a POSIX TZ string in config.h (e.g. #define TIMEZONE_POSIX
  // "IST-2IDT,M3.5.5/2,M10.5.0" for Israel) also follows summer time, which
  // matters now that "What time is it?" reads this clock. Without it, the
  // fixed offset is used as before.
#ifdef TIMEZONE_POSIX
  configTzTime(TIMEZONE_POSIX, "pool.ntp.org", "time.nist.gov");
#else
  configTime(TIMEZONE_UTC_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
#endif

  unsigned long started_ms = millis();
  unsigned long last_report_ms = started_ms;
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {  // still near the 1970 epoch means time hasn't synced yet
    delay(300);
    Serial.print(".");  // raw progress indicator, not a log message - always visible
    now = time(nullptr);
    if (millis() - last_report_ms >= kStillWaitingReportMs) {
      last_report_ms = millis();
      Serial.println();
      LOG_ERROR("Time not synced after %lu s - the network may block NTP (UDP port 123). Still retrying...\n",
                (millis() - started_ms) / 1000);
      esp32va_status::reportStage(esp32va_status::Stage::kTimeSync, esp32va_status::StageState::kActive,
                                  "no answer from the time servers - still retrying");
    }
  }

  Serial.println();
  Serial.printf("Time synced: %s", asctime(gmtime(&now)));
  esp32va_status::reportStage(esp32va_status::Stage::kTimeSync, esp32va_status::StageState::kOk, nullptr);
}

namespace esp32va_wifi {

void reportWifiSignal() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  esp32va_status::printProtocolLine("@@WIFI %d", (int)WiFi.RSSI());
}

void monitorWifiConnection() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifi_was_connected) {
    wifi_was_connected = connected;
    if (connected) {
      LOG_INFO("Wi-Fi is back. IP address: %s\n", WiFi.localIP().toString().c_str());
      esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kOk,
                                  WiFi.localIP().toString().c_str());
      reportWifiSignal();
    } else {
      LOG_ERROR("Wi-Fi connection lost - reconnecting.\n");
      esp32va_status::reportStage(esp32va_status::Stage::kWifi, esp32va_status::StageState::kFail,
                                  "connection lost - reconnecting");
    }
  }
  if (connected && millis() - last_signal_report_ms >= kSignalReportIntervalMs) {
    last_signal_report_ms = millis();
    reportWifiSignal();
  }
}

}  // namespace esp32va_wifi
