#pragma once

// Template for SECRETS.h - copy this file to SECRETS.h (same folder) and
// fill in your own values. SECRETS.h is in .gitignore, so your password and
// API key never reach git.
//
// The Wi-Fi network below is only the default: the control center's Wi-Fi
// card (or the WIFI_SSID / WIFI_PASSWORD commands) saves another one in the
// board's flash, without compiling again.

// The default Wi-Fi network (2.4 GHz - the ESP32 has no 5 GHz radio).
#define WIFI_SSID     "your-network-name"
#define WIFI_PASSWORD "your-network-password"

// An OpenAI API key with access to the Realtime API (platform.openai.com).
#define OPENAI_API_KEY "sk-..."
