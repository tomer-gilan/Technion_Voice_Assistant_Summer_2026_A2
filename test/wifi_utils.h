#pragma once

// Wi-Fi connection helpers shared by every networked test sketch. Both
// functions read WIFI_SSID / WIFI_PASSWORD straight from config.h, so no
// parameters are needed - just #include this and call them from setup().
// Both also report their progress as @@STAGE lines (status_report_utils.h).

/** Blocks until Wi-Fi connects, logging progress and the assigned IP. */
void connectToWifi();

/** Blocks until SNTP has set a plausible system time, required for TLS
    certificate validation on any subsequent WSS connection. Says so every
    15 s while it's still waiting, rather than waiting silently. */
void syncSystemTime();

namespace esp32va_wifi {

/** Prints "@@WIFI <RSSI dBm>" - the current signal strength - or nothing if
    Wi-Fi isn't connected. */
void reportWifiSignal();

/** Call every loop() pass: reports the signal strength every 10 s, and a
    lost / regained connection as a @@STAGE wifi line when it happens. The
    ESP32 reconnects to the same network by itself. */
void monitorWifiConnection();

}  // namespace esp32va_wifi
