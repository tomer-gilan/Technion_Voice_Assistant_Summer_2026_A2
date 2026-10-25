# System States and Error Messages

Group A2, IoT course, Summer 2026. What each state means, how it looks, and what to do when something is wrong.

## Where to see the state

| Where | What you see |
|---|---|
| Control center: the big circle | The assistant's current state (the table below), as a color and a word |
| Control center: Board health | A light for each startup check: SD, microphone, speaker, Wi-Fi, clock, OpenAI |
| The board itself, without a computer | Status sounds (when turned on, `SOUNDS on`): two falling tones = offline, two rising tones = working again, one low tone = a question failed. The timer: 3 beeps with the LED blinking. The BOOT button: a high start tone before a manual recording, a low tone when the press is rejected, and holding it for 2 seconds is a technician check. The full table: "Without a computer" below |
| Serial (log) | A line for each event, and `PASSED` / `FAILED` lines for each question |

## Without a computer: what you hear and see on the board

What the board itself shows or plays in each state, without the control center. The LED on the board (GPIO 2) is the user's light (`Turn on the light`) and not a status light, so it does not indicate any state. The timer blinks it only as part of its beeps.

| State / event | On the board itself | Only in the control center |
|---|---|---|
| `booting` (about 7 to 11 seconds) | Nothing: the LED is off and it is silent | The Board health lights, the circle shows Starting up |
| `ready` | A spoken opening greeting, once after startup | The circle is green |
| `armed` (Hands-free waiting) | Nothing | Waiting for speech, the sound level |
| Manual `listening` (Talk, BOOT, `LISTEN`) | A short high start tone just before the recording. In Hands-free there is no tone, because the speech has already started | The circle is red, the microphone meter |
| `processing` / `tool` | Nothing (the LED changes only if the tool is the LED) | The circle is orange |
| `speaking` | The answer itself | The circle is blue |
| `reconnecting` | Nothing | The circle is orange |
| `offline` (no Wi-Fi or OpenAI) | Two falling tones, **only if Status sounds is on** (off by default) | The circle is red, the lights and the reasons |
| Working again | Two rising tones, only if Status sounds is on | |
| A question failed | One low tone, only if Status sounds is on | `LISTEN #N FAILED` |
| The SD card is missing, or was removed while running | Nothing: it continues without recordings | A red SD light with the reason |
| `halted` (the microphone task did not start) | Nothing (silent) | A Stopped banner |
| The timer ended | 3 beeps, the LED blinks, and a spoken message | A countdown |
| A rejected BOOT press (offline, not ready, busy answering, saving the last answer, microphone in use) | A short low tone | `LISTEN ignored - ...`, and the button shows Busy |
| Technician check: holding BOOT for 2 seconds (when the button is turned on) | 4 beeps in the order Wi-Fi, OpenAI, SD card, microphone: high = OK, low = failed | The Board health lights |

Status sounds is off by default, so by default the board does not play network changes. The button sounds (start, rejected, technician check) always work, regardless of Status sounds, and follow the volume: when the board is muted they are silent. In the technician check, "microphone OK" means that the signal check at startup passed. Holding the button first cancels the LISTEN that the press itself started.

## The assistant's states

| State (`@@STATE`) | In the circle | What is happening | What to do |
|---|---|---|---|
| `booting` | Starting up, gray, spinning | The startup checks and connecting | Wait. Each check appears in Board health |
| `ready` | Ready, green | Ready for a question | Talk, the space bar, or the BOOT button (if turned on) |
| `armed` | Waiting for speech, green, breathing | Hands-free: waiting for someone to speak | Just speak |
| `listening` | Listening, red, pulsing | Recording and sending to OpenAI (in a manual recording a short start tone is heard first) | Speak. Cancel or BOOT cancels |
| `processing` | Processing, orange | The recording has ended, waiting for the answer | Wait (usually about a second) |
| `tool` | Processing, orange | Carrying out an action on the board (LED, volume, timer...). The state also ends when the question failed or the connection dropped, and does not get stuck | Wait |
| `speaking` | Speaking, blue | Playing the answer. Also between the request for a greeting, a timer message or an answer to a typed question and the start of its audio | Listen. Talk is rejected until it is back to Ready |
| `saving` | Speaking, blue | Saving the answer to the SD card | A second. Talk comes back afterwards |
| `reconnecting` | Reconnecting, orange | The connection to OpenAI dropped, reconnecting (with a growing wait) | Wait. After 5 attempts it becomes offline |
| `offline` | **Offline, red** | No Wi-Fi, or OpenAI is not available (also when Wi-Fi is up but there is no internet). It keeps retrying every 30 seconds (with no Wi-Fi it does not try OpenAI at all), and immediately when the Wi-Fi comes back. The timer, the controls and the recordings work | Check the hotspot, or switch networks in the Wi-Fi network card. No Reset is needed. When the hotspot comes back, the connection to OpenAI is restored within seconds (about 1-3). If it still does not answer after 10 seconds, the board goes back to retrying every 30 seconds |
| `halted` | Stopped, red | Stopped: the microphone task could not start (out of memory). This is the only state that stops | Reset |

