#pragma once

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

// Serial commands that control the board's own hardware - mostly sent by the
// laptop app's control panel (tools/reply_audio_app.html), but just as
// usable typed by hand (step 28):
//
//   VOLUME <0-100>         speaker volume in %        -> @@VOLUME <percent> <muted|unmuted>
//   MUTE <on|off>          silence / unsilence it      -> @@VOLUME ...
//   BEEP                   0.3 s test tone, when idle  -> @@BEEP played, or @@BEEP busy
//   LED <on|off>           onboard LED                 -> @@LED <on|off> <brightness> gui
//   LED_BRIGHTNESS <0-100> its brightness in %         -> @@LED ...
//
// The volume can also be set by voice, through the set_volume tool
// (handleSetVolumeCall() below) -> @@VOLUME ...
//
// Volume, mute and brightness are kept across reboots (settings_store_utils.h);
// the LED itself always starts off, as before. (BEEP doesn't answer @@BUSY:
// the app reads @@BUSY as the answer to a pending LIST/GET.) A malformed command is logged
// and answered with the current state, so the app's controls snap back.

namespace esp32va_controls {

/** Loads the saved volume, mute and LED brightness and applies them. Call
    once from setup(), after setupBoardState() and setupI2sFullDuplex(). */
void setupDeviceControls();

/** Handles a typed Serial command if it's one of the above
    (case-insensitive). idle_for_beep says whether a BEEP may play now -
    nothing being recorded or played. Returns true if `command` was one of
    these commands (handled or rejected), false if it's something else. */
bool handleControlCommand(const char* command, bool idle_for_beep);

/** Handles a finalized response.function_call_arguments.done event for the
    set_volume tool: arguments {"percent": 0-100} sets that volume, or
    {"step": "up"|"down"} changes it by kVoiceVolumeStepPercent. Unmutes the
    speaker (the user asked to hear it), saves the volume, announces
    @@VOLUME, and sends the function_call_output plus a response.create so
    the model confirms it out loud. Returns true if handled, or false
    (logged) if call_id is missing or the arguments say neither. */
bool handleSetVolumeCall(JsonDocument& doc, WebSocketsClient& webSocket);

/** Sets the LED's brightness (0-100%) and saves it, like LED_BRIGHTNESS -
    for set_board_state's brightness_percent (board_state_utils.h). */
void setAndSaveLedBrightness(uint8_t percent);

/** Prints the current @@VOLUME and @@LED lines - part of the STATUS reply. */
void announceControlStates();

}  // namespace esp32va_controls
