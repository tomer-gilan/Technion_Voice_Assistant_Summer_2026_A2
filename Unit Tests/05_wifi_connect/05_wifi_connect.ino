// Step 5 - Wi-Fi Connectivity
// Connects to Wi-Fi using credentials from config.h, prints the assigned
// IP once connected, and reconnects automatically if the connection drops.

#include <WiFi.h>
#include "config.h"

namespace {
constexpr uint32_t kReconnectRetryDelayMs = 2000;
constexpr uint32_t kConnectionCheckIntervalMs = 5000;
uint32_t last_connection_check_ms = 0;
}  // namespace

/** Blocks until Wi-Fi connects, logging progress and the final IP to Serial. */
void connectToWifi() {
  Serial.printf("[wifi_connect][Step 5] Connecting to Wi-Fi SSID \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.printf("[wifi_connect][Step 5] Connected. IP address: %s\n", WiFi.localIP().toString().c_str());
}

/** Checks the connection periodically and reconnects if it has dropped. */
void maintainWifiConnection() {
  uint32_t now = millis();
  if (now - last_connection_check_ms < kConnectionCheckIntervalMs) {
    return;
  }
  last_connection_check_ms = now;

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("[wifi_connect][Step 5] Connection lost. Reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(kReconnectRetryDelayMs);
    Serial.println("[wifi_connect][Step 5] Still reconnecting...");
  }

  Serial.printf("[wifi_connect][Step 5] Reconnected. IP address: %s\n", WiFi.localIP().toString().c_str());
}

/** One-time setup: start Serial and connect to Wi-Fi. */
void setup() {
  Serial.begin(115200);
  Serial.println("[wifi_connect][Step 5] Boot.");

  connectToWifi();
}

/** Periodically verifies the Wi-Fi connection is still up, reconnecting if needed. */
void loop() {
  maintainWifiConnection();
}
