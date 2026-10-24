# Edge Cases - Voice Assistant on ESP32

Group A2, IoT course, Summer 2026.

Cases the system knows how to handle, and how to see them in the demo.

| # | The case | What the system does | How to demo it | ✓ |
|---|---|---|---|---|
| 1 | Noise in the room in Hands-free mode (a knock on the table, a clap, footsteps) | The voice detector recognizes that it is not speech and does not start recording. | Knock on the table and clap next to the device. The state stays Waiting for speech. | |
| 2 | Silence | In Hands-free mode, silence is not sent to the cloud at all. When using the button, the system answers with a generic sentence. | Hands-free on and stay silent: no recording starts. Click Talk and stay silent: the assistant answers with a generic sentence. | |
| 3 | Clicking Talk while the assistant is still answering | A second recording cannot start while the assistant is speaking (also a greeting, a timer message or an answer to a typed question): the request is rejected (also `LISTEN` from the terminal and BOOT, with a message). Wait for it to finish, or click Cancel. | In the middle of an answer the Talk button shows Busy and cannot be clicked, and the space bar does not start a recording either. | |
| 4 | A short pause in the middle of a sentence | The recording continues until about a second of silence (can be changed with Stop after silence). | Say a sentence with a short stop in the middle. It is sent as one question. | |
| 5 | The connection to OpenAI drops | The board reconnects on its own, with a growing wait between attempts (backoff). After 5 failed attempts in a row it goes Offline (red OpenAI light) and keeps retrying every 30 seconds, never stopping. When the network comes back it reconnects within a few seconds, and when the connection comes back it recovers on its own. | Turn off the hotspot for a few seconds and turn it back on. The OpenAI light turns red, and after the network comes back it turns green again on its own. | |
| 6 | Board Reset | The settings (volume, mute, brightness, light thresholds, Hands-free) are saved in flash and come back after startup. | Change the volume, press Reset, and check that the volume stayed. | |
| 7 | The control center disconnects in the middle | A red banner appears, and when you connect again the page reconnects on its own and gets the current state (STATUS). | Click Disconnect and then Connect, or unplug the USB cable and plug it in again. All the lights and the state appear again. | |
| 8 | Volume out of range | The board limits the volume to 0 to 100. | Say `Set the volume to 150 percent`. The volume stops at 100%. | |
| 9 | Speech from a standard distance (a meter) with background noise | The voice detector recognizes the speech even with noise in the background, and the assistant understands the request and carries it out. | One person drums on the table, and the other stands a meter from the device and says `Turn on the light`. The LED turns on. | |
| 10 | The timer ends when there is no connection to OpenAI | The timer runs on the board itself, so it beeps and blinks even without internet. Only the spoken message needs a connection: if there is no connection when the timer ends, it is not said. | Start `Set a timer for 30 seconds` and turn off the hotspot. After 30 seconds the board beeps. | |
| 11 | Several hardware requests in one command | The assistant catches all the requests in the command and carries them all out together. | Say `Set the light brightness and the volume to 100 percent`. Both the LED brightness and the volume go up to 100%. | |
