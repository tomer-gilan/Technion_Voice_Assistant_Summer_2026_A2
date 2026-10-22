#pragma once

// Wi-Fi and the clock: connecting (without ever blocking for good), the
// network to connect to, and the SNTP time sync TLS needs. Progress is
// reported as @@STAGE wifi / time lines (status_report_utils.h).
//
// The network: the one saved in flash from the control center (WIFI_SSID +
// WIFI_PASSWORD commands), or else SECRETS.h's WIFI_SSID / WIFI_PASSWORD -
// so a new network needs no compile. If the saved network fails
// kWifiAttemptsBeforeFallback attempts in a row (parameters.h), the board
// tries SECRETS.h's instead, and back again the same way - in RAM only, the
// saved one stays in flash. Status line:
//   @@WIFI_NET <saved|default> <ssid>   - which network the board uses
// (never the password).
//
// Commands:
//   WIFI                    - prints the network in use (@@WIFI_NET)
//   WIFI_SSID <name>        - the next network's name (to the end of the line)
//   WIFI_PASSWORD [<pass>]  - its password (none for an open network): saves
//                             both in flash and connects to it right away
//   WIFI_FORGET             - back to SECRETS.h's network

namespace esp32va_wifi {

/** Starts connecting to the network in use and waits up to wait_ms for it,
    printing progress. Returns true once connected; false if it isn't yet -
    the ESP32 keeps trying in the background, and monitorWifiConnection()
    takes it from there. Call once from setup(). */
bool connectToWifi(unsigned long wait_ms);

/** Starts SNTP (once) and waits up to wait_ms for a plausible clock, which
    TLS certificate validation needs. Returns true once the time is set;
    false if not yet (monitorWifiConnection() reports it when it is). */
bool syncSystemTime(unsigned long wait_ms);

/** True while Wi-Fi is connected. */
bool isWifiConnected();

/** True once the clock has been set by SNTP (it then stays set). */
bool isTimeSynced();

/** Prints "@@WIFI <RSSI dBm>" - the current signal strength - or nothing if
    Wi-Fi isn't connected. */
void reportWifiSignal();

/** Prints "@@WIFI_NET <saved|default> <ssid>" - the network in use. */
void announceWifiNetwork();

/** Handles WIFI / WIFI_SSID / WIFI_PASSWORD / WIFI_FORGET (see above).
    command is the trimmed line; raw_line the same line untrimmed (only its
    line ending removed), from which WIFI_PASSWORD takes the password, so
    spaces at its ends are kept. Returns false if command isn't one of
    them. Never prints the password. */
bool handleWifiCommand(const char* command, const char* raw_line);

/** Call every loop() pass: reports the signal strength every 10 s and a
    lost / regained connection as a @@STAGE wifi line, starts over on the
    network if it has been down for a while (the ESP32's own reconnect
    gives up on some errors), and starts / reports the time sync once
    connected if setup() couldn't. */
void monitorWifiConnection();

}  // namespace esp32va_wifi
