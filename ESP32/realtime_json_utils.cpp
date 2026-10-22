#include "realtime_json_utils.h"

#include <string.h>
#include <stdio.h>

bool extractEventType(const uint8_t* payload, size_t length, char* out, size_t out_size) {
  constexpr char kNeedle[] = "\"type\":\"";
  constexpr size_t kNeedleLen = sizeof(kNeedle) - 1;
  constexpr size_t kScanWindow = 128;
  size_t scan_limit = length < kScanWindow ? length : kScanWindow;

  for (size_t i = 0; i + kNeedleLen <= scan_limit; i++) {
    if (memcmp(payload + i, kNeedle, kNeedleLen) != 0) continue;
    size_t start = i + kNeedleLen;
    size_t j = start;
    while (j < length && payload[j] != '"' && (j - start) < out_size - 1) j++;
    memcpy(out, payload + start, j - start);
    out[j - start] = '\0';
    return true;
  }
  return false;
}

bool findRawStringField(const uint8_t* payload, size_t length, const char* field_name,
                         const uint8_t** value_start, size_t* value_len) {
  char needle[32];
  int needle_len = snprintf(needle, sizeof(needle), "\"%s\":\"", field_name);
  if (needle_len <= 0 || (size_t)needle_len >= sizeof(needle)) return false;

  for (size_t i = 0; i + (size_t)needle_len <= length; i++) {
    if (memcmp(payload + i, needle, needle_len) != 0) continue;
    size_t start = i + needle_len;
    size_t j = start;
    while (j < length && payload[j] != '"') j++;
    if (j >= length) return false;
    *value_start = payload + start;
    *value_len = j - start;
    return true;
  }
  return false;
}
