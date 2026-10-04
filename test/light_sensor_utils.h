#pragma once

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

// Ambient light level -> part of the day (day / evening / night), plus the
// get_time_of_day tool that lets the model ask for it. The physical sensor
// (an LDR read as analogRead(pin) * 1.5) isn't wired up yet, so for now
// g_light_sensor_value is set by hand with the LIGHT serial command instead.
// Same split as board_state_utils.h: whether a handled tool call counts
// towards the current LISTEN cycle's state machine stays the caller's call.

namespace esp32va_light_sensor {

/** Highest reading the sensor can produce: analogRead()'s 12-bit max (4095)
    scaled by the same * 1.5 the real reading uses, truncated to int (6142). */
constexpr int kMaxLightSensorValue = 4095 * 3 / 2;

/** Latest light level, 0 (dark) to kMaxLightSensorValue (bright). Written by
    the LIGHT serial command for now; the real sensor will write
    analogRead(<pin>) * 1.5 here instead. That pin must be an ADC1 pin
    (GPIO 32-39) - ADC2 pins can't be read while Wi-Fi is running. */
extern int g_light_sensor_value;

/** Logs the starting light level and how to change it. Call once from
    setup(); the real sensor's pin setup will go here too. */
void setupLightSensor();

/** Handles a typed Serial command if it's a LIGHT command: "LIGHT <value>"
    sets g_light_sensor_value (0 to kMaxLightSensorValue, anything else is
    rejected with a log line), bare "LIGHT" logs the current value and its
    part of the day. Case-insensitive. Returns true if `command` was a LIGHT
    command (valid or not), false if it's something else for the caller to
    handle. */
bool handleLightCommand(const char* command);

/** Handles a finalized response.function_call_arguments.done event for the
    get_time_of_day tool: maps g_light_sensor_value (read now, not cached)
    to "day"/"evening"/"night", logs it, and sends the function_call_output
    plus a follow-up response.create so the model speaks the answer. Returns
    true if handled - the caller should then expect the next response.done
    to be the function-call-only turn, same as handleSetBoardStateCall() -
    or false (logged) if call_id is missing. */
bool handleGetTimeOfDayCall(JsonDocument& doc, WebSocketsClient& webSocket);

}  // namespace esp32va_light_sensor
