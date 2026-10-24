# Questions the Examiners May Ask - and Short Answers

Group A2, IoT course, Summer 2026. A preparation page for the team before the demo. All the numbers are from the [README](../README.md) and from [How the code is built](architecture.md).

## Hardware and memory

**Why not record the whole question and then send it?**

Our ESP32 does not have much RAM. We checked, and 3 seconds of audio at 24kHz and 16 bits is about 144KB, and when the connection to OpenAI is open only about 45KB stay free. So each 100ms chunk is sent right away.

**Why does the answer arrive as mu-law at 8kHz?**

It is 6 times smaller than PCM at 24kHz (64 kbps instead of 384), so the answer fits in the 32KB playback buffer. The quality is enough for speech.

**What takes up the most memory?**

The encrypted connection and the WebSocket, about 51KB. During a conversation about 45KB stay free, stable over 26 questions, which means there is no leak.

**How do the microphone and the speaker work together?**

Both are on the same I2S clocks (BCLK and LRC are connected to both), with one I2S port in full-duplex mode. See the [wiring diagram](images/wiring.png).

## Software

**What happens when the network is slow?**

A separate FreeRTOS task reads the microphone every 20ms into a queue of about 1.36 seconds. A slow send only delays the audio and does not lose it. We found this in the measurements: before the fix, samples were lost in 21 of 26 questions.

**How does the model turn on the LED?**

In the session configuration message we defined 6 "tools" with a JSON schema. The model decides to call a tool, the ESP32 carries it out and returns a result, and the model says out loud what was done. The code does not try to understand text on its own.

**How does Hands-free know that it is speech and not a knock?**

The voice detector compares the energy in the speech frequency band with the room noise, checks whether the sound is "voiced", and whether it has a syllable rhythm. Knocks, claps and footsteps never started a recording in the test on 112 noise clips.

**Why doesn't the assistant answer itself?**

During an answer and for 0.6 seconds after it the microphone does not listen (half-duplex). Talk is also rejected as long as the assistant is speaking (the button shows Busy): wait for it to finish, or click Cancel.

**What is kept after a Reset?**

Volume, mute, LED brightness, the light thresholds, the Hands-free settings, the Wi-Fi network saved from the control center, the BOOT button and the status sounds, in flash (NVS, through `Preferences`).

**How do you switch Wi-Fi networks without compiling?**

In the Wi-Fi network card in the control center. It sends `WIFI_SSID` and then `WIFI_PASSWORD`, the board saves both in flash and connects right away. The password is not printed in the log, and it is saved exactly as typed (including spaces at the ends). `SECRETS.h` is only the default: a saved network that fails twice in a row (for example a wrong password) is temporarily replaced by the network from `SECRETS.h`, and the saved network stays in flash.

**What happens without an SD card?**

The board works normally, just without recordings: a red SD light, and the recordings tab says there is no card. Before, the whole board stopped, and we changed that because the recordings are a feature and not a requirement.

**What happens if OpenAI stops answering in the middle?**

If there is no message at all from the server for 15 seconds during a question, the question fails and the board closes the connection and reopens it (the conversation context is lost). In addition, a heartbeat (a ping every 15 seconds) detects a "stuck" connection even with no open question: if 2 answers in a row do not come back within 10 seconds, the connection is closed and reopened.

**How is the assistant's state managed in the code?**

A few flags, one for each source of events (recording, a server answer, playback, a tool), and one function (`updatePipelineState()`) that computes a single state from them by a fixed order of priority. The table is in [architecture.md](architecture.md), and the reason for the choice is in [decisions.md](decisions.md).

**How do you transfer files from the SD card to the computer without an extra Wi-Fi connection?**

Over the same USB cable. The board sends the bytes between `@@GET_BEGIN` and `@@GET_END`, and the control center checks a CRC-32 to know that the file arrived complete.

## Performance

**How fast does it answer?**

A median of about 1.1 seconds from the end of the recording to the first sound. 23 of 26 questions started answering in under 2 seconds. The slow ones were quiet speech or speech from far away.

**How long does a boot take?**

6.8 to 10.8 seconds until it is ready to talk. The long part is the encrypted connection to OpenAI, 3.9 to 5.1 seconds.

## Limitations

**Why is the light sensor simulated?**

The light sensor (LDR) is not connected to the board, and as the course lecturer instructed, the value comes from the Light sensor slider in the control center (like the `LIGHT <value>` command). Everything else is real: the `get_time_of_day` tool, the evening and day thresholds that are saved in flash, and the spoken answer. Connecting a real sensor only requires wiring the voltage divider to pin 34 and uncommenting one line in `parameters.h`.

**Does it work in Hebrew?**

Yes, but less well than in English. You can speak Hebrew and it answers in Hebrew, but the answers are less accurate, and the transcript shown in the control center (what the user said) is often garbled. The transcription is done by a separate model (`gpt-4o-mini-transcribe`) that is not told the language. Setting the transcription language in the session configuration is the next improvement we would test.

**What happens without internet?**

The assistant goes to Offline mode: the circle in the control center is red, and with the status sounds you hear two falling tones. It keeps trying to connect every 30 seconds, and never stops (the OpenAI light is red during this time, also when there is Wi-Fi without internet). Meanwhile the timer, the controls (volume, LED, Hands-free) and the recordings work, because they run on the board. When the Wi-Fi comes back, the connection to OpenAI reopens within a few seconds (the OpenAI light is orange until then), and if it still does not answer after 10 seconds the board goes back to retrying every 30 seconds. All of this without a Reset, and it is usually back to Ready within about 15 seconds.

**Can you use it without the computer?**

Yes. Hands-free, the BOOT button (as the Talk button) and the status sounds are saved on the board, and a power bank is enough. Disconnect the USB cable from the computer and only then connect a power bank (not both together, so as not to damage the computer). The board restarts and reaches the opening greeting within about 10 seconds, and the settings are kept. The computer provides the screen and the settings.

**Why did the BOOT button come back, after you gave up on it?**

In step 19 it gave false readings. During the submission stage we found a likely cause: in the I2S configuration the MCLK pin field was not initialized, meaning it was 0, and the driver outputs that clock on GPIO0, the button's pin. We removed the MCLK pin (none of our components need it). The unit test `08_boot_button` checks this directly, and it passed on the board. The button is off by default, and you turn it on in the On the board itself card.

**What would you improve?**

We thought about moving the GUI to a phone app instead of the computer.
