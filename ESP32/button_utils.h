#pragma once

// The onboard BOOT button (TALK_BUTTON_PIN, GPIO0) as an optional physical
// Talk button, so the board can be used without the laptop: a press starts
// a LISTEN like the Talk button, and a press while it records or answers
// cancels it. Off until switched on (BUTTON on), and the choice is kept in
// flash - a board that hasn't been tested with it behaves as before.
//
// Debounced with a saturating counter sampled every kButtonSampleMs by a
// periodic esp_timer (so a slow loop() pass doesn't stretch it): a press
// counts once kButtonDebounceSamples samples in a row agree, so a glitch
// only nudges the counter instead of restarting a timer.
//
// Status line: @@BUTTON <on|off>. Command: BUTTON on|off.

namespace esp32va_button {

/** Loads the saved on/off and, if on, sets the pin up. Call once from
    setup(). */
void setupTalkButton();

/** Handles BUTTON on|off. Returns false if command isn't BUTTON. */
bool handleButtonCommand(const char* command);

/** Prints "@@BUTTON <on|off>". */
void announceButtonState();

/** Call every loop() pass. Returns true exactly once per press (on the
    debounced press, not the release) - never while the button is off. */
bool takeButtonPress();

}  // namespace esp32va_button
