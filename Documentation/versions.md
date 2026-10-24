# Project Versions - How We Built the Assistant, Step by Step

Group A2, IoT course, Summer 2026. We built the project in small steps. Each step was a complete sketch that worked on its own, and the top of each file says what it adds and how to check that it passed. The old steps are kept in the `Documentation/development_steps/` folder as `NN_*.ino.inactive` (the hardware tests 02-05 are in `Unit Tests/`). They were written against the shared modules as they were at that step, so they document the process and are not code that compiles today as is. The current version is `ESP32/ESP32.ino` (step 30 and the improvements after it).

## 1. The hardware (steps 2-5)

| Step | What was tested |
|---|---|
| 02 | A 440Hz tone on the speaker through the MAX98357A amplifier. Checks the speaker wiring |
| 03 | The INMP441 microphone: prints the minimum, maximum and RMS every half second |
| 04 | Recording and then playback (loopback). The two components share the same I2S clocks |
| 05 | Connecting to Wi-Fi, with automatic reconnection |

These steps are now the unit tests in [Unit Tests](../Unit%20Tests/README.md), together with tests for the SD card, the LED, the button and the LDR.

## 2. A first attempt with Gemini Live (steps 6-8)

| Step | What was tested |
|---|---|
| 06 | An encrypted connection (TLS + WebSocket) to the Gemini server |
| 07a / 07b | A minimal setup message, and then a full one with instructions and an LED tool |
| 08a | A text question and answer. We learned that Gemini's audio models answer only in audio, not in text |
| 08b | Sending real audio from the microphone in 100ms chunks, without keeping the whole recording in RAM |

## 3. Moving to OpenAI Realtime (steps 9-16)

| Step | What was added |
|---|---|
| 09 | An encrypted connection to OpenAI with authentication in the `Authorization` header |
| 10 | A `session.update` message (model, instructions, audio format) and a confirmation from the server |
| 11 | A text question and answer |
| 12 | Audio from the microphone to the server, in small chunks |
| 13 | Playing the answer on the speaker, chunk by chunk |
| 14 | Transcription of both sides, and a conversation that remembers the previous questions |
| 15 | The first tool: `set_board_state` turns the LED on and off |
| 16 | Many questions in a row and a clean reconnection when the network drops |

## 4. Tidying the code and behavior (steps 17-22)

| Step | What was added |
|---|---|
| 17 | Log macros (`LOG_ERROR` / `LOG_INFO` / `LOG_DEBUG`) instead of scattered prints |
| 18 | Short answers: one sentence |
| 19 | Pressing the BOOT button starts a recording, like the `LISTEN` command. In step 20 we gave it up because of false readings (the likely explanation was found in the submission stage - see [decisions.md](decisions.md)) |
| 20a | Splitting the code into modules (`*_utils.cpp/.h`) |
| 21 | A bug fix: repeated silence turned the LED on and off without being asked |
| 22 | A random opening greeting when the board is ready |

## 5. Features (steps 23-29)

| Step | What was added |
|---|---|
| 23 | The `get_time_of_day` tool: day, evening or night according to a light sensor (simulated from the control center) |
| 24 | An SD card alongside the connection to OpenAI, and memory measurement at each stage of the boot |
| 25 | Saving the answers on the SD card, and transferring files to the computer (`LIST` / `GET` / `DELETE`) |
| 27 | Saving the microphone recording on the SD card, to hear what the board heard |
| 28 | The control center in the browser: `@@` lines with the board's state, and buttons for the LED, volume and a beep |
| 29 | Time and memory measurements for every question, and a script that summarizes them (see "Performance measurements" in the README) |

There is no file for step 26. The code of step 28 is inside step 29, which is built directly on it.

## 6. The current version (step 30, `ESP32/ESP32.ino`)

| What | Where |
|---|---|
| Hands-free: a voice activity detector (VAD) that starts and stops the recording on its own | `vad_utils`, `hands_free_utils` |
| A separate task that reads the microphone every 20ms, with 500ms before the start of speech | `mic_capture_utils` |
| A tool for testing the detector on WAV files on the computer | `tools/vad_replay.cpp` |
| The control center: Hands-free, a sound level meter, sliders, statistics | `control_center/control_center.html` |

### The improvements toward the submission

| Area | What was added |
|---|---|
| Voice tools | Volume in percent (`set_volume`), dimming, time and date, a timer and a reminder, `How are you doing?` (`get_board_status`), `What can you do?` |
| Firmware | The timer rings even without internet, a typed question for a noisy room (`ASK`), support for a real LDR (in the submission the value is simulated from the control center) |
| Control center | A Tour for the examiners, Self-test, a Demo checklist that checks itself off, a demo clock, Stage view, Summary, Save report, replaying a saved log |
| Documents | A visual README, user stories and edge cases for printing, a quick start guide, a demo plan, the code structure, a wiring diagram, and this document |

## 7. The submission version

Changes according to the course's criteria (robustness, settings without compiling, the git structure). Each firmware change is a separate commit.

| Area | What changed |
|---|---|
| Repository structure | Following the course template: `ESP32/`, `Unit Tests/`, `Documentation/`, `Parameters/`, `control_center/`. The old configuration file (`config.h`) was split into `parameters.h` (all the parameters) and `SECRETS.h` (not in git, with a template) |
| Robustness | Without an SD card the board does not stop; it never gives up on the connection to OpenAI (Offline mode, an attempt every 30 seconds, and an immediate attempt when the Wi-Fi comes back); a heartbeat and a server that stays silent for 15 seconds close a stuck connection; a saved network that fails twice is temporarily replaced by the network from `SECRETS.h`; Talk is rejected during a greeting, a timer message and an answer to a typed question; `setup()` does not get stuck without Wi-Fi; a fix for a bug where 5 disconnects scattered over time stopped the board |
| Settings | A Wi-Fi network is saved in flash from the control center (`WIFI_SSID` / `WIFI_PASSWORD`, the password with the spaces at its ends), and so is the API key (`OPENAI_KEY`: reconnects right away, and a saved key that is refused twice is temporarily replaced by the key from `SECRETS.h`). The clock follows Israel's time zone with an automatic switch to daylight saving time |
| Hands-free | Turns on and off by voice too, with a seventh tool (`set_hands_free`), only on an explicit request and only while answering a question. The microphone does not listen while the control center plays a recording from the SD card (`LAPTOP_PLAYBACK`), so the recording is not taken as a question |
| Control center | An OpenAI API key card (the key does not go into the log), and the examples in Try saying are a list to read and not buttons |
| Physical interface | The BOOT button as the Talk button and status sounds, both off by default |
| I2S | No MCLK pin (before, it went out on GPIO0, the BOOT button's pin) |
| Code | Each module in its own namespace, zero compiler warnings in our code, small fixes in the tools |
| Unit tests | 02-05 are standalone, and new tests for the SD card, the LED, the BOOT button and the LDR (the LDR test was written but not run, because the sensor is not connected) |
| Documentation | Significant decisions, states and error messages, calibration, parameters, edge cases in the course format, the state machine and the usage flow |

## Software versions

The versions of the IDE, the ESP32 core, the libraries and the model: in the "Software and versions" table in the [README](../README.md).
