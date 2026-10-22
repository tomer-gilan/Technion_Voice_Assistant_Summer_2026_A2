#pragma once

// Small helpers shared by every module that answers a typed serial command
// (device controls, hands-free, Wi-Fi, the SD file transfer): splitting a
// command into its word and argument, and reading "on"/"off".

namespace esp32va_cmd {

/** If command is `word` alone or `word <argument>` (case-insensitive),
    returns the argument with leading spaces skipped ("" if there's none);
    otherwise nullptr - so "LED_BRIGHTNESS 5" is not an LED command, and
    "LISTEN" is not a LIST command. */
const char* commandArgument(const char* command, const char* word);

/** Parses "on"/"off" (case-insensitive) into on_out. False if it's neither. */
bool parseOnOff(const char* text, bool* on_out);

}  // namespace esp32va_cmd
