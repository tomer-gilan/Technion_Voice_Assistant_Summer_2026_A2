# Significant Decisions and Discoveries Along the Way

Group A2, IoT course, Summer 2026. This document records the significant decisions in the project: what we decided, why, which alternatives we considered, and what the decision rests on (a measurement, an experiment or official documentation). Comments in the code refer to this document by section name.

Legend for "Rests on": **measured** (there are numbers in the log), **tried** (we tried it on the board and saw the result), **documentation** (according to the manufacturer's documentation), **estimate** (a reasonable assumption that was not fully verified).

Decisions that rest on documentation also have a **Sources:** line with links to the pages they rest on (checked on 23.10.2026).

## Architecture

### Speech understanding in the cloud, everything else on the microcontroller

**The decision:** speech recognition, understanding and the voice of the answer run in the OpenAI Realtime API. The ESP32 records, streams, plays, runs the tools, and decides by itself when someone is speaking (the voice detector).

**Why:** an ESP32 without PSRAM (520KB) cannot run a language model or general speech recognition. Whatever can be done on the microcontroller, we did on it: the voice detector, the timer, the clock, the light sensor (whose value is simulated from the control center, as the course guide instructs), the recordings and the hardware control. That way the timer and the controls keep working even without internet.

**Rests on:** documentation (the ESP32's limitations), measured (the free memory during a conversation is about 45KB).

**Sources:** [ESP32-WROOM-32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf) (520KB SRAM, and no PSRAM in the module's feature list); [OpenAI, Realtime API: conversations](https://developers.openai.com/api/docs/guides/realtime-conversations).

### From Gemini Live to OpenAI Realtime (steps 6-9)

**The decision:** we started with Gemini Live (steps 6-8b; the connection, the setup and sending audio worked) and moved to OpenAI Realtime in step 9.

**Why:** in step 8a we found that the Gemini audio models that were available answer only in audio and do not return the text of the answer, while OpenAI returns both audio and a transcript of both sides. We use the transcript for the control center and for checking the results. OpenAI also has a well-documented tool mechanism (function calling) on the same connection. *(The team's other considerations, such as cost and availability, were not written down at the time.)*

**Rests on:** tried (step 8a).

**Sources:** [Gemini Live API](https://ai.google.dev/gemini-api/docs/live-guide) (the native audio models support answering in audio only; the current documentation also mentions a transcript of the audio output, `output_audio_transcription`, and we did not check whether it was available at step 8a); [OpenAI, function calling in the Realtime API](https://developers.openai.com/api/docs/guides/realtime-function-calling).

### One encrypted WebSocket connection, and sending in 100ms chunks

**The decision:** all the microphone audio is sent right away, in 100ms chunks (2400 samples), on the same connection. A whole recording is never kept in memory.

**Why:** 3 seconds of PCM at 24kHz are about 144KB, and when the encrypted connection is open about 45KB are left. Sending in chunks is the only way it fits. 24kHz is the lowest input rate that OpenAI accepts for PCM (step 10).

**Rests on:** measured (`@@HEAP`), documentation.

**Sources:** [OpenAI, Realtime API: conversations](https://developers.openai.com/api/docs/guides/realtime-conversations) (the `audio/pcm` input is defined at 24kHz).

### The answer in 8kHz mu-law

**The decision:** we asked OpenAI for the answer in G.711 mu-law at 8kHz, and not PCM at 24kHz.

**Why:** one byte per sample and 8000 samples per second, 6 times less than PCM at 24kHz (64 instead of 384 kbps). That way a 32KB buffer holds about 4 seconds of answer. The quality is enough for speech. Decoding to PCM and upsampling to 24kHz happen only at playback time.

**Rests on:** measured (the buffer size, the memory), tried (the sound quality).

**Sources:** [OpenAI, Realtime API: conversations](https://developers.openai.com/api/docs/guides/realtime-conversations) (the `audio/pcmu` format appears as an output format in the `session.update` example).

### Shared I2S port, not two independent ones

**The decision:** the microphone (INMP441) and the amplifier (MAX98357A) are connected to the same clock lines (BCLK on pin 26, WS/LRC on pin 25), and one I2S port works in full duplex mode.

**Why:** two ports that are each a master would compete for the same pins. One port with RX and TX together saves pins and guarantees that the clocks are identical. Tested in step 4 (loopback).

**Rests on:** tried (step 4).

### No MCLK pin (submission stage)

**The decision:** in `i2s_pin_config_t` we explicitly set `mck_io_num = I2S_PIN_NO_CHANGE`.

**Why:** the field was not initialized, meaning it was 0. The ESP32 driver outputs the MCLK clock on one of pins 0, 1 or 3, and 0 is GPIO0, the BOOT button's pin. None of our components need MCLK. This is probably also the explanation for the unexplained readings from the BOOT button in steps 19-20.

**Rests on:** documentation (`driver/i2s.h`, the error message in the driver library), and tried on the board: the unit test `Unit Tests/08_boot_button` passed (0 raw changes when the button is not touched, and PASSED after 5 presses). With `kStartI2sWithMclkOnGpio0 = true` it imitates the old configuration for comparison (see [board-tests-toSubmit.md](board-tests-toSubmit.md)).

**Sources:** [`driver/i2s.h` in ESP-IDF 4.4](https://github.com/espressif/esp-idf/blob/release/v4.4/components/driver/include/driver/i2s.h) (the comment on `mck_io_num`: on the ESP32, MCK can be output only on GPIO0, GPIO1 or GPIO3); [ESP-IDF 4.4 I2S documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/i2s.html) (`i2s_pin_config_t` and `I2S_PIN_NO_CHANGE`); [ESP32-WROOM-32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf) (GPIO0 is also the strapping pin that sets the boot mode).

## Memory

### Ring buffer is heap-allocated, not static

**The decision:** the answer's buffer (ring buffer) is allocated once in `setup()`, before the Wi-Fi and TLS, and not as a static array.

**Why:** when we enlarged it to 64KB as a static array, the linker failed: the ESP32's static budget is much smaller than the heap. Allocating early also guarantees a contiguous block, before TLS fragments the memory.

**Rests on:** tried.

### A 32KB buffer, not 64KB (step 24)

**The decision:** we shrank the playback buffer from 64KB to 32KB when we added the SD card.

**Why:** with the SD card and 64KB, only about 9KB were left free when the session was ready, and the first question used up the heap. We also tried 48KB. We chose 32KB to leave room for the next features, and it turned out to be enough, because what paces the answer is the backpressure (the next section) and not the buffer size.

**Rests on:** measured (`@@HEAP` in step 24).

### Backpressure, not a bigger buffer

**The decision:** when the playback buffer is almost full, the board stops reading from the WebSocket, and continues when it empties (two different thresholds, so it does not flicker).

**Why:** a long answer arrives from the server faster than the rate at which it plays. When the socket is not read, TCP flow control stops the server. That way an answer of any length plays with a small buffer.

**Rests on:** tried (steps 13-14).

### "Audio blew the heap, again"

**The decision:** decoding the audio's base64 is done in fixed slices of 1024 characters straight into the buffer. There is no allocation whose size depends on the message length. Audio events are scanned without full JSON parsing (`realtime_json_utils`).

**Why:** an audio message from the server can be tens of KB. Full JSON parsing or a buffer the size of the message failed twice on out of memory.

**Rests on:** tried.

### Not parsing `session.created` / `session.updated`

**The decision:** these events are identified by name only, without JSON parsing.

**Why:** in them the server returns all the settings, about 6KB. Full parsing needed a few more KB, and once it failed on out of memory and left the OpenAI light stuck on "connecting". We do not need any field from them.

**Rests on:** tried on the board (18.10).

### "X509 - Certificate verification failed" (step 24)

In step 24 we saw a certificate verification error when the heap was tight. **The hypothesis** (not verified): an allocation that fails inside the TLS handshake comes out as an error that looks unrelated. So `heap_diagnostics_utils` counts every failed allocation.

## The protocol with OpenAI

### Not sending from inside the WebSocket callback ("Discoveries", step 7)

**What we saw:** in step 7, sending `session.update` directly from inside the `CONNECTED` event froze the WebSockets library.

**The decision:** messages that are sent in response to the connection (the session setup, the opening greeting) are sent from the next iteration of `loop()`.

**An honest note:** the tool answers (`function_call_output`) and the `response.create` after them are sent from inside the `TEXT` event, and that worked in all the documented runs (including the 26 questions of the measurement). So the problem was probably specific to sending during the connection itself. Moving these sends to `loop()` as well is a safer change, but it touches the core that was tested, so it stayed as it is.

**Rests on:** tried.

### A silence timeout, not a length timeout (step 16)

**What we saw:** a limit on the total length of an answer ("15 seconds from the start of the question") cut long answers off in the middle, while the audio was still arriving. This is the "reply timeout fired mid-reply" discovery.

**The decision:** the question fails only after 15 seconds without *any* message from the server. Every message resets the clock. When it fails, the board also closes the connection and reopens it, because a server that is silent for so long may be behind a dead connection (Wi-Fi without internet). The price: the conversation context is lost.

**Rests on:** tried (step 16).

### An opening greeting without a tool (step 22)

**The decision:** the opening greeting is a `response.create` with one-time instructions ("say exactly this"), not a tool. The same mechanism is used for the message when the timer ends.

**Rests on:** documentation (OpenAI, verified against the documentation on 21.9.2026).

**Sources:** [OpenAI, Realtime API: conversations](https://developers.openai.com/api/docs/guides/realtime-conversations) (`response.create` starts a response from the model).

### `turn_detection: null`: the voice detector on the microcontroller and not on the server

**The decision:** we turned off the server's end-of-speech detection, and the board decides when to start and stop recording (`vad_utils`), and sends `commit` by itself.

**Why:** with server-side detection we would have had to stream audio all the time, including silence and noise. That wastes network and money, and the board would have been limited in its ability to discard noises. On the microcontroller we also know exactly when the recording started, and keep 0.5 seconds before it (pre-roll) so the first word is not cut off.

**Rests on:** measured (112 noise clips and 100 speech clips with `tools/vad_replay.cpp`). **Not measured** with our microphone in the room.

**Sources:** [OpenAI, Realtime API: conversations](https://developers.openai.com/api/docs/guides/realtime-conversations) (turning off the server VAD with `turn_detection: null`, and then the client sends `input_audio_buffer.commit` and `response.create` by itself).

### Hands-free and not only push-to-talk, and what happens in a noisy room

**The decision:** Hands-free (the voice detector on the microcontroller) is an option and is off by default. Talk, the BOOT button (when turned on) and a typed question (Ask) always remain, so you never have to speak "freely".

**The assumption:** that Hands-free works best in a relatively quiet room. In a noisy room the detector may take noise for speech, and miss quiet speech.

**What we tested:** on the computer, with `tools/vad_replay.cpp` on 100 speech clips and 112 noise clips ([README](../README.md), "The voice detector of Hands-free mode"): in all 100 speech clips the recording started and the first word was not cut off; in very quiet speech against background noise, 7 of 27 clips (26%) lost 20 to 200ms from the start; 20 of 112 noise clips (17.9%) started a recording (coughing, laughter, a creaking door, bells and running water); knocks, claps, footsteps, rain and a vacuum cleaner never started one. On the board: 5 questions by voice and knocks on the table, which did not start a recording (`HANDS-FREE PASSED`, see [board-tests-toSubmit.md](board-tests-toSubmit.md)).

**What we did not test:** speech in the background (a TV, other people), and the demo room with our microphone. Such speech counts as speech, so it also appears in the known limitations in the README.

**If the room is noisy:** (1) Lower the Sensitivity. (2) Turn off Hands-free and switch to Talk (3 seconds of recording). (3) Type the question in the Ask field and the assistant answers out loud as usual. After 3 noises in a row that were not speech, the detector also stops listening for 5 seconds. This order also appears in the [demo plan](demo-plan.md) and in the [calibration](calibration.md).

**Rests on:** measured (on the computer), tried (on the board), estimate (the demo room).

### Switching between Hands-free and push-to-talk by voice too, and only on an explicit request

**The decision:** a seventh tool, `set_hands_free`, does exactly what the Hands-free switch in the control center does: the same function (`setHandsFree()`) that runs `VAD on|off`, it is saved in flash, and the switch in the control center moves with it (`@@VAD`). The tool takes the requested state (`on: true|false`) and not "toggle", so a request heard twice does not go back to the previous state.

**Why this way, and why carefully:** a switch nobody asked for is confusing: Hands-free that turns on by itself starts listening to the room, and Hands-free that turns off by itself stops answering whoever is talking to it. So there are three layers: (1) The instructions for the model: call the tool only when this message explicitly asks to change the listening mode, with examples ("Switch to hands-free mode", "I want to talk without pressing a button", "Disable VAD"), and never on a question about the mode ("What is hands-free?"), a passing mention of hands, a button or voice, unclear audio, or an earlier request. If the model is not completely sure, it does not call the tool and asks. (2) The tool's description repeats the rule. (3) In the firmware: the tool is accepted only while answering a user's request (a spoken or typed question), not during the opening greeting or a timer message, and a request for the mode that is already active changes nothing.

**Rests on:** estimate. The instructions are built like the `set_board_state` rule against tool calls nobody asked for (step 21). It has not yet been tested on the board how often the model calls the tool without being asked.

### The microphone does not listen while the control center plays a recording

**The problem:** the recordings player plays on the computer's speakers. With Hands-free on, the board's microphone heard the recording (an answer or a voice command from the SD card) and treated it as a question.

**The decision:** the control center sends `LAPTOP_PLAYBACK on <ms>` when the player starts, resumes, seeks or changes speed (ms is the time left, plus 1.5 seconds), and `LAPTOP_PLAYBACK off` when it stops, ends or is replaced. During this time Hands-free does not start listening, and the wait afterwards (600ms, as after the board itself speaks) is counted from the end of the playback. A Hands-free sentence that was already being recorded when the playback started is canceled, like Cancel, before it becomes a question. A question the user asked for (Talk, BOOT, `LISTEN`) is allowed: the control center stops the player when it starts, and does not let the player start while it is being recorded.

**Why this way:** the board does not know how much of the recording is left, so the time comes from the page. If the page is closed in the middle of playback the `off` will not arrive, so the pause ends on its own at the end of that time (5 minutes at most), and Hands-free does not get stuck off. A board that restarted in the middle of playback gets the `on` again with its first `@@ALIVE`.

**Rests on:** estimate. Not yet tested on the board.

## Code

### Log levels, and a namespace for everything (step 17)

**What we saw:** a global variable named `g_log_level` changed by itself while connecting to Wi-Fi, without any of our code writing to it. **The likely explanation:** a name collision with an internal variable in one of the closed-source libraries (WiFi, lwIP or ESP-IDF). When a namespace gave the variable a different name at the linker level, the problem disappeared.

**The decision:** every symbol in the project is inside a namespace, except `setup()` and `loop()`, which Arduino calls. In the submission stage we closed the last modules that were still outside (`esp32va_audio`, `esp32va_json`, `esp32va_board`).

**Rests on:** tried (step 17, a full bisection).

### Each module is a single object (a namespace), not a class

**The decision:** each hardware component and code area is an `esp32va_*` module that is a single object: its internal state (variables, buffers, flags) is in an anonymous `namespace` in the `.cpp` file, meaning "private members", and the `.h` file is the public interface, meaning the "class". There is no `class` in the board's code (there are only `enum class`es).

**Why not a C++ class:**
- Each component has one instance on the board (one LED, one I2S port, one SD card, one button). A class that can be instantiated many times would not have added anything.
- No dynamic allocation: a module is static, and there are no objects created and destroyed on the heap. On a board without PSRAM we avoid such allocations (see "Audio blew the heap, again").
- Timer, task or interrupt callbacks need a plain function: the `esp_timer` callback in `button_utils` and the FreeRTOS task of `mic_capture_utils` take a function pointer, and a class method would have needed a static wrapper anyway.
- The ESP-IDF and Arduino APIs (`i2s_*`, `ledc*`, `analogRead`, FreeRTOS) are C, and free functions fit into them without an extra layer.
- The namespace is what solved the real bug we ran into, the `g_log_level` collision at the linker level (see "Log levels, and a namespace for everything").

The rule is not perfect: a few variables are declared in a header on purpose (`g_light_sensor_value` in `light_sensor_utils.h`, and `g_log_level` in `log_utils.h`). The others are internal.

**Which hardware belongs to which module:**

| Hardware | The module | The namespace |
|---|---|---|
| The LED on the board (GPIO 2, brightness via PWM) | `board_state_utils` | `esp32va_board` |
| Speaker + MAX98357A amplifier and INMP441 microphone (one I2S port) | `i2s_audio_utils`. Reading the microphone in a separate task: `mic_capture_utils`. The voice detector: `vad_utils` | `esp32va_audio`, `esp32va_mic_capture`, `esp32va_vad` |
| Volume, mute and beep | `device_controls_utils` | `esp32va_controls` |
| BOOT button (GPIO 0) | `button_utils` | `esp32va_button` |
| LDR light sensor (simulated from the control center; the ADC reading is ready) | `light_sensor_utils` | `esp32va_light_sensor` |
| SD card (SPI) | `sd_card_utils`. Saving and transferring the recordings: `reply_recorder_utils`, `voice_command_recorder_utils`, `sd_file_transfer_utils` | `esp32va_sd_card`, `esp32va_reply_recorder`, `esp32va_command_recorder`, `esp32va_sd_transfer` |
| Wi-Fi and the clock | `wifi_utils` | `esp32va_wifi` |
| Non-volatile memory (NVS) | `settings_store_utils` | `esp32va_settings` |

**Rests on:** tried (the linker collision, step 17), and estimate (the other reasons are design considerations and were not measured).

### The state machine: flags, and one computed state

**The decision:** the conversation logic keeps a few flags (`is_capturing`, `awaiting_reply`, `response_in_flight`, `speech_in_progress` and more), and `updatePipelineState()` computes from them, on every iteration, one defined state (`@@STATE`). The transitions are described in [architecture.md](architecture.md).

**Why, and not an enum with transitions:** the events arrive from three sources in parallel (the server, the microphone task, the playback), and an answer can be in the middle of playback when a new event already arrives. Separate flags for each source fit this better. In the submission stage we did **not** rewrite it, because this is the core that was tested on the board in many runs. Instead we documented the table that maps flags to states.

### A separate task for the microphone (step 30)

**What we measured:** in 21 of 26 questions, microphone samples were lost. The DMA buffer holds 150ms, and a single send over Wi-Fi took up to 465ms. The amount of lost audio matched the slowest send in that question (correlation coefficient 0.93).

**The decision:** a FreeRTOS task at a higher priority reads the microphone every 20ms into the playback buffer, which is lent to it as long as there is nothing to play. A slow send only delays the audio.

**Rests on:** measured (before the fix). **A repeat measurement after the fix is still missing.**

## Hardware and network

### The BOOT button: given up (steps 19-20), and back as an option (submission stage)

In step 19 the BOOT button started a recording, but it gave false readings. The debounce was replaced with a saturating counter (step 20b), the investigation did not finish, and we gave up on the button. In the submission stage we found a likely cause: the MCLK clock went out on this pin (see "No MCLK pin"). The button came back, **off by default**, with the same debounce, and it is turned on from the control center only after a test on the board (`Unit Tests/08_boot_button`).

### The light sensor is simulated from the control center

**The decision:** the LDR is not connected to the board. The light value comes from the control center (the `LIGHT <0-6142>` command), as the course guide instructs.

**What is real:** the `get_time_of_day` tool that the model calls, the thresholds (`LIGHT_BANDS`) that are saved in NVS, and the mapping of the value to day, evening or night. The code for a real LDR is also already written: define `#define LIGHT_SENSOR_PIN 34` in `parameters.h`, and the board reads the sensor through the ADC every second (an average of several samples, times 1.5) and ignores the `LIGHT` command. The unit test `Unit Tests/09_light_sensor` is ready for when the sensor is connected (see [calibration.md](calibration.md)).

**How we demo it:** the control center has a manual slider, to change the light on the spot and see right away how the assistant answers. Next to it is an Auto day-cycle mode: a whole simulated day (night, sunrise, day, sunset) in two minutes, a new `LIGHT` value every 1.5 seconds, on a smooth curve with a small ADC-like noise, so the data resembles readings from a real LDR. Touching the slider returns to manual mode. This mode is in the control center only, with no change to the firmware.

### A hotspot and not the Technion Wi-Fi

**The decision:** we demo through a hotspot from a phone and not on the Technion network. In the submission stage the network can be changed from the control center without recompiling (`WIFI_SSID` / `WIFI_PASSWORD`, saved in flash).

**What is known and what is an assumption:** the board could not connect to the Technion network. We did not fully investigate the reason, and it is **an estimate and not a fact**: the network probably requires enterprise login (WPA2-Enterprise) or a sign-in page (captive portal), and `WiFi.begin()` with a name and password handles neither of them. According to the documentation, the ESP32 itself is capable of WPA2-Enterprise, so this is not necessarily a hardware limitation.

**Rests on:** tried (the connection failed), estimate (the reason).

**Sources:** [ESP-IDF 4.4 Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-guides/wifi.html) (mentions support for WPA2-Enterprise, which authenticates against a RADIUS server).

### A control center over USB (Web Serial) and not a server on the microcontroller

**The decision:** the control center is a single HTML page that talks to the board over a USB cable (Web Serial), and not an HTTP server on the ESP32 or an app.

**Why:** a web server on the microcontroller needs more memory while the encrypted connection is open, and only about 45KB are left then. Web Serial needs nothing from the board except printing `@@` lines. The board itself also works without the computer: Hands-free is saved in flash, and there are the BOOT button and status sounds as options.

**The downside:** without a cable there is no screen. "Debug mode without a computer" remains partial (the status sounds).

**Sources:** [Chrome, Web Serial API](https://developer.chrome.com/docs/capabilities/serial) (a page that can read from and write to a serial device).

## Robustness (submission stage)

| The decision | Why |
|---|---|
| Without an SD card the board continues, without recordings | The recordings are a feature and not a requirement. Before, the whole board stopped |
| The connection to OpenAI is never abandoned: after 5 attempts it is "offline", with an attempt every 30 seconds | Before, the board stopped until a Reset |
| When the Wi-Fi comes back (or the network was changed), the wait between attempts resets and OpenAI reconnects within seconds. After 10 seconds without success it goes back to the 30-second pace | A wait of up to 30 seconds after the network was already back looked like a fault |
| A heartbeat: a ping every 15 seconds, and the connection is closed after 2 answers (pongs) that did not come back within 10 seconds | On Wi-Fi without internet the connection looked open, and a question just failed without the board realizing it was offline |
| A network saved from the control center that fails twice in a row is replaced (in memory only) by the network from `SECRETS.h`, and back the same way | A wrong password left the board offline until someone noticed. The saved network stays in flash, so a temporary switch does not delete it |
| The attempt counter resets on every successful connection | A bug: 5 disconnects scattered over hours (each of which reconnected) stopped the board |
| `setup()` does not wait forever for the Wi-Fi or the clock (20 / 15 seconds) | Before, without a network, the board did not reach `loop()` and did not answer any command |
| A Wi-Fi network is saved in flash from the control center | A course requirement: settings without recompiling |
| The BOOT button and status sounds are off by default | They are new, optional features. A board where they were not turned on behaves exactly as before |

Each of these changes is a separate commit in the git history, so each one can be reverted on its own.

### Downloading a recording stops `loop()` on purpose

**The decision:** the `GET <name>` command sends the whole file over Serial within one iteration of `loop()`, and the board does nothing else until the transfer ends.

**Why:** the file's bytes go over the same channel as the log lines and the `@@` lines. Any print in the middle of the transfer would corrupt the file (and the CRC would fail), so nothing gets mixed into it.

**How long:** at 115200 baud about 11.5KB pass per second: about 3 seconds for an answer from the assistant, and up to about 40 seconds for a 10-second Hands-free voice command (about 480KB of PCM at 24kHz).

**What we do because of it:** Hands-free is paused before the download starts, and the control center locks Talk and Ask and shows "Downloading…" until it ends, so a click does not wait in a queue and start a recording only tens of seconds later.

**Considered and not done:** a non-blocking transfer, a few bytes per iteration of `loop()`, in which all the other prints are blocked as long as the transfer is open. That is a change to the core that was tested on the board, so we chose locking the interface and documenting it.

**Rests on:** estimate (a calculation from the baud rate and the file sizes; we did not measure the actual download time).

### No queue of questions to send later (offline)

**The decision:** a question asked when there is no connection (offline) is not saved to be sent later. The board rejects it right away and says why (`LISTEN ignored - offline`), and the control center shows `offline` and a red OpenAI light.

**Why:** a spoken answer that arrives minutes after the question, out of context, would confuse more than it helps. An immediate, visible refusal is clear to the user.

**What is saved, even when there is no network:** the answers and the voice commands on the SD card (the last 5 of each kind, which can be downloaded later), the settings in flash (`Preferences`, stored in NVS), and the log and the statistics in the control center (a log file and a CSV on the computer). The timer and the controls keep working offline.

**Rests on:** estimate (a design decision); the behavior itself was tested on the board in the offline edge cases ([edge-cases-extended.md](edge-cases-extended.md)).

**Sources:** [Arduino-ESP32, Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html) (storing key-value pairs in NVS).

## Settings: who sets what

### The OpenAI key is also set from the control center, like the Wi-Fi network

**The decision:** the key in `ESP32/SECRETS.h` is the default, and a different key can be saved from the OpenAI API key card in the control center (`OPENAI_KEY <key>`), without compiling. It is saved in flash (NVS) and stays after a Reset, and the board reconnects with it right away. Exactly as with the Wi-Fi network: if OpenAI refuses the saved key twice in a row (HTTP 401 or 403 when opening the WebSocket), the board switches to the key from `SECRETS.h`, and back the same way, in memory only. `OPENAI_KEY_FORGET` (Back to default) restores the key from `SECRETS.h`.

**Why:** we had first decided that the key belongs to the manufacturer and not to the user, so it was compiled into the firmware only. The price turned out to be high: a key that expired or was revoked, or a switch to another account, required compiling and uploading, and a board without a valid key just looked "offline". Now the board also tells a refused key apart from a network that does not answer: the OpenAI light says `API key refused (HTTP 401)`. The key is never printed, neither in the board's log nor in the control center's log: only `sk-...` and the last 4 characters (`@@OPENAI_KEY`). The board checks the format (`sk-`, only letters, digits, `-` and `_`, 20-200 long) before saving, so a paste with a space or a line break does not reach the HTTP header.

**Considered and not done:** checking the key against OpenAI before saving. That requires a second TLS connection in parallel, and there is no memory for it on a board without PSRAM (see "No Bluetooth and no access point"). Instead we connect with it, and if it is refused twice we go back to the previous key that worked.

**Rests on:** estimate (a design decision), documentation (the WebSockets library 2.7.2 passes the HTTP code of a refused connection in the `WStype_DISCONNECTED` event). Not yet tested on the board.

### No Bluetooth and no access point (AP) for setting up the board

**The decision:** the settings, mainly the Wi-Fi network, are sent from the computer over a USB cable (Web Serial), and not through Bluetooth or through an access point on the board with a setup page.

**Why:** the Bluetooth stack and a page server on an access point consume memory (heap), and the encrypted connection to OpenAI already takes most of what there is on a board without PSRAM: during a conversation about 45KB are left, and the measured minimum was 27KB. We did not measure exactly how much they would cost, and we did not try to add them. Web Serial needs nothing from the board except printing `@@` lines (see "A control center over USB").

**Rests on:** measured (the free memory during a conversation), estimate (the cost of the Bluetooth and the AP).

### No separation between User and Admin

**The decision:** there are no separate screens for a user and an admin. There is one customer, and they set everything on the website, meaning in the control center. Whoever "administers" the system changes things by recompiling (`ESP32/parameters.h`, `ESP32/SECRETS.h`).

**Why:** such an admin is not a customer, so we neither offer nor support customization for them through an interface. Everything a customer needs (Wi-Fi, the API key, volume, Hands-free and its sensitivity, the light thresholds, the BOOT button) is in the control center. The developer's parameters are concentrated in `parameters.h` and documented in [Parameters/parameters.md](../Parameters/parameters.md).

**Rests on:** estimate (a design decision).

### The microphone gain, the voice and the recording length are fixed in the code

**The decision:** `kMicSoftwareGain`, `OPENAI_VOICE` and `kManualRecordDurationSec` stayed fixed in `parameters.h` and were not exposed as a setting in the control center.

**Why:** for settings related to recording, we preferred fixed values that were tested and are stable over values that can be changed. The ×2 gain was tested (a median of -19 dBFS, clipping in 1 question of 26), and ×4 caused clipping in normal speech. All the measurements were made with a 3-second recording. What can be tuned from the control center is the Hands-free sensitivity and the silence that ends a sentence, within the range the firmware allows. As for the voice: according to the documentation it cannot be changed after the model has already played audio in the same session.

**Rests on:** measured (the microphone level and the clipping), tried (×4 gain), documentation (the voice). See also [calibration.md](calibration.md).

**Sources:** [OpenAI, Realtime API: conversations](https://developers.openai.com/api/docs/guides/realtime-conversations) (choosing the `voice`, and that it cannot be changed after the model has played audio in the session).

## Deliberate limitations

### No OTA, no wireless debugging and no Home Assistant / MQTT

**The decision:** we did not implement wireless firmware updates (OTA), wireless debugging, or a connection to Home Assistant / MQTT. These are deliberate limitations and not things we forgot.

**Why:** each of them needs another socket or open service (an OTA listener, a network log port, a connection to an MQTT broker) and more memory, on top of the encrypted connection to OpenAI, on a board without PSRAM that has about 45KB left during a conversation. We did not try to add them, so the reason is an estimate and not a measurement.

**What there is instead:** the board works without a computer and without internet (the timer and the controls keep working, see "Robustness"). A run can be analyzed after the fact: the control center saves a log, and **Replay a log** plays it again without the board. The recordings of the answers and the commands are saved on the SD card and downloaded later. Firmware updates and debugging are done over the cable.

**Rests on:** estimate.

## Cost and use of a paid API

### The cost of the OpenAI Realtime API

**The decision:** the assistant runs on `gpt-realtime-2.1-mini` (the `OPENAI_MODEL` constant in `ESP32/parameters.h`), the cheaper of the two models in the series (the other is `gpt-realtime-2.1`). The transcript shown in the control center is made by a separate model, `gpt-4o-mini-transcribe`, which is billed separately. The API is paid per token.

**What sets the cost:** how much audio goes in and out in each question. Audio that the user says is counted as one token per 100ms (about 10 per second), and audio that the model answers as one token per 50ms (about 20 per second). With every answer the whole conversation so far is sent to the model, so later questions in a conversation cost more (part of the input is counted as cached input, which is cheaper). Each of our questions sends about 193KB of PCM at 24kHz (about 4 seconds of audio, see [README](../README.md), "Performance measurements"), and the answer comes back as mu-law at 8kHz, 64 kbps (the instructions for the model ask for one short sentence).

**The prices** (from the official pricing page, as of 23.10.2026, per million tokens): `gpt-realtime-2.1-mini`, audio: input $10, output $20, cached input $0.30; text: input $0.60, output $2.40. `gpt-4o-mini-transcribe`: input $1.25, output $5 (about $0.003 per minute according to the page).

**How to compute a question:** (input tokens × input price + output tokens × output price) / 1,000,000, where the input tokens include the audio, the instructions and the tools and the conversation so far, plus the cost of the transcription. The server returns the exact numbers in the `usage` field of the `response.done` event. The board does not read it today, and the length of the answer in tokens was not recorded, so **we did not write a price per question here**: any number would be a guess.

**Rests on:** documentation (the prices and the token counting), measured (the volume of audio sent per question).

**Sources:** [OpenAI, pricing](https://developers.openai.com/api/docs/pricing); [OpenAI, costs in the Realtime API](https://developers.openai.com/api/docs/guides/realtime-costs) (the token counting for audio, and that the whole conversation is sent with every answer).

## Security

The API key is stored in the firmware (`SECRETS.h`, not in git), and if a key was saved from the control center, also in NVS, and it is sent in the connection header. Anyone who holds the board can read it from the flash. It does not appear in the log: the board prints only `sk-...` and the last 4 characters, and the control center records `OPENAI_KEY ********` (and the same for a Wi-Fi password typed on the command line). In a real product the board would get a temporary key (ephemeral token) from our own server. According to the course guidelines security is not a priority, so this is documented as a known limitation. The board verifies the certificate of `api.openai.com` against a single root certificate (GTS Root R4). If OpenAI changes its chain, it needs to be updated in `openai_realtime_utils.cpp`.

## Language

**What was tried:** you can talk to the assistant in Hebrew, and it answers in Hebrew, but the quality of the answers is lower than in English. The transcript shown in the control center is often garbled in Hebrew.

**The decision:** the instructions for the model and the examples are written in English, and in the demo we speak English. We did not lock the language, so Hebrew still works.

**What was not tested (a possible improvement):** setting a language (`language: "he"`) for the transcription model in the session configuration, and an instruction to the model to answer in the language it was addressed in. Both changes are small, but they need testing on the board.
