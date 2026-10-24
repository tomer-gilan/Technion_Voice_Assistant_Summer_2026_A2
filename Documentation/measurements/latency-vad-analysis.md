## Performance measurements

### How we measured

We added measurement points to the firmware (`measurement_utils`). Every question prints a `@@TIMING` line, and every boot prints a `@@BOOT_TIMING` line, with times from `millis()`, sizes, microphone levels and free memory. We saved the log from the control center and ran `tools/measurements_report.py` on it.

Test conditions (15.10.2026): a hotspot from a phone, RSSI at boot between -37 and -45 dBm and a median of -48 during the questions (range -38 to -62), a quiet room, 26 questions and 3 boots. Questions 1-19 were normal speech from about half a meter, 20-22 quiet, 23-24 from far away (about 2 meters), 25 close, 26 a whisper. Push-to-talk mode (3 seconds). All the times in a question are measured from the end of the recording. **The firmware in this measurement is step 29's, before the microphone task of step 30.** The full log: `Documentation/measurements/esp32_log_2026-10-15.txt`.

### Results

![Response time](../images/response-time.png)

| What | Result |
|---|---|
| Questions that finished successfully | 26 of 26 |
| From the end of the recording to the first message from the server (median) | About 0.3 seconds |
| From the end of the recording to the first sound of the answer (median) | About 1.1 seconds |
| Questions where the answer started in under 2 seconds | 23 of 26 (the slow ones, about 5 seconds, were quiet speech and speech from far away) |
| From the request `Turn on the light` until the LED turned on | 0.97 seconds (the spoken confirmation started after 1.9 seconds) |
| Full boot (until ready to talk) | 6.8 / 9.1 / 10.8 seconds |
| The longest stage of the boot | The encrypted connection to OpenAI, 3.9 to 5.1 seconds |

![The statistics panel](../images/statistics.png)

![Boot and memory](../images/boot-and-memory.png)

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


### The voice detector of Hands-free mode

The voice detector (`vad_utils`) compares the energy in the speech frequency band with the room noise, and also checks whether the sound is "voiced" and whether it has a syllable rhythm. 

We have made an experiment testing its robustness:

the experiment consisted of:

**7 tries no background noise, from 30cm**: https://drive.google.com/file/d/15XqPF-qTp5h3Js5RfgU-yO7ZWNuI14x2/view?usp=sharing

**7 tries no background noise, from 1m**: https://drive.google.com/file/d/1_J4KRvmbBdHWHWzz8-GxnjN1snCDVA2i/view?usp=sharing

**7 tries no background noise, from 3m**: https://drive.google.com/file/d/1bafFgYCVepoukUZTQFGe9seul2shS6F6/view?usp=sharing

**7 tries with background knocking noise, from 1m**: https://drive.google.com/file/d/1ZO0dkavtI3zt9yy6JgWnE4jvzhyE8G6y/view?usp=sharing

