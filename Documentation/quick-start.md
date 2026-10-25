# Quick Start Guide - Voice Assistant on ESP32

<img src="images/repo-qr.png" alt="QR code to the GitHub repository" width="80" align="right">

Group A2, IoT course, Summer 2026. One page for starting the system on your own. All the code and documents are in the GitHub repository (scan the QR code).

## Getting started

1. The board is connected to the computer by USB, and the phone's hotspot is on (2.4GHz, and the board needs to know it - see "Connecting to the hotspot" below).
2. The control center (`control_center/control_center.html`) is open in Chrome. Click **Connect board** and select the board's COM port.
3. Wait until all the lights at the top are green. The assistant says an opening sentence. You can click **Self-test** for a quick check of all the parts.
4. Click **Talk** (or press the space bar) and speak, for up to 3 seconds. It works best in English; Hebrew works, but less well. Or turn on **Hands-free** and just speak.

New to the screen? The **Tour** button at the top gives a short tour.

![The control center](images/control-center.png)

## Connecting to the hotspot

The assistant needs Wi-Fi, and in the demo we use a phone's hotspot, not the Technion network (the board did not work with it. We did not check why; our guess is WPA2-Enterprise login or a sign-in page). Do this once, before the demo.

1. On the phone, turn on a hotspot on **2.4GHz**. The board does not connect to 5GHz. On an iPhone: Personal Hotspot → **Maximize Compatibility**. On Android: Hotspot → Band → 2.4 GHz.
2. The network name is up to 32 characters, preferably in English, with no emoji and no apostrophe. The password is 8 to 64 characters.
3. The board learns the network in one of two ways:
   - **Without compiling:** in the control center, in the **Wi-Fi network** card (under Device controls), enter the name and password and click **Save & connect**. The card shows Sent and then Saved only after the board confirms, and the network stays saved on the board after a Reset. If you save while the board is still in its startup Wi-Fi wait, the card shows `Sent - the board reads it once its Wi-Fi wait ends (up to 20 s)…` and waits up to 40 seconds for Saved.
   - **In the code:** the name and password in `ESP32/SECRETS.h` (the default, as the README explains). **Back to default** in the card returns to it. If a network saved in the card fails twice in a row (for example a wrong password), the board switches to the network from `SECRETS.h` on its own.
4. Check it: Reset the board, and wait for the Wi-Fi light to turn green. Do this the day before, and again when you arrive, not at the last minute.
5. To switch networks (for example to demo "the network changed"): a second phone's hotspot, also on 2.4GHz, in the Wi-Fi network card.

## What to try

| Topic | What to say | What will happen |
|---|---|---|
| What it can do | `What can you do?` | It lists them in one sentence |
| General question | `What is the capital of France?` | It answers out loud within about a second |
| Light | `Turn on the light` then `Dim the light to 30 percent` | The LED turns on and dims, and the bulb on the screen glows to match |
| Volume | `Set the volume to 30 percent` then `A bit louder` | The slider moves and it confirms |
| Reminder | `Remind me in 30 seconds to check the oven` | A countdown, and at the end beeps, a blinking LED and `Time's up - check the oven` |
| Time | `What time is it?` | The time and date from the board's clock |
| Part of the day | `Is it day or night?` | According to the light slider (simulates a sensor), or according to **Auto day-cycle**: a full simulated day in about 2 minutes |
| Memory | `My name is Dana` then `What is my name?` | It remembers the name |

## What you see on the screen

- **The big circle:** what the assistant is doing right now. Green is ready, red is listening, orange is thinking, blue is speaking.
- **Conversation:** what it heard and what it answered, how long it took, and what it did on the board.
- **Device controls:** volume, mute, LED and the light sensor. The settings stay saved on the board after a Reset.
- **Statistics:** how many commands, response times and a graph. **Summary** shows everything in full screen.
- **Stage view** (the F key): the assistant shown large, for people standing far away.
- **On the board itself:** the BOOT button works as the Talk button, and there are status sounds, so you can use the board without the computer. Before a manual recording you hear a short high start tone, and a rejected press (for example in Offline mode) sounds as a low tone. Holding BOOT for 2 seconds is a technician check: 4 beeps in the order Wi-Fi, OpenAI, SD card, microphone (high = OK, low = failed).

What each color and message means: [System states and error messages](system-states-and-errors.md).

## If something does not work

| What you see | What to do |
|---|---|
| Talk shows Busy | The assistant is still speaking (also during the greeting, a timer message or an answer to a typed question), and Talk is rejected until it finishes. Wait for the end of the answer, or click **Cancel** |
| A red light on Wi-Fi or OpenAI, the circle shows Offline | Check the hotspot (and that it has internet). The board reconnects on its own, without a Reset, within seconds of the network coming back. A different network: Device controls → Wi-Fi network |
| A red SD light | There is no card, or it was removed while the board was running: the assistant works normally, just without recordings. Insert a FAT32 card and press Reset (EN). Putting the card back without a Reset leaves the light red |
| A red banner about USB | Connect the cable again. The board is powered by the cable, so it restarts (about 10 seconds until the greeting), and the control center reconnects on its own |
| Talk shows Downloading… | The board is sending a recording to the computer and is not listening in the meantime: about 3 seconds for an answer, up to about 40 seconds for a 10-second voice command. Talk and Ask come back when the download finishes |
| The status is gray with `Board not responding - no report for 15 s. If it stays like this, press Reset (EN) on the board.` | The board reports every 5 seconds, and 15 seconds without a report grays out the status. Wait a moment, and if it stays like this: Reset (EN) on the board |
| It did not understand | Speak closer, preferably in English, and check in the bubble what it heard. In a noisy room you can type the question in the field under Talk |
| Hands-free is set off by noise | Lower the Sensitivity, or use Talk |
