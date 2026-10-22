#pragma once

// Includes SECRETS.h, or stops the build with a clear message when it hasn't
// been created yet (it isn't in git - only its template is).
#if __has_include("SECRETS.h")
#include "SECRETS.h"
#else
#error "ESP32/SECRETS.h is missing: copy ESP32/SECRETS.example.h to ESP32/SECRETS.h and fill in your Wi-Fi network and OpenAI API key."
#endif
