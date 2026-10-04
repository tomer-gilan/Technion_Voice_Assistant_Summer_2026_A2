#include "wifi_utils.h"

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "log_utils.h"

void connectToWifi() {
  LOG_INFO("Connecting to Wi-Fi SSID \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");  // raw progress indicator, not a log message - always visible
  }

  Serial.println();
  LOG_INFO("Wi-Fi connected. IP address: %s\n", WiFi.localIP().toString().c_str());
}

void syncSystemTime() {
  // SNTP sync is required before the WSS handshake: TLS cert validation
  // fails without a correct system clock.
  LOG_INFO("Syncing time...\n");
  // TIMEZONE_UTC_OFFSET_SEC only affects localtime_r() elsewhere (e.g. step
  // 22's greeting) - SNTP itself, and this function's own gmtime()-based log
  // line below, are unaffected and stay in UTC.
  configTime(TIMEZONE_UTC_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {  // still near the 1970 epoch means time hasn't synced yet
    delay(300);
    Serial.print(".");  // raw progress indicator, not a log message - always visible
    now = time(nullptr);
  }

  Serial.println();
  LOG_INFO("Time synced: %s", asctime(gmtime(&now)));
}
