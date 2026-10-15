#pragma once

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

// Onboard-LED / set_board_state tool handling - extracted out of step 19's
// sketch into a shared module (step 20a), since "drive the LED" and "execute
// a set_board_state tool call" are reusable board/protocol capabilities,
// not test-specific LISTEN-cycle logic. Whether a tool call should count
// towards the current LISTEN cycle's state machine (e.g.
// function_call_turn_pending) stays a decision for the caller.

/** One-time setup: attaches LED_PIN to a PWM channel (so it can be dimmed -
    step 28) and drives it to the default (off) state. Call once from setup(). */
void setupBoardState();

/** Turns the onboard LED on (at the current brightness) or off, respecting
    this board's active-high/low polarity (config.h's LED_ACTIVE_HIGH). */
void setLed(bool on);

/** Handles a finalized response.function_call_arguments.done event for the
    registered set_board_state tool: parses the requested led_on value,
    drives the onboard LED, and sends the required function_call_output plus
    a follow-up response.create back over webSocket so the model can give
    its spoken confirmation. Also announces the change as "@@LED ... voice",
    so the laptop app's switch follows a spoken request. Any function name other than set_board_state,
    or a missing call_id / unparseable arguments, is logged and ignored.
    Returns true if a tool call was actually handled - the caller should
    then expect the *next* response.done to be for a function-call-only
    turn (no audio), not the real reply - false if nothing was done. */
bool handleSetBoardStateCall(JsonDocument& doc, WebSocketsClient& webSocket);

namespace esp32va_board {

/** Sets how bright the LED is when on, 0-100% (higher counts as 100%).
    Takes effect at once if it's on; remembered if it's off. */
void setLedBrightness(uint8_t percent);

/** Whether the LED is currently on. */
bool isLedOn();

/** The LED's brightness when on, 0-100%. */
uint8_t ledBrightnessPercent();

/** Prints "@@LED <on|off> <brightness %> <source>" so the laptop app's LED
    controls match the board. source says who changed it: "gui", "voice",
    or "status" (a STATUS reprint). */
void announceLedState(const char* source);

}  // namespace esp32va_board