When the control center is not connected to the board, the circle shows **Not connected**. This is a state of the page, not of the board.

## The startup checks (Board health)

| Light | Red means | What to do | Does the assistant work |
|---|---|---|---|
| SD card | `no card (or not FAT32) - running without recordings`. Also a card that was removed while running: after a recording fails the board checks the card, and if it does not answer: `card removed or failed - insert it and press Reset` | Insert a FAT32 card and Reset. Putting a card back without a Reset leaves the light red | **Yes**, just without recordings. The recordings tab shows "no SD card" |
| Microphone | No signal from the microphone | Check the INMP441 wiring (SCK 26, WS 25, SD 21, 3.3V) | It answers, but will not hear |
| Speaker | The I2S driver did not start | Reset. If it happens again: check the wiring | No |
| Wi-Fi | `network not found` / `check the password` / `connection lost` | Turn on a hotspot (2.4GHz), or enter a different network in the Wi-Fi network card. A saved network that failed twice in a row (for example a wrong password) is temporarily replaced by the network from `SECRETS.h`, and then `@@WIFI_NET` shows `default` | Offline until there is a network |
| Time | `no answer from the time servers` | A network that blocks NTP (UDP 123): switch to a hotspot | Without a clock there is no TLS, so it is offline |
| OpenAI | `offline - retrying every 30 s`. `waiting for Wi-Fi`: no Wi-Fi, so no attempts are made (and none counted) until it is back. Red also when the Wi-Fi is up but there is no internet, not just orange "connecting". `API key refused (HTTP 401)` (or 403): OpenAI refused the API key | Check the internet (not just the Wi-Fi), and that the API key is valid and has credit. A refused key: paste a different key in the OpenAI API key card (without compiling). A saved key that is refused twice in a row is temporarily replaced by the key from `SECRETS.h`, and then `@@OPENAI_KEY` shows `default`. When the Wi-Fi comes back, or when you save a new key, the light is orange (`Wi-Fi is back - reconnecting` / `new API key - connecting`) until the connection is ready | Offline, keeps retrying |

## Common error messages (Serial and the control center)

