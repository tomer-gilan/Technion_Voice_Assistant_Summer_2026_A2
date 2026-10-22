#include "settings_store_utils.h"

#include <Arduino.h>
#include <Preferences.h>
#include "log_utils.h"

namespace esp32va_settings {

namespace {
const char kStorageNamespace[] = "esp32va";  // NVS namespace, at most 15 characters
Preferences preferences;
}  // namespace

int32_t loadSetting(const char* key, int32_t default_value) {
  if (!preferences.begin(kStorageNamespace, true)) {
    // Expected on a board that has never saved anything: the namespace
    // doesn't exist until the first write.
    LOG_DEBUG("No saved settings yet - using the default for %s.\n", key);
    return default_value;
  }
  int32_t value = preferences.getInt(key, default_value);
  preferences.end();
  return value;
}

void saveSetting(const char* key, int32_t value) {
  if (!preferences.begin(kStorageNamespace, false)) {
    LOG_ERROR("Couldn't open the settings storage - %s not saved.\n", key);
    return;
  }
  if (preferences.putInt(key, value) == 0) {
    LOG_ERROR("Saving %s failed.\n", key);
  }
  preferences.end();
}

bool loadTextSetting(const char* key, char* out, size_t out_size) {
  if (out_size > 0) {
    out[0] = '\0';
  }
  if (!preferences.begin(kStorageNamespace, true)) {
    LOG_DEBUG("No saved settings yet - nothing stored for %s.\n", key);
    return false;
  }
  bool found = preferences.isKey(key) && preferences.getString(key, out, out_size) > 0;
  preferences.end();
  return found;
}

void saveTextSetting(const char* key, const char* text) {
  if (!preferences.begin(kStorageNamespace, false)) {
    LOG_ERROR("Couldn't open the settings storage - %s not saved.\n", key);
    return;
  }
  if (preferences.putString(key, text) == 0 && text[0] != '\0') {
    LOG_ERROR("Saving %s failed.\n", key);
  }
  preferences.end();
}

void removeSetting(const char* key) {
  if (!preferences.begin(kStorageNamespace, false)) {
    LOG_ERROR("Couldn't open the settings storage - %s not removed.\n", key);
    return;
  }
  if (preferences.isKey(key)) {
    preferences.remove(key);
  }
  preferences.end();
}

}  // namespace esp32va_settings
