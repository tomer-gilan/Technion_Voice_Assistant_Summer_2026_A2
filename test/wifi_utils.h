#pragma once

// Wi-Fi connection helpers shared by every networked test sketch. Both
// functions read WIFI_SSID / WIFI_PASSWORD straight from config.h, so no
// parameters are needed - just #include this and call them from setup().

/** Blocks until Wi-Fi connects, logging progress and the assigned IP. */
void connectToWifi();

/** Blocks until SNTP has set a plausible system time, required for TLS
    certificate validation on any subsequent WSS connection. */
void syncSystemTime();
