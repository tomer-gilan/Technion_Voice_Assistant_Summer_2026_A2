#pragma once

#include <ArduinoJson.h>
#include <WebSocketsClient.h>

// The board's clock and a kitchen-timer, as two more tools for the model:
//
//   get_current_time  -> "It is 21:05 on Saturday, 17 October 2026." (from
//                        the SNTP-synced clock, TIMEZONE_UTC_OFFSET_SEC applied),
//                        plus " The timer has 40 seconds left." while one runs
//   set_timer         {"seconds": N} -> rings in N seconds (1 to 3600);
//                        {"seconds": 0} cancels a running timer. An optional
//                        "label" ("check the oven") turns it into a reminder.
//
// When the timer is due and the board is idle, it "rings": the LED blinks
// and the speaker beeps 3 times (~1.4 s, blocking - like BEEP, it holds
// hands-free off first so the mic doesn't take the beeps for speech). Then
// the sketch has the assistant say it out loud (sendTimerAnnouncement()),
// with the label if there is one: "Time's up - check the oven."
// Status lines for the laptop app (tools/reply_audio_app.html):
//
//   @@TIMER <seconds left> <seconds total> [label] - set, and in the STATUS reply
//   @@TIMER off                                    - cancelled, or none running (STATUS)
//   @@TIMER done [label]                           - it just rang

namespace esp32va_clock {

/** Handles a finalized response.function_call_arguments.done event for the
    get_current_time tool: sends the local date and time as the
    function_call_output plus a response.create so the model says it.
    Returns true if handled, false (logged) if call_id is missing. */
bool handleGetCurrentTimeCall(JsonDocument& doc, WebSocketsClient& webSocket);

/** Handles the set_timer tool: arms (or with 0, cancels) the timer, prints
    @@TIMER, and sends the function_call_output plus a response.create so
    the model confirms it. Returns true if handled, false (logged) if
    call_id is missing or the arguments have no usable seconds. */
bool handleSetTimerCall(JsonDocument& doc, WebSocketsClient& webSocket);

/** Call every loop() pass. board_idle: nothing is being recorded, sent,
    played or saved. Rings a due timer once the board is idle - a timer that
    comes due mid-reply waits for the reply to end. Returns true right after
    it rang, so the caller can have it announced (sendTimerAnnouncement()). */
bool serviceTimer(bool board_idle);

/** Asks the model to say the timer is done (with its label, if any) - a
    response.create nobody is waiting on, like the opening greeting. */
void sendTimerAnnouncement(WebSocketsClient& webSocket);

/** Prints the current @@TIMER line - part of the STATUS reply. */
void announceTimerState();

}  // namespace esp32va_clock
