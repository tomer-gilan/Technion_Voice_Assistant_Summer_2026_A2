# Voice Assistant on ESP32
**Project by:** Solomon Mekhiln, Tomer Gilan, Itamar Cohen

A project in the IoT course (236333), Technion, Summer semester 2026, group A2.

## About the project

We built a small voice assistant on an ESP32. You talk to it, it answers out loud, and it controls the board: it turns the LED on and off and dims it, changes the speaker volume, runs a timer and a reminder, tells the time, and says what part of the day it is according to a light sensor (the value is simulated from the control center).

Speech understanding happens in the cloud (OpenAI Realtime API). The ESP32 records from the microphone, streams the voice over Wi-Fi, plays the answer, and carries out the commands. The voice detector (Hands-free), the timer, the clock, the recordings and the settings run on the microcontroller itself. A control center in the browser (over a USB cable) shows what the board is doing and lets you change every setting without compiling.

![The control center in the middle of a conversation](Documentation/images/control-center.png)

<sub>The control center. The screenshot was made from board status lines that we fed into the page, with sentences from our logs.</sub>

## What the system can do

- **Push to talk:** Talk in the control center, the space bar, or the **BOOT button on the board** (optional). You speak for 3 seconds.
- **Hands-free:** no button. A voice activity detector (VAD) on the ESP32 starts recording when you start speaking and stops when you stop, and is not set off by knocks, claps or footsteps. You turn it on and off with a switch or by voice (`Switch to hands-free mode`, `Go back to the button`), only on an explicit request. While the control center plays a recording from the SD card, the microphone does not listen, so the recording is not taken as a question.
- **Controlling the board by voice:** `Turn on the light`, `Dim the light to 30 percent`, `Set the volume to 30 percent`, `A bit louder`.
- **Timer, reminder and clock:** `Set a timer for 30 seconds`, `Remind me in 30 seconds to check the oven`, `Turn off the light in 20 seconds`, `What time is it?`.
- **Part of the day:** `Is it day or night?` is answered according to the light sensor (a value simulated from the control center).
- **Board status:** `How are you doing?`: the Wi-Fi signal strength, how long the board has been on, and how much memory is free.
- **Conversation memory:** it remembers what was said earlier, for example your name.
- **English and Hebrew:** best in English. In Hebrew it understands and answers, but at a lower quality, and the transcript on the screen is sometimes garbled.
- **Works without the computer too:** Hands-free, the BOOT button and the status sounds are stored on the board, and a power bank is enough (connect it only after disconnecting the USB cable from the computer).
- **Robustness:** without Wi-Fi or without OpenAI (also when Wi-Fi is up but has no internet) the board goes to **Offline** mode, the OpenAI light turns red, and it keeps retrying every 30 seconds (the timer and the controls keep working). When the Wi-Fi or the hotspot comes back, or you switch networks, the connection to OpenAI reopens within seconds, and if OpenAI still does not answer after 10 seconds the board goes back to retrying every 30 seconds. A "stuck" connection is detected on its own: a heartbeat (a ping every 15 seconds), and a server that stays silent for 15 seconds during a question is closed and reopened (the conversation starts over). A network saved from the control center that fails twice in a row (for example a wrong password) is temporarily replaced by the network from `SECRETS.h`, and a saved API key that OpenAI refuses twice in a row is temporarily replaced by the key from `SECRETS.h` (the light says `API key refused`). Without an SD card it works without recordings. The Wi-Fi network and the API key are changed from the control center, without compiling. The details are in the [edge cases](Documentation/edge-cases.md) and in the [extended list](Documentation/edge-cases-extended.md).
- **Control center in the browser:** health lights for each component and a Self-test, the conversation as bubbles with response times, all the settings (saved in flash), statistics and a response-time graph with CSV export (with the date and the board's boot number), the recordings from the SD card with a player and a waveform, a typed question for a noisy room, and Replay of a saved log (works without a board, also on logs from `tools/serial_logger.ps1`, and does not mix with the live statistics or the saved log).

![Four voice commands in the control center](Documentation/images/demo.gif)

## Repository folders

| Folder | What is in it |
|---|---|
| `ESP32/` | The firmware: `ESP32.ino` (the flags, `setup()` and `loop()`) and its tabs: `state_machine.ino` (the state machine), `listen_turn.ino`, `server_events.ino`, `serial_commands.ino` and `verdicts.ino`, the `*_utils.cpp/.h` modules, `parameters.h` (all the fixed parameters), and `SECRETS.example.h` (a template for the secrets) |
| `control_center/` | The control center: `control_center.html` and `control_center.js` (the two files must stay in the same folder), which runs in Chrome or Edge |
| `Unit Tests/` | Unit tests for each hardware component (speaker, microphone, Wi-Fi, SD, LED, button, and the LDR once it is connected) and [their report](Unit%20Tests/README.md) |
| `Parameters/` | [The list of all the parameters](Parameters/parameters.md): what is changed from the control center, what is in `parameters.h`, and what is in `SECRETS.h` |
| `Documentation/` | All the documentation (list below), images, the measurement log, and `development_steps/` with all the steps we built along the way |
| `tools/` | Tools for the computer: a measurement summary from a log, a test of the voice detector on WAV files, a Serial logger, and an API diagnostic |

### The documentation

| Document | What is in it |
|---|---|
| [Quick start guide](Documentation/quick-start.md) | One page: how to start it and what to try |
| [System states and error messages](Documentation/system-states-and-errors.md) | What each state and each light means, and what to do |
| [Calibration instructions](Documentation/calibration.md) | The light sensor, the Hands-free sensitivity, the microphone level |
| [How the code is built](Documentation/architecture.md) | The modules, the state machine, the usage flow, and the protocol between the board and the control center |
| [Significant decisions](Documentation/decisions.md) | What we decided, why, and what it rests on (a measurement, an experiment or documentation) |
| [User stories](Documentation/user-stories.md) ([PDF](Documentation/user-stories.pdf)) | In the order of the demo |
| [Edge cases](Documentation/edge-cases.md) ([PDF](Documentation/edge-cases.pdf)) | The original list of edge cases we tested |
| [Edge cases - extended](Documentation/edge-cases-extended.md) ([PDF](Documentation/edge-cases-extended.pdf)) | In the course format (indication and recovery), with the cases of the submission version. **To print: `edge-cases-extended.pdf` (2 copies)** |
| [Demo plan](Documentation/demo-plan.md) and [expected questions](Documentation/questions.md) | Preparation for the submission |
| [Board test report](Documentation/board-tests-toSubmit.md) | The tests of the submission version on the board (robustness, Wi-Fi, BOOT, status sounds), all passed |
| [Project versions](Documentation/versions.md) | All the steps, from the first sound to the current version |

## Hardware

![The hardware](Documentation/images/hardware.png)

| Component | Quantity | Notes |
|---|---|---|
| ESP32-WROOM-32 DevKit | 1 | 520KB SRAM, no PSRAM. The LED (GPIO 2) and the BOOT button (GPIO 0) on the board |
| INMP441 microphone (I2S) | 1 | |
| MAX98357A amplifier (I2S) | 1 | |
| 4Ω 3W speaker | 1 | |
| MicroSD module (SPI) and MicroSD card (FAT32) | 1 + 1 | The recordings. Optional: without it the board works without recordings |
| LDR (light-dependent resistor) and a 10kΩ resistor | 1 + 1 | The light sensor. **Not connected:** the value is simulated from the control center, as the course guide instructs. It stays on the list because the code and the calibration for a real LDR are ready ([calibration](Documentation/calibration.md)) |
| Breadboard | 1 | |
| Jumper wires | A set, about 20 wires | Microphone 6 (including L/R to GND), amplifier 5, speaker 2, SD module 6 |
| USB cable | 1 | Power and connection to the computer (control center, flashing) |
| Power bank | 1 | Optional: the board also works on a power bank without a computer. Do not connect it together with the USB cable to the computer (safety, course instruction) |

### Wiring diagram

The microphone and the amplifier share the same I2S clock lines (one port, full duplex). The pins are defined in `ESP32/parameters.h`.

![Wiring diagram](Documentation/images/wiring.png)

| Signal | ESP32 pin |
|---|---|
| I2S bit clock (microphone SCK + amplifier BCLK) | 26 |
| I2S word select (microphone WS + amplifier LRC) | 25 |
| Data from the microphone (SD) | 21 |
| Data to the amplifier (DIN) | 22 |
| SD: CS / SCK / MISO / MOSI | 5 / 18 / 19 / 23 |
| Microphone channel select (L/R) | To GND (left channel), no ESP32 pin |
| Amplifier: the SD and GAIN inputs | Not connected (as in the diagram) |
| Speaker | + and - to the amplifier output |
| LED | 2 (on the board) |
| Talk button | 0 (BOOT, on the board) |
| LDR (not connected, simulated from the control center) | 34 (ADC1), once connected |
| Power | The microphone and the SD module to 3.3V, the amplifier to 5V, all to a common GND |

<details>
<summary>The same as text</summary>

```
                 ESP32-WROOM-32
              +------------------+
 INMP441 SCK -| GPIO 26 (BCLK)   |- BCLK    MAX98357A
 INMP441 WS  -| GPIO 25 (LRC)    |- LRC     MAX98357A
 INMP441 SD  -| GPIO 21 (DIN)    |
              | GPIO 22 (DOUT)   |- DIN     MAX98357A --> speaker
              |                  |
   SD CS     -| GPIO 5           |
   SD SCK    -| GPIO 18          |
   SD MISO   -| GPIO 19          |
   SD MOSI   -| GPIO 23          |
              |                  |
              | GPIO 2           |- onboard LED
              | GPIO 0           |- onboard BOOT button
  LDR divider-| GPIO 34 (ADC1)   |  (not wired: simulated from the control center)
              +------------------+
 VCC -> 3.3V (MAX98357A -> 5V), GND -> GND
```

</details>

## Software and versions

| What | Version |
|---|---|
| ESP32 SDK: ESP32 Arduino core (Espressif) | 2.0.17 (ESP-IDF 4.4) |
| Arduino IDE | 2.3.10 |
| WebSockets by Markus Sattler (Links2004) | 2.7.2 |
| ArduinoJson by Benoit Blanchon | 7.4.3 |
| OpenAI model | `gpt-realtime-2.1-mini` (voice to voice), and `gpt-4o-mini-transcribe` for the text shown in the control center |
| Control center | Chrome or Edge (needs Web Serial) |
| Python, for the tools `tools/measurements_report.py` and `tools/realtime_audio_diagnostic.py` | 3.6 or later (the tools use f-strings). Tested with 3.11.2. `measurements_report.py` uses only the standard library |
| `websocket-client` (only for `realtime_audio_diagnostic.py`) | `pip install websocket-client`. Tested with 1.8.0 |
| C++11 compiler, for the tool `tools/vad_replay.cpp` | Any `g++` version that supports C++11 (on Windows: MSYS2/MinGW) |
| PowerShell, for the logger `tools/serial_logger.ps1` | Windows PowerShell 5.1 (tested) |

Everything else comes with the ESP32 core: `WiFi.h`, `SD.h`, `driver/i2s.h`, `Preferences` (NVS), mbedTLS and FreeRTOS. Directly from ESP-IDF and FreeRTOS, and not through an Arduino wrapper, we used: the legacy I2S driver `driver/i2s.h` in full duplex mode, `xTaskCreatePinnedToCore` (the microphone task, pinned to core 1 at priority 3), `esp_timer` (sampling the BOOT button), `heap_caps_*` (memory diagnostics and failed allocations), `esp_random`, and mbedTLS for base64. Through the Arduino wrappers: NVS (`Preferences`, not directly), PWM (`ledc`), SNTP (`configTime`), SD and Wi-Fi.

## Installation and running

1. In the Arduino IDE, in the **Boards Manager**, install **esp32 by Espressif Systems** version **2.0.17**. Then install the two libraries from the **Library Manager**, at the versions in the table.
2. Copy `ESP32/SECRETS.example.h` to `ESP32/SECRETS.h` and fill in the Wi-Fi name and password (2.4GHz) and an OpenAI key. The file is in `.gitignore`. Without it, compilation stops with an explanation.
3. Open `ESP32/ESP32.ino` in the Arduino IDE, select **ESP32 Dev Module** and upload. It also works from the command line: `arduino-cli compile --fqbn esp32:esp32:esp32 ESP32`.
4. Open `control_center/control_center.html` in Chrome, click **Connect board** and select the board's COM port (115200).
5. Wait for the green lights and the opening greeting, then click **Talk** or turn on **Hands-free**.

**Safety:** a board connected to a computer by USB must not be connected to any other power source (including a power bank and the 5V pin). To use it without a computer, first disconnect the USB cable, and only then connect a power bank.

**A different network?** Use the **Wi-Fi network** card in the control center, without compiling. The Technion network did not work with the board. We did not check why (our guess: WPA2-Enterprise login or a sign-in page), so we demo on a hotspot from a phone.

**No board at hand?** Open the control center, click **Replay a log** and select `Documentation/measurements/esp32_log_2026-10-15.txt`. The log of the 26 questions plays as if the board were connected.

**Compiled file:** we did not include `compiled_program.bin`, because the password and the API key are compiled into it. To export it: in the Arduino IDE, Sketch → Export Compiled Binary.

## How it works

![The path of one sentence](Documentation/images/sentence-path.png)

1. A separate task (FreeRTOS) reads the microphone at 24kHz every 20ms, even when the main loop is busy.
2. Every 100ms of audio is sent right away over a single encrypted WebSocket connection to OpenAI. We do not wait for the end of the recording, because 3 seconds of raw audio do not even fit in the free memory.
3. The model answers with mu-law audio at 8kHz (6 times smaller than PCM at 24kHz). The ESP32 decodes and plays it.
4. When the model wants to do something on the board, it calls a "tool" with a JSON schema. The ESP32 carries it out and returns a result, and the model says what was done.
5. The board constantly prints `@@...` lines to Serial, and the control center reads them ([the protocol](Documentation/architecture.md)).

### The tools the model can call

| Tool | What it does | Example |
|---|---|---|
| `set_board_state` | Turns the LED on or off, and can also set the brightness | `Turn on the light`, `Dim the light to 30 percent` |
| `get_time_of_day` | Reads the light sensor (simulated from the control center) and returns day, evening or night | `Is it day or night?` |
| `set_volume` | Sets the volume in percent (10-100), or raises / lowers it by 20% | `Set the volume to 30 percent`, `Louder` |
| `get_current_time` | The date and time from the board's clock (synchronized from the internet, in Israel time including daylight saving time), and how much time is left on the timer | `What time is it?` |
| `get_board_status` | The Wi-Fi signal strength, how many minutes the board has been on and how much memory is free | `How are you doing?` |
| `set_hands_free` | Turns Hands-free on or off, exactly like the switch in the control center. Only when the current question explicitly asks for it: a question about the mode, or a passing mention of a button, changes nothing | `Switch to hands-free mode`, `I want to talk without pressing a button`, `Disable VAD` |
| `set_timer` | A timer of 1 to 3600 seconds (20 to 60 seconds in the demo), or a cancel (0). Above an hour, it sets one hour and the assistant says so. A negative value or one shorter than a second is rejected, and the assistant asks again without touching the running timer. At the end the assistant says so out loud (including what the reminder is about), and it can turn the light on or off | `Remind me in 30 seconds to check the oven`, `Turn off the light in 20 seconds` |

![What happens when you ask to turn on the light](Documentation/images/led-sequence.png)

## Performance measurements

### How we measured

We added measurement points to the firmware (`measurement_utils`). Every question prints a `@@TIMING` line, and every boot prints a `@@BOOT_TIMING` line, with times from `millis()`, sizes, microphone levels and free memory. We saved the log from the control center and ran `tools/measurements_report.py` on it.

Test conditions (15.10.2026): a hotspot from a phone, RSSI at boot between -37 and -45 dBm and a median of -48 during the questions (range -38 to -62), a quiet room, 26 questions and 3 boots. Questions 1-19 were normal speech from about half a meter, 20-22 quiet, 23-24 from far away (about 2 meters), 25 close, 26 a whisper. Push-to-talk mode (3 seconds). All the times in a question are measured from the end of the recording. **The firmware in this measurement is step 29's, before the microphone task of step 30.** The full log: `Documentation/measurements/esp32_log_2026-10-15.txt`.

### Results

![Response time](Documentation/images/response-time.png)

| What | Result |
|---|---|
| Questions that finished successfully | 26 of 26 |
| From the end of the recording to the first message from the server (median) | About 0.3 seconds |
| From the end of the recording to the first sound of the answer (median) | About 1.1 seconds |
| Questions where the answer started in under 2 seconds | 23 of 26 (the slow ones, about 5 seconds, were quiet speech and speech from far away) |
| From the request `Turn on the light` until the LED turned on | 0.97 seconds (the spoken confirmation started after 1.9 seconds) |
| Full boot (until ready to talk) | 6.8 / 9.1 / 10.8 seconds |
| The longest stage of the boot | The encrypted connection to OpenAI, 3.9 to 5.1 seconds |

![The statistics panel](Documentation/images/statistics.png)

![Boot and memory](Documentation/images/boot-and-memory.png)

| Memory (heap) | |
|---|---|
| Free at boot | 215KB |
| Free when the session is ready | 38KB |
| Free during a conversation | About 45KB, stable over 26 questions (no leak) |
| Least free during a conversation | 27KB |
| The largest consumer | The encrypted connection and the WebSocket, about 51KB |
| Firmware size (the current version) | 1.08MB, 82% of the flash |
| Static RAM (global variables) | 75KB of 320KB |

| Audio | |
|---|---|
| Microphone level, normal speech (median) | -19 dBFS |
| Close / far / whisper | -17 / -30 / -32 dBFS |
| Clipping | In 1 question of 26 |
| Sent to the cloud per question | About 193KB (PCM at 24kHz, 384 kbps) |
| The answer's audio | mu-law at 8kHz, 64 kbps |

### A problem we found in the measurements and fixed

In 21 of the 26 questions, the firmware warned that microphone samples were lost. The cause: the microphone's DMA buffer holds only 150ms, and sometimes a single send over Wi-Fi got stuck for up to 465ms, so old audio was overwritten. On average 0.14 seconds out of 3 were lost (0.54 in the worst case). The amount of lost audio matched the slowest send in that question (correlation coefficient 0.93).

The fix (step 30): a separate FreeRTOS task reads the microphone every 20ms, and the audio waits in a queue (inside the playback buffer, which is free while recording) until it is sent. A slow send only delays the audio and does not lose it. The queue holds about 1.36 seconds. For every question, the firmware reports how much audio was lost (`dropped_ms`), so a repeat measurement with the current firmware will show the result.

**The measurement after the fix has not been run yet**, so the tables above are for step 29's firmware only. It will be made like this: run a few questions in a row on the current firmware while `tools/serial_logger.ps1` saves the log, then `python tools/measurements_report.py <log file>` writes `timing.csv` and `summary.md` (in a `measurements` folder next to the log). The `dropped_ms` row (median and maximum) will be added to the results table above, next to the step 29 measurement.

### The voice detector of Hands-free mode

The voice detector (`vad_utils`) compares the energy in the speech frequency band with the room noise, and also checks whether the sound is "voiced" and whether it has a syllable rhythm. We tested it on the computer with `tools/vad_replay.cpp` on public recordings: 112 noise clips and 100 speech clips with household noise in the background.

| Test | Clips | Result | Rate |
|---|---|---|---|
| Normal speech: the recording started, and the first word was not cut off | 100 | In all of them | 100/100 = **100%** |
| Very quiet speech against background noise: 20 to 200ms of the start were cut off | 27 | In 7 | 7/27 = **26%** cut off (74% with no cut) |
| Noise clips that were sent to OpenAI (false trigger) | 112 | In 20 | 20/112 = **17.9%** (82.1% rejected) |
| Knocks, claps, footsteps, rain, a vacuum cleaner | Out of the 112 | Never started a recording | 0% |

The false triggers were coughing, laughter, a creaking door, bells and running water.

**How we tested:**
- **The tool:** `tools/vad_replay.cpp` compiles the same `ESP32/vad_utils.cpp` file that runs on the board, and runs it on files on the computer. It simulates the Hands-free loop: it waits for speech, records from 500ms before the start was confirmed, stops after silence or discards a clip that did not sound like speech, and goes back to waiting. That way we know for each clip whether it would have been sent to OpenAI, and where the recording would have started.
- **Build and run:** `g++ -std=c++11 -O2 -I ESP32 tools/vad_replay.cpp ESP32/vad_utils.cpp -o vad_replay`, then `vad_replay [--sensitivity 1-5] [--hangover <ms>] <files>`. For example: `vad_replay --sensitivity 3 --hangover 1000 clips/*.wav`.
- **Parameters:** the firmware defaults (`ESP32/parameters.h`: `kDefaultVadSensitivity`, `kDefaultVadHangoverMs`): sensitivity 3 (on a scale of 1 to 5) and 1000ms of silence to end a sentence (range 600 to 1500ms).
- **Recordings:** 16-bit WAV files, mono or stereo, at any sample rate. The tool mixes them down to one channel and resamples them to 24kHz (linear interpolation), the rate the microphone on the board records at.
- **The clip sets:** 112 noise clips (coughing, laughter, a creaking door, bells, running water, knocks, claps, footsteps, rain, a vacuum cleaner), and 100 speech clips with household noise in the background. The test of very quiet speech against background noise (cutting off the start of the sentence) was done on 27 clips.
- **On the board:** the same detector also runs on the board, and a test on the board passed: 5 questions in a row by voice, and knocks on the table that did not start a recording (`HANDS-FREE PASSED`, see [the report](Documentation/board-tests-toSubmit.md)).

The numbers in the table are from recordings on the computer, not from our microphone in the room. The response times and memory above were measured on step 29's firmware, before the microphone task of step 30 (see "How we measured").

## Known limitations

- The LDR is not connected: the value is simulated from the control center, as the course guide instructs. The wiring and calibration for when it is connected are in [calibration.md](Documentation/calibration.md).
- **Hebrew:** you can speak Hebrew, and the assistant answers in Hebrew, but not as well as in English. The transcript shown in the control center (what the user said) is often garbled in Hebrew. Both sides work best in English ([decisions.md](Documentation/decisions.md)).
- In Hands-free mode, a TV or other people talking count as speech.
- If you start speaking within about 0.9 seconds after an answer ends, the start of the sentence may be lost (the microphone is closed for 0.6 seconds so the board does not hear itself).
- Without internet the assistant does not answer questions (Offline). The timer, the controls and the recordings keep working.
- The API key is stored in the firmware (and, if it was changed from the control center, also in flash). It does not appear in the log, but anyone who holds the board can read it. In a real product the board would get a temporary key from a server.

## Division of work

| Who | What |
|---|---|
| Tomer Gilan | *(to be completed)* According to git: steps 2-22 (the hardware, Gemini, the move to OpenAI, the first tool, the logs and the modules) |
| Solomon Mekhiln | *(to be completed)* According to git: the light sensor, the SD recordings, and the first control center (steps 23-27) |
| Itamar Cohen | *(to be completed)* According to git: the control center (step 28), the measurements (29), the voice detector and Hands-free (30) |

---

This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion. https://icst.cs.technion.ac.il/
