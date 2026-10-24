# Demo Plan - 25 Minutes

Group A2, IoT course, Summer 2026. This document is for us: what to prepare, in what order to demo, and what to do if something does not work. The order follows the [user stories](user-stories.md) and the [extended edge cases](edge-cases-extended.md) (the edge case numbers here are from the extended list). Both are printed in two copies (`user-stories.pdf`, `edge-cases-extended.pdf`). The original list [edge-cases.md](edge-cases.md) stays in the repository and is not printed for the demo.

The timeline: arrive 20 minutes early and set up (the "20 minutes before" section), then 25 minutes of demo, which open with one minute of presenting the project without slides, continue from the main features (conversation and controlling the board by voice) to the settings, and end with the statistics.

## The day before

- [ ] Upload the latest firmware and go once through the whole Demo checklist in the control center.
- [ ] Check that the time is correct (`What time is it?`, or the Board clock row in Self-test). The clock follows Israel's time zone automatically, including the switch to daylight saving time and back.
- [ ] In On the board itself, turn on BOOT button and Status sounds (they are off by default, and the choice is saved on the board).
- [ ] **OpenAI key:** check that the account has credit, and run 10 questions in a row. Prepare a backup key with credit, in a text file on the laptop: if needed, paste it in the **OpenAI API key** card in the control center → Save & connect, without compiling, and the board reconnects with it within seconds (edge case 35).
- [ ] Charge a power bank (for the demo without a computer), and practice the switch twice: disconnect the USB cable from the computer, and only after that connect the power bank (not both together, per the course's safety instructions). The board restarts, says the opening greeting after about 10 seconds, and the settings are kept. To go back to the computer: first disconnect the power bank.
- [ ] **Safety:** a board connected to a computer by USB must not be connected to any other power source (including a power bank and the 5V pin). To use it without a computer, first disconnect the USB cable.
- [ ] **Hotspot:** on the phone, turn on a hotspot on 2.4GHz (on an iPhone: Maximize Compatibility). A name in English with no emoji and no apostrophe (up to 32 characters), a password of 8 characters or more. Enter the name and password once in `ESP32/SECRETS.h`, or without compiling in the **Wi-Fi network** card in the control center → Save & connect. The network is saved on the board. Do not use the Technion network.
- [ ] Do a full trial run on the hotspot: Reset the board, the Wi-Fi turns green, and a few questions. Make sure it stays connected after another Reset, and that the phone has mobile data (without it there is no OpenAI).
- [ ] Prepare a second network for switching networks (edge case 3): a hotspot on a second phone, also on 2.4GHz, and check once that you can switch to it in the Wi-Fi network card and back with **Back to default**.
- [ ] **Backup video:** record a short video (about 3 minutes) of the main features working (stories 1-15) and of a few edge cases (hotspot off and on, SD card removed, USB disconnected), and keep it on one of our laptops, open and loaded. It is for the case where a main feature does not work.
- [ ] **The project video:** required for submission, within a week of the demo. Record it before the demo day, because the equipment is returned on that day (see "After the demo").
- [ ] During the rehearsal, save the log (Save log to folder…). It gives something to show with Replay a log… in story 21. **Replay is not a substitute for the backup video:** it plays only the control center, without the board, the lights and the speaker, and only when the board is not connected.
- [ ] Print **2 copies of `user-stories.pdf` and 2 copies of `edge-cases-extended.pdf`** (only the extended list, without `edge-cases.pdf`), and a copy of the [quick start guide](quick-start.pdf) to leave next to the laptop.
- [ ] Charge the laptop and the hotspot phone.
- [ ] Pack: the board, a USB cable (and a spare cable), a power bank, an SD card, the laptop and its charger, and the bag for returning the equipment (see "After the demo").

## On the demo day

- [ ] **Arrive at least 20 minutes before** the group's time, set up and check that everything works. The presentations alternate between rooms 231 and 236; you can go in half an hour early without waiting outside.
- [ ] **The hotspot phone physically against the window** (for good reception), with mobile data on and connected to a charger. Do not rely on the Technion network.
- [ ] The backup video open on the laptop, and two printed copies of each document ready for the examiners.

## 20 minutes before (setting up in the room)

- [ ] Turn on the hotspot on the phone (against the window), connect the board to USB, open `control_center/control_center.html` in Chrome and click Connect board.
- [ ] Wait for all the lights to be green and hear the opening greeting.
- [ ] Tune the Sensitivity to the noise in the room ([calibration](calibration.md)), and ask the examiners for about 20 seconds of silence for story 9 (Hands-free). Hold the board 20-30 cm from your mouth.
- [ ] Click Self-test (next to Board health): the LED blinks, a beep, and all the rows with ✓.
- [ ] Clear everything: Clear in the conversation, Reset in the statistics, and the reset button at the top of the Demo checklist.
- [ ] Start from a normal state: volume about 60%, LED off, Hands-free off, the light slider on day.
- [ ] If there is a big screen or a projector: Stage view (the F key) when demoing a conversation.
- [ ] As soon as you start: Demo checklist → the ⏱ button that starts the demo clock. A clock appears in the header with the step you should be at according to the table below, and it turns red after 25 minutes.

## The order

The user stories follow the order of the printed table (1-22), so the examiners can mark ✓ in the order they see them. The easy edge cases are done in the same row as their story, and the network and hardware robustness (hotspot, SD) in one block before the statistics. The Demo checklist in the control center is built in the same order: stories 1-22 followed by the edge cases, each with its number here, and the demo clock shows the step according to the first column of the table.

| Minutes | What we demo | Story / edge case | Tip |
|---|---|---|---|
| 0-1 | A short introduction, without slides | - | One sentence about the project's goal, then move on to the demos. Say out loud: the light sensor is simulated with a slider, and Hebrew works but less well |
| 1-2 | Boot and health lights | Story 1 | Reset the board in front of the examiners, so they see the lights turn on one by one |
| 2-3.5 | A push-to-talk question, and the conversation on the screen | Stories 2, 3 | Let an examiner pick an example from Try saying and ask it themselves |
| 3.5-5 | Controlling the board by voice: LED and volume | Stories 4, 5, edge cases 18, 19, 20 | `Make the light blue`, `Set the volume to 150 percent`, and a double request |
| 5-6.5 | Reminder and timer | Story 6 | `Remind me in 30 seconds to check the oven` and meanwhile continue to the next item. It will ring and say the reminder in the middle. Then `Turn off the light in 20 seconds`. All the documents use the same examples: 30 seconds for the reminder and the timer, 20 seconds for turning off the light (all under a minute, so they finish during the demo) |
| 6.5-8 | Part of the day, and the board status | Stories 7, 8, edge case 12 | Move the light slider before the question (or Simulate: Auto day-cycle: a full simulated day in about 2 minutes). `How are you doing` |
| 8-10 | Hands-free, and noise that does not set it off | Story 9, edge cases 13, 14, 23, 24 | A knock on the table and a cough, and show the note in the conversation. One examiner speaks and the other drums |
| 10-11.5 | Conversation memory, and Hebrew | Stories 10, 11 | `My name is Omri` then `What is my name`. In Hebrew: `Ma birat Tzarfat?` ("What is the capital of France?") |
| 11.5-13.5 | A typed question, and canceling | Stories 12, 13, edge cases 15, 16, 17, 21 | Type a question in Ask. Talk in the middle of an answer (rejected), Talk and immediately Cancel |
| 13.5-15 | Self-test, and recordings from the SD card | Stories 14, 15 | The button next to Board health, then play the last command and see the waveform |
| 15-17 | Settings, and that they are kept | Stories 16-19, edge case 10 | Change the volume and brightness, Hands-free and the light thresholds, Reset the board, and show that they stayed |
| 17-19 | Without a computer | Story 20, cases 9, 11 | Disconnect the USB cable from the computer, and only after that connect the power bank (not both together; the board restarts, and wait about 10 seconds for the opening greeting). Ask with Hands-free or the BOOT button, and hold BOOT for 2 seconds for the technician check (edge case 33). Practice it twice |
| 19-20 | Replaying a log, without the board | Story 21 | Replay a log… with a saved log, at 4x speed. At the end Stop, and connect the board to the computer again (Connect board) |
| 20-23 | Robustness: network and hardware | Edge cases 7, 1, 3, 8 | A 30-second timer and turn off the hotspot (Offline and the beep on time), turn it back on. After the hotspot comes back, recovery takes a few seconds; budget about 15 seconds until Ready and explain what you see in the meantime. Then switch networks in the Wi-Fi network card, and if there is time, Reset without an SD card |
| 23-24 | Statistics and summary | Story 22 | The graph, Summary, and Save report |
| 24-25 | Reserve | - | Questions, or the examiners try it themselves |

Edge cases 2, 4, 5, 6 and 22 are not in the table (long, or they cannot be triggered on purpose). If there is time left in the reserve or the examiners ask: 5 (turn off the hotspot right after a question), 22 (`VOLUME abc` in the terminal), and explain the rest from the printed list. Case 4 is also what we show when OpenAI is not available (see "If something does not work").

## After the demo

- [ ] Return **all the equipment on the demo day**: the board with everything in it, and also the rest of the equipment that is not part of the project (cables, power bank, etc.), **in a bag with the group's number and name**, all at the same time.
- [ ] Submit the project video within a week of the demo. The course grade is held until the equipment is returned and the video is submitted.

## Letting the examiners operate it

It is better for the examiners to try it themselves. During the demo, hand them the laptop and the board in at least four places: they click Talk and ask (stories 2, 3), drum on the table in front of Hands-free (story 9), type a question in Ask (story 12), and choose which edge case to try from the printed list themselves (for example turning off the hotspot). If they do not know the screen, click **Tour**: a short tour of 9 stops that explains each part. The Try saying card gives them ready-made sentences, and so does the printed [quick start guide](quick-start.md). Remind them that it is best to speak in English (Hebrew works, but less well), close to the microphone, and that they can ask any question, not just the examples.

## If something does not work

| What happened | What to do |
|---|---|
| A red Wi-Fi light / Offline | Check that the hotspot is on. No Reset is needed: the board connects on its own, and goes back to Ready within about 15 seconds of the network coming back (the connection to OpenAI reopens within a few seconds, and the OpenAI light is orange until then). A different hotspot: the Wi-Fi network card. The Technion Wi-Fi does not work with the board |
| The network entered in the Wi-Fi network card does not connect (for example a wrong password) | Click **Back to default** and the demo continues on the first hotspot. If you do not click it, after two failed attempts (about two minutes) the board switches to the network from `SECRETS.h` on its own |
| No internet at all, or a main feature does not work | Open the **backup video** on the laptop and explain what you see. Replay a log… (story 21) shows only the control center, without the board, the lights and the speaker, so it does not replace the video. Tell the examiners that it is a log or a video, not live |
| A red OpenAI light while the Wi-Fi is green | There is no credit in the OpenAI account or the key is not valid (edge case 4; a refused key shows `API key refused` on the light). Paste the backup key in the OpenAI API key card → Save & connect (edge case 35), without compiling. If that does not work either, demo the Offline mode (edge case 4: the board retries every 30 seconds, the timer and the controls work) and switch to the backup video |
| No answer to a question | After 15 seconds the board gives up on its own, writes why, closes and reopens the connection (the assistant forgets what was said earlier, for example the name) and goes back to Ready. Ask again (edge case 6) |
| The assistant did not understand | Speak closer, preferably in English. The bubble shows what it heard, and that helps to explain |
| The room is noisy, or Hands-free is set off by noise | In this order: (1) Lower the Sensitivity. (2) Turn off Hands-free and switch to **Talk** (3 seconds of recording, the board 20-30 cm from your mouth). (3) Last resort: type the question in the Or type a question… field and click Ask, and it answers out loud as usual (edge case 21) |
| A red banner: the cable disconnected | Connect it again. The board is powered by the same cable, so it restarts (about 10 seconds until the opening greeting). The control center reconnects on its own |
| A red banner: the board stopped | Press the Reset (EN) button on the board (happens only if the microphone task did not start) |
| A red SD light | The board works without recordings. Insert the card and Reset, or continue without it (edge case 8). A card removed while running also turns it on (edge case 30), and putting it back without a Reset does not turn it off |
| The time is wrong | The clock is taken from a time server on the network. The Board clock row in Self-test shows the difference from the laptop. Explain and continue |
| The timer did not ring | It waits until the board is free. If the assistant is in the middle of an answer, the ring comes right after it |
