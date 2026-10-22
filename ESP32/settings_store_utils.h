#pragma once

#include <stddef.h>
#include <stdint.h>

// Settings that should survive a reboot (step 28: speaker volume and mute,
// LED brightness, the light sensor's day/evening thresholds), kept in the
// ESP32's built-in non-volatile storage (NVS flash) through the core's
// Preferences library - not on the SD card, so they load before it's
// mounted and work without one. Each value is a small integer (or, for the
// Wi-Fi network, a short text) under a short key (NVS keys are at most 15
// characters).

namespace esp32va_settings {

/** Returns the stored value for key, or default_value if nothing was ever
    stored (or storage can't be opened - logged). */
int32_t loadSetting(const char* key, int32_t default_value);

/** Stores value under key. A write wears the flash a little, so call it when
    a setting actually changes, not on every loop() pass. Logs a failure. */
void saveSetting(const char* key, int32_t value);

/** Copies the stored text for key into out (cut to fit, always
    terminated) and returns true - or returns false, leaving out empty, if
    nothing was ever stored under key. */
bool loadTextSetting(const char* key, char* out, size_t out_size);

/** Stores text under key. Same flash-wear caveat as saveSetting(). */
void saveTextSetting(const char* key, const char* text);

/** Forgets whatever is stored under key (fine if nothing is). */
void removeSetting(const char* key);

}  // namespace esp32va_settings
