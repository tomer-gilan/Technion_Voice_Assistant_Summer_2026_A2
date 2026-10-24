# Voice Assistant on ESP32
**Project by:** Solomon Makhlin, Tomer Gilan, Itamar Cohen

A project in the IoT course (236333), Technion, Summer semester 2026, group A2.

## About the project

We built a small voice assistant on an ESP32. You talk to it, it answers out loud, and it controls the board: it turns the LED on and off and can dim it, changes the speaker volume, runs a timer and a reminder, tells the time, and says what part of the day it is according to a light sensor (the value is simulated from the control center).

Speech understanding happens in the cloud (OpenAI Realtime API). The ESP32 records from the microphone, streams the voice over Wi-Fi, plays the answer, and carries out the commands. The voice detector (Hands-free), the timer, the clock, the recordings and the settings run on the microcontroller itself. A control center in the browser (over a USB cable) shows what the board is doing and lets you change every setting without compiling.

![The control center in the middle of a conversation](Documentation/images/control-center.png)

<sub>The control center. The screenshot was made from board status lines that we fed into the page, with sentences from our logs.</sub>

## What the system can do

- **Push to talk:** Talk button in the control center, the space bar, or the **BOOT button on the board** - You speak for 3 seconds.
- **Hands-free:** no button. A voice activity detector (VAD) on the ESP32 starts recording when you start speaking and stops when you stop, and is not set off by non speech voices like knocks, claps or footsteps. You turn it on and off with a switch in the Web control center or by a voice command (`Switch to hands-free mode`, `Go back to the button`).
- **Controlling the board by voice:** `Turn on the light`, `Dim the light to 30 percent`, `Set the volume to 30 percent`, `A bit louder`.
- **Timer, reminder and clock:** `Set a timer for 30 seconds`, `Remind me in 30 seconds to check the oven`, `Turn off the light in 20 seconds`, `What time is it?`.
- **Part of the day:** `Is it day or night?` is answered according to the light sensor (a value simulated from the control center).
- **Conversation memory:** it remembers what was said earlier, for example your name.
- **English and Hebrew:** Supports multiple languages but the best accuracy by far is in English.
- **Works without the computer too:** The Web control center is there just to display all the information in a GUI and allow additional configurations (such as setting the WiFi and openai API key configurations, playing back up to 5 recording of your voice commands or the API's replies, etc).
- **Robustness:** Integrates automatic recovery mechanism - for example, if the WiFi or openai API connections stop, it will try and reconnect both of them automatically and notify the user upon success or failure using rising and falling beep sounds.
- **Control center in the browser:** health lights for each component and a Self-test, the conversation as bubbles with response times, all the settings (saved in flash on the ESP32 iteslf), statistics and a response-time graph with CSV export (with the date and the board's boot number), the recordings from the SD card with a player and a waveform, a typed question for a noisy room, and a display of raw serial logs including the ability to save them for debugging.

![Four voice commands in the control center](Documentation/images/demo.gif)

## Repository folders

| Folder | What is in it |
|---|---|
| `ESP32/` | The firmware: `ESP32.ino` (the flags, `setup()` and `loop()`) and its tabs: `state_machine.ino` (the state machine), `listen_turn.ino`, `server_events.ino`, `serial_commands.ino` and `verdicts.ino`, the `*_utils.cpp/.h` modules, `parameters.h` (all the fixed parameters), and `SECRETS.example.h` (a template for the secrets) |
| `control_center/` | The control center web GUI: `control_center.html` and `control_center.js` (the two files must stay in the same folder), which runs in Chrome or Edge |
| `Unit Tests/` | Unit tests for each hardware component (speaker, microphone, Wi-Fi, SD, LED, button, and the LDR once it is connected) and [their report](Unit%20Tests/README.md) |
| `Parameters/` | [The list of all the parameters](Parameters/parameters.md): what is changed from the control center, what is in `parameters.h`, and what is in `SECRETS.h` |
| `Documentation/` | All the documentation (list below), images, the measurement log, and `development_steps/` with all the steps we built along the way |
| `tools/` | Tools for the computer: a measurement summary from a log, a test of the voice detector on WAV files, a Serial logger, and an API diagnostic |

### The documentation

| Document                                                                                                                           | What is in it                                                                                                                                |
| ---------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| [Quick start guide](Documentation/quick-start.md)                                                                                  | One page: how to start it and what to try                                                                                                    |
| [System states and error messages](Documentation/system-states-and-errors.md)                                                      | What each state and each light means, and what to do                                                                                         |
| [Calibration instructions](Documentation/calibration.md)                                                                           | The light sensor, the Hands-free sensitivity, the microphone level                                                                           |
| [How the code is built and system's architecture](Documentation/architecture.md)                                                   | The modules, the state machine, the usage flow, and the protocol between the board and the control center                                    |
| [Performance analysis and 'hands-free' VAD (Voice activity detection) testing](Documentation/measurements/latency-vad-analysis.md) | Analysis of start-up latency, response latency and the robustness of the hands-free VAD algorithm (recorded experiment)                      |
| [Significant decisions](decisions.md)                                                                                | What we decided, why, and what it rests on (a measurement, an experiment or documentation)                                                   |
| [User stories](Documentation/user-stories.md) ([PDF](Documentation/user-stories.pdf))                                              | In the order of the demo                                                                                                                     |
| [Edge cases](Documentation/edge-cases.md) ([PDF](Documentation/edge-cases.pdf))                                                    | The original list of edge cases we tested                                                                                                    |
| [Edge cases - extended](Documentation/edge-cases-extended.md) ([PDF](Documentation/edge-cases-extended.pdf))                       | In the course format (indication and recovery), with the cases of the submission version. **To print: `edge-cases-extended.pdf` (2 copies)** |
| [Board test report](Documentation/board-tests-toSubmit.md)                                                                         | The tests of the submission version on the board (robustness, Wi-Fi, BOOT, status sounds), all passed                                        |

## Hardware

![The hardware](Documentation/images/hardware.png)

| Component                                        | Quantity              | Notes                                                                                                                                                                                     |
| ------------------------------------------------ | --------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ESP32-WROOM-32 DevKit                            | 1                     | 520KB SRAM, no PSRAM. The LED (GPIO 2) and the BOOT button (GPIO 0) on the board                                                                                                          |
| INMP441 microphone (I2S)                         | 1                     |                                                                                                                                                                                           |
| MAX98357A amplifier (I2S)                        | 1                     |                                                                                                                                                                                           |
| 4Ω 3W speaker                                    | 1                     |                                                                                                                                                                                           |
| MicroSD module (SPI) with a MicroSD card (FAT32) | 1                     | The recordings. Optional: without it the board works without recordings                                                                                                                   |
| LDR (light-dependent resistor)                   | 1                     | The light sensor. **Not connected:** the value is simulated from the control center - the code and the calibration for a real LDR are ready ([calibration](Documentation/calibration.md)) |
| Breadboard                                       | 1                     |                                                                                                                                                                                           |
| Jumper wires                                     | A set, about 20 wires | Microphone 6 (including L/R to GND), amplifier 5, speaker 2, SD module 6                                                                                                                  |
| USB cable                                        | 1                     | Power and connection to the computer (control center, flashing)                                                                                                                           |

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


## Software and versions

| What                                                 | Version                                                                                                         |
| ---------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| ESP32 SDK: ESP32 Arduino core (Espressif)            | 2.0.17 (ESP-IDF 4.4)                                                                                            |
| Arduino IDE                                          | 2.3.10                                                                                                          |
| WebSockets by Markus Sattler (Links2004)             | 2.7.2                                                                                                           |
| ArduinoJson by Benoit Blanchon                       | 7.4.3                                                                                                           |
| OpenAI model                                         | `gpt-realtime-2.1-mini` (voice to voice), and `gpt-4o-mini-transcribe` for the text shown in the control center |
| Control center                                       | Chrome or Edge (needs Web Serial)                                                                               |


## Installation and running

1. In the Arduino IDE, in the **Boards Manager**, install **esp32 by Espressif Systems** version **2.0.17**. Then install the two libraries from the **Library Manager**, at the versions in the table.
2. Copy `ESP32/SECRETS.example.h` to `ESP32/SECRETS.h` and fill in the Wi-Fi name and password (2.4GHz) and an OpenAI key. Without it, compilation stops with an explanation.
3. Open `ESP32/ESP32.ino` in the Arduino IDE, select **DOIT ESP32 DEVKIT V1** and upload.
4. Open `control_center/control_center.html` in Chrome, click **Connect board** and select the board's COM port (115200) - in the GUI you can set your desired Wi-Fi and openai API key configurations as needed.
5. Wait for the green lights and the opening greeting, then click **Talk** (or the physical BOOT button on board) or turn on **Hands-free** (can be done by a voice command as well).


## Known limitations

- The LDR is not connected: the value is simulated from the control center.
- In Hands-free mode, a TV or other people talking count as speech - there is no way to distinguish between the user's speech to speech from other sources given similar volume.
- Without internet the assistant does not answer questions (Offline). The timer, the controls and the recordings keep working.



This project is part of ICST - The Interdisciplinary Center for Smart Technologies, Taub Faculty of Computer Science, Technion. https://icst.cs.technion.ac.il/