| The message | What it means | What to do |
|---|---|---|
| `LISTEN ignored - offline` | There is no connection, so you cannot ask | Wait for the connection. The timer and the controls work |
| `LISTEN ignored - still waiting for or playing back a reply` | The previous answer has not ended yet | Wait for Ready |
| `LISTEN ignored - the assistant is still answering, try again when it is Ready` | The greeting, a timer message or an answer to a typed question is still on its way or playing | Wait for Ready and try again |
| `LISTEN #N FAILED: no server activity for 15000 ms` | The server did not answer for 15 seconds. The board closes the connection and reopens it, so the conversation context (for example the name you said) is lost | Ask again. If it happens again: check the internet |
| `LISTEN #N FAILED: disconnected before a reply arrived` | The connection dropped in the middle | The board reconnects on its own. Ask again |
| `LISTEN #N dropped - that didn't sound like speech` | Hands-free discarded a noise (a knock, a cough) | This is intended. If it also happens to speech: raise the Sensitivity |
| `@@GET_ERROR - no SD card` | An attempt to download a recording without a card (or after the card was removed) | Insert a card and Reset |
| `@@BUSY` | The board is in the middle of a conversation and cannot transfer a file | Try again when it is Ready |
| `WIFI_PASSWORD ignored - send WIFI_SSID <name> first` | The network commands were sent in the wrong order | The card in the control center sends them in the right order |
| `WIFI_PASSWORD ignored - a WPA2 password is 8 to 64 characters` | The password is shorter than 8 bytes or longer than 64 (a Hebrew letter is 2 bytes). Spaces at the ends count as part of the password; a password of only spaces counts as "no password" (an open network) | The control center checks this before sending |
| `OPENAI_KEY ignored - expected an OpenAI API key: sk-..., 20 to 200 characters, only letters, digits, - and _.` | What was pasted in the OpenAI API key card does not look like a key (for example a space got into the middle of it). The key is not printed | Copy the whole key again. The control center checks the same thing before sending |
| `OpenAI refused the saved API key (HTTP 401) - check it, or set another in the control center.` | OpenAI refused the key when opening the connection (expired, revoked, or it has no access) | Paste a different key in the OpenAI API key card. After two refusals in a row the board switches to the key from `SECRETS.h` on its own |
| `Unknown command "(not shown - it may hold a key or a password)"` | An unknown command that starts with `OPENAI_KEY` or `WIFI_PASSWORD` (for example with no space before the key). The board does not print it, because it may contain a secret | Type it again with a space, or use the cards in the control center |
| `Unknown command "..."` | An unknown command. The message lists all the commands | |
| A short low tone after pressing BOOT | The press was rejected: there is no connection, the board is not ready, the assistant is answering, it is saving the last answer, or the microphone is in use | Wait for Ready and try again |
| `Board not responding - no report for 15 s. If it stays like this, press Reset (EN) on the board.` | The board prints `@@ALIVE` every 5 seconds. 15 seconds without any line from it (after the first `@@ALIVE`) grays out the status. The alert is paused while a recording is downloading and during Replay. The next line from the board restores the status | Wait a moment. If it stays like this: Reset (EN) on the board |
| A red banner `Lost the board's USB connection.` | The USB cable was disconnected. The board is powered by the cable, so it restarts | Connect it again. The page reconnects on its own, and the board says the greeting after about 10 seconds |

## Behavior safety

- The board does not record or respond while it is speaking, and for 0.6 seconds afterwards (so it does not hear itself).
- Talk in the middle of an answer is not sent: the button shows Busy. The same applies while the assistant is still saying the greeting, a timer message or an answer to a typed question, or while the answer is still on its way (the state is then Speaking). Talk from the keyboard, BOOT and the `LISTEN` command are also rejected then, with the message `LISTEN ignored - the assistant is still answering, try again when it is Ready`.
- A typed question (`ASK`) is limited to 200 bytes: about 100 letters in Hebrew (two bytes per letter) or 200 in English. Anything longer is cut off.
- The connection to OpenAI is checked all the time (a heartbeat, a ping every 15 seconds): if 2 answers (pongs) in a row do not come back within 10 seconds, or there is no activity at all from the server for 15 seconds during an answer, the board closes the connection and reopens it (the conversation context is lost), and goes to Reconnecting and then to Offline if it does not succeed. This way a Wi-Fi with no internet is also detected, and the OpenAI light turns red.
- A Wi-Fi network saved from the control center that fails twice in a row (for example a wrong password): the board switches to the default network from `SECRETS.h`, and back the same way. This happens in memory only, and the saved network stays in flash for the next boot. A Wi-Fi password is saved exactly as typed, including spaces at the ends.
- An error returned by the server sends Hands-free back to listening immediately, without waiting. A connection drop in the middle of a question fails only that question (`LISTEN #N FAILED`), and does not count as a failure of the Hands-free test (no `HANDS-FREE FAILED` verdict).
- A short press on BOOT (about 30ms) is caught even while the assistant is speaking: the button is sampled by a timer and not in the main loop, so you can cancel an answer with a quick press.
- A tool that received an invalid value answers the model with an error message, and the model says out loud that it did not succeed, instead of staying silent.
- Timer: the value 0 cancels. A negative value or one shorter than a second returns an error and the assistant asks again, without touching the running timer. Above an hour, it sets one hour, and the assistant says the limit was applied.
- Volume set by voice is limited to 10%-100%, so the assistant can always confirm out loud.
- Switching between Hands-free and push-to-talk by voice (`set_hands_free`) happens only on an explicit request in the current question, and only while answering a user's question (not in the greeting or a timer message). A question about the mode does not change it.
- While the control center plays a recording on the computer's speakers, Hands-free does not listen (`LAPTOP_PLAYBACK`), so the recording is not taken as a question. If the page is closed in the middle, the board goes back to listening on its own at the end of the recording's length.
- The API key and the Wi-Fi password are not printed in the board's log or in the control center's log: of the key you see only `sk-...` and the last 4 characters.
