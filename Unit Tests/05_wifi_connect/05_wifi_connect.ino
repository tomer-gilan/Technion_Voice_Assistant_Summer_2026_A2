// Step 5 - Wi-Fi Connectivity
// Connects to Wi-Fi using the credentials below, prints the assigned
// IP once connected, and reconnects automatically if the connection drops.
//
// PASS: "===== WIFI TEST PASSED =====" once connected and an IP is assigned.
// FAILED: "===== WIFI TEST FAILED: ... =====" if there is no connection within
// kConnectTimeoutMs (wrong name or password, or no 2.4 GHz network in range).
// After a PASS, switch the hotspot off and on: it logs the drop and reconnects.

#include <WiFi.h>
// Your network - fill in before uploading (2.4 GHz). Not committed with real
// values; the firmware itself takes them from ESP32/SECRETS.h.
#define WIFI_SSID     "your-network-name"
#define WIFI_PASSWORD "your-network-password"

namespace {
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kReconnectRetryDelayMs = 2000;
constexpr uint32_t kConnectionCheckIntervalMs = 5000;
uint32_t last_connection_check_ms = 0;
bool connect_failed = false;
}  // namespace

/** Waits up to kConnectTimeoutMs for Wi-Fi to connect, logging progress, the
    final IP and the PASSED/FAILED verdict to Serial. */
void connectToWifi() {
  Serial.printf("[wifi_connect][Step 5] Connecting to Wi-Fi SSID \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started_ms = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started_ms < kConnectTimeoutMs) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    connect_failed = true;
    Serial.println("===== WIFI TEST FAILED: no connection within 20 s (check the SSID and password at the top of the file, and that it is a 2.4 GHz network) =====");
    return;
  }
  Serial.printf("[wifi_connect][Step 5] Connected. IP address: %s\n", WiFi.localIP().toString().c_str());
  Serial.println("===== WIFI TEST PASSED =====");
}

/** Checks the connection periodically and reconnects if it has dropped.
    Does nothing after a failed first connect (the verdict already says why). */
void maintainWifiConnection() {
  if (connect_failed) {
    return;
  }
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
