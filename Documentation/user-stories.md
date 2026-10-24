# User Stories - Voice Assistant on ESP32

Group A2, IoT course, Summer 2026.

**Project goal:** a voice assistant on a cheap microcontroller (ESP32, 520KB of memory). You talk to it, it answers out loud and controls components on the board. Speech understanding is in the cloud (OpenAI), and everything else is on the microcontroller. A control center in the browser shows what is happening and lets you change settings.

The demos in order: first the main features (voice conversation and controlling the board by voice), then more ways to talk, and finally settings, working without a computer, and statistics. A short list is also in the control center (the Demo checklist button). Anyone starting the system for the first time can click Tour for a short tour of the screen.

**Prerequisite - network:** all the demos run on a phone hotspot (not the Technion network), on 2.4GHz. The board knows the hotspot in advance, from `SECRETS.h` or from the Wi-Fi network card in the control center (name and password, without compiling).

**Edge cases:** the "Edge cases" column refers to the numbers in the [edge case list](edge-cases-extended.md) that are demonstrated at that step. The network and hardware cases (1, 3, 7, 8) are demonstrated in a separate block before the statistics.

## Voice conversation

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 1 | User | Turn on the device and see that all the components are OK | I know it is ready for a conversation | Connect the power and click Connect board in the control center. | The health lights turn green (Wi-Fi, SD card, Microphone, Speaker, OpenAI) and the assistant says an opening greeting. Until the clock is synchronized, the OpenAI light shows `Syncing the clock…`. | 8, 25, 28 | |
| 2 | User | Press a button and ask a question | I get a spoken answer | Click Talk (or press the space bar) and ask `What is the capital of France`. Ideas for questions are in the Try saying card (a list to read, not buttons). | The speaker answers within about a second. | 14, 15, 17 | |
| 3 | User | See on the computer what the assistant heard and what it answered | I understand whether it understood me | Watch the control center during the question. | The state changes (Listening, Processing, Speaking), and the conversation appears as bubbles with the time and the response time. When the assistant does something on the board, a tag appears (💡 LED, 🌙 part of the day, 🔊 volume, ⏰ timer, 🕒 time, 📶 status). | 20 | |

