# Board Test Report - Submission Version

All the tests below were run on the board with the submission firmware, **and they all passed** (✅). The report covers all the changes of the submission version compared with the previous version: first tests that confirm that what worked before still works, and then the new features (no SD card, Offline mode, switching networks without compiling, the BOOT button and status sounds). Each firmware change is a separate commit, so every test can be linked to the change it checks.

**Setup:** `ESP32/SECRETS.example.h` was copied to `ESP32/SECRETS.h` with the hotspot details and the API key, and then `ESP32/ESP32.ino` was uploaded.

## A. What worked before still works

| # | Test | What should happen | Commit tested | Result |
|---|---|---|---|---|
| 1 | Normal boot (hotspot on, card inserted) | All the lights green, an opening greeting, boot within about 7-11 seconds as before | All | ✅ |
| 2 | Test beep and Talk with a question | A clean beep, a recording, an answer heard with no new noises | `I2S: no master clock pin` | ✅ |
| 3 | 10 questions in a row, some with tools (LED, volume, timer) | Everything as before, `LISTEN #N PASSED` | All | ✅ |
| 4 | Hands-free: 5 questions by voice, and knocks on the table | Starts on speech, not on knocks, `HANDS-FREE PASSED` | All | ✅ |
| 5 | The recordings: Play, Download, Delete | Works as before | `Robustness...` | ✅ |
| 6 | The settings are kept after a Reset | Volume, brightness, Hands-free | | ✅ |

## B. The new changes

| # | Test | What should happen | Result |
|---|---|---|---|
| 7 | Reset **without** an SD card | A red SD light with `running without recordings`, the assistant answers questions, LIST returns `no SD card`. **No** HALTED | ✅ |
| 8 | Reset when the hotspot is **off**, and turn it on after 30 seconds | After 20 seconds the boot continues, the circle shows Offline, the controls work. When the hotspot comes up, OpenAI connects on its own and it goes back to Ready | ✅ |
| 9 | Turn off the hotspot in the middle of work for a minute, and turn it back on | Offline (`no Wi-Fi`), and then back to Ready on its own, without a Reset | ✅ |
| 10 | A hotspot with no internet (or a wrong API key) | After 5 attempts: a red OpenAI light `offline - retrying every 30 s`, Offline. **Not** HALTED | ✅ |
| 11 | Wi-Fi network card: a different hotspot | `@@WIFI_NET saved <name>`, the board connects to the new network, and stays on it after a Reset. In the log the password appears as asterisks | ✅ |
| 12 | Back to default | Returns to the network from `SECRETS.h` (`@@WIFI_NET default ...`) | ✅ |
| 13 | A wrong password on a saved network | A red Wi-Fi light `check the password`, Offline, a retry every minute. Back to default fixes it (this is what was tested on the board. Since then an automatic switch to the network from `SECRETS.h` after two failed attempts was added, and it has not been tested on the board yet) | ✅ |
| 14 | `Unit Tests/08_boot_button`, as is | 0 raw changes when not touched, and PASSED after 5 presses | ✅ |
| 15 | The same test with `kStartI2sWithMclkOnGpio0 = true` | A reverse experiment, for comparison: if the suspicion is right (the MCLK clock goes out on GPIO0), there will be many raw changes without touching | ✅ |
| 16 | In the firmware: On the board itself → BOOT button, then press BOOT | A 3-second recording starts. Another press in the middle cancels it. Without touching: no recording starts on its own for 5 minutes | ✅ |
| 17 | Status sounds on, then turn off the hotspot | Two falling tones when it goes Offline, and two rising tones when it comes back. Hands-free does not start recording from the tones | ✅ |
| 18 | `Set the volume to 0 percent` by voice | The volume is set to 10%, and the assistant confirms out loud | ✅ |
| 19 | The board runs for an hour or more without being touched | It does not go to HALTED even after server disconnects (before: after 5 scattered disconnects) | ✅ |
| 20 | `Unit Tests/06_sd_card` and `07_led_pwm` (08 was checked in test 14) | PASSED / what each one says | ✅ |