## Controlling the board by voice

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 4 | User | Say "turn on the light" | The light turns on without touching anything | Say `Turn on the light`, then `Dim the light to 30 percent`, and finally `Turn off the light`. | The LED turns on and the assistant confirms out loud. When dimming, the brightness slider moves, and at the end the LED turns off. | 18, 20 | |
| 5 | User | Change the speaker volume by voice | I don't need to touch the computer | Say `Set the volume to 30 percent`, then `A bit louder`. | The volume slider moves to 30% and the assistant confirms. On the second request the volume goes up. | 19, 20 | |
| 6 | User | Ask for a timer or a reminder and ask what time it is | I don't forget something while I'm busy | Say `Remind me in 30 seconds to check the oven`. Then `Turn off the light in 20 seconds`, and finally `What time is it`. | A countdown appears on the screen. At the end of the reminder the board beeps 3 times, the LED blinks and the assistant says `Time's up - check the oven`. The light turns off at the end of the second timer. The assistant says the time. | 7, 31 | |
| 7 | User | Ask what part of the day it is | The assistant answers according to the light in the room | The sensor is simulated (as the course lecturer instructed): move the Light sensor slider to a low value and ask `Is it day or night`. Raise the value and ask again. You can also use Simulate: Auto day-cycle (a simulated day in about 2 minutes, with noise like a real sensor's). Touching the slider returns it to Manual. | At a low value the assistant answers `night`, and at a high value `day`. In Auto day-cycle the value goes through night, sunrise, day and sunset on its own. | 12 | |
| 8 | User | Ask the assistant how the board is feeling | I know the board is healthy and the signal is good | Say `How are you doing`. | The assistant says how strong the Wi-Fi signal is (for example strong), how long the board has been on, and how much memory is free. A 📶 tag appears in the conversation. | - | |

## More ways to talk

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 9 | User | Talk without pressing anything | I can use it when my hands are busy | Turn on the Hands-free switch and say a sentence. It also works by voice: with Talk, say `Switch to hands-free mode`, and to go back `Go back to the button`. | The board starts recording on its own, stops when you stop speaking, and answers. On a spoken request the switch moves on its own, but a question about the mode (`What is hands-free?`) does not move it. | 13, 14, 23, 24, 27, 37 | |
| 10 | User | Have the assistant remember what I said earlier | The conversation feels natural | Say `My name is Omri`, and then ask `What is my name`. | The assistant answers `Omri`. | 6 | |
| 11 | User | Talk to the assistant in Hebrew | It suits people who don't speak English | Ask in Hebrew `Ma birat Tzarfat?` ("What is the capital of France?"). | The assistant answers in Hebrew. Hebrew works less well than English, and the transcript in the bubble may be inaccurate. | - | |
| 12 | User | Type a question when the room is noisy | I get an answer without speaking | Type in the Or type a question… field (up to 200 bytes, about 100 letters in Hebrew) and click Ask. | The question appears in the conversation as a bubble, and the assistant answers out loud as usual. | 21 | |
| 13 | User | Cancel a question in the middle | I don't wait for an answer I don't need | Click Talk and immediately Cancel. | The recording is deleted, the assistant does not answer and the board goes back to Ready (in the terminal: `LISTEN #N CANCELLED`). As long as the assistant is speaking (also during the greeting, a timer message or an answer to a typed question), Talk is rejected: wait for it to finish, or click Cancel. | 15, 16 | |

## Checking and recordings

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 14 | User | Check that all the parts of the system work before starting | I know everything is OK without guessing | Click Self-test next to Board health. | The LED blinks, a beep is heard, and the check rows (Wi-Fi signal, LED, Speaker, Light sensor, SD card, Board clock, OpenAI) get a ✓. | - | |
| 15 | User | Hear again a command I said and an answer I got | I can check what was picked up | In the recordings tab, select a recent command and play it. | The recording plays from the computer, with a waveform. You can also delete it. Even with Hands-free on, the board does not treat the playback as a question: the microphone does not listen until the playback ends or is stopped. | 8, 30, 36 | |

## Settings

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 16 | User | Change the volume, mute, and change the LED brightness | The device suits the room | Move the volume slider and click Test beep, check Mute, change the LED brightness. | The beep is heard at the new volume, Mute silences the speaker, and the LED changes brightness. | 22 | |
| 17 | User | Tune the Hands-free sensitivity and the silence time | It is not set off for no reason and does not cut me off | Change Sensitivity and Stop after silence and check speech and noise again. | Speech still starts it, and a short noise (a knock) does not. The recording stops after the chosen silence time. | 13, 29 | |
| 18 | User | Change the day, evening and night thresholds | They suit my lighting | Change the thresholds and click Apply thresholds. | The part-of-the-day light updates according to the new thresholds. | 12, 22 | |
| 19 | User | Have the settings saved | I don't need to set them again every time | Press Reset on the board. | After startup the volume, the brightness and the Hands-free settings stay as they were. | 10 | |

## Without a computer

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 20 | User | Use the assistant without a computer | It can stand in the room like a regular device | In On the board itself, turn on BOOT button, Status sounds and Hands-free. Disconnect the USB cable from the computer, **and only after that** connect a power bank (never both together: the course's safety instruction). Ask by voice or by pressing BOOT. | The board restarts with the saved settings, and after about 10 seconds says an opening greeting. It answers questions, and sounds show the state (details in edge cases 11, 33). | 9, 11, 32, 33 | |
| 21 | User | See a saved conversation again, even without the board | I can show or analyze a previous run | When the board is not connected, click Replay a log… and select a saved log (Save log to folder…, or a log from `tools/serial_logger.ps1`). | The control center plays the conversation as if it were happening now, and you can change the speed. This is a log and not a live run: it does not go into the live statistics or into the saved log. | - | |

## Statistics

| # | As a... | I want to... | So that... | What to do | What should happen | Edge cases | ✓ |
|---|---|---|---|---|---|---|---|
| 22 | User | See usage statistics | I know how fast it answers and how much I used it | Open the Statistics panel, click Export CSV, and finally Summary and Save report. | In the panel: the number of commands, the average response time, the fastest and the slowest, voice-triggered actions (LED, volume), light sensor changes, and a graph of the response time for each command. The CSV includes the date and time, the board's boot number and the command's number within that boot. Summary shows everything large with the board's settings, and Save report saves a single HTML page. | - | |
