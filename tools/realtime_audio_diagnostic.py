#!/usr/bin/env python3
"""Diagnostic for the step 13 mid-response disconnect (see PROJECT_OVERVIEW.md).

Reproduces the exact same session.update -> conversation.item.create ->
response.create flow the ESP32 sketch (test/test.ino) sends, but from a
plain desktop Python client - completely bypassing the ESP32 and the
WebSocketsClient (Links2004) Arduino library. On the device, every attempt
disconnects a few hundred ms to ~1.7s after response.create, always before
any response.output_audio.delta arrives, with free heap consistently
~150-155KB (not exhausted) and no consistent elapsed-time or content-length
pattern. If this script hits the same disconnect, that's strong evidence
the cause is on OpenAI's side, not the ESP32 client; if it doesn't, that
points back at something ESP32/WebSocketsClient-specific.

Uses text input (not real mic audio) to isolate the suspect component - the
audio OUTPUT side - since step 11 already showed the identical disconnect
signature with text input and zero I2S/heap involvement at all.

Usage:
    pip install websocket-client
    $env:OPENAI_API_KEY = "sk-..."      (PowerShell)
    python realtime_audio_diagnostic.py
"""
import json
import os
import sys
import threading
import time

import websocket  # pip install websocket-client

API_KEY = os.environ.get("OPENAI_API_KEY")
MODEL = os.environ.get("OPENAI_MODEL", "gpt-realtime-2.1-mini")
URL = f"wss://api.openai.com/v1/realtime?model={MODEL}"

# Mirrors test/test.ino's kSessionUpdateMessage exactly.
SESSION_UPDATE = {
    "type": "session.update",
    "session": {
        "type": "realtime",
        "model": MODEL,
        "instructions": (
            "You are a helpful, concise voice assistant running on an ESP32 "
            "dev board. Keep spoken replies short."
        ),
        "output_modalities": ["audio"],
        "audio": {
            "input": {
                "format": {"type": "audio/pcm", "rate": 24000},
                "turn_detection": None,
            },
            "output": {
                "format": {"type": "audio/pcm", "rate": 24000},
                "voice": "alloy",
            },
        },
        "reasoning": {"effort": "low"},
    },
}

# Text input instead of real mic audio - see module docstring for why.
CONVERSATION_ITEM = {
    "type": "conversation.item.create",
    "item": {
        "type": "message",
        "role": "user",
        "content": [{"type": "input_text", "text": "Hello there, how are you?"}],
    },
}

RESPONSE_CREATE = {"type": "response.create"}

state = {
    "session_updated": False,
    "response_start": None,
    "audio_delta_count": 0,
    "audio_bytes_total": 0,
    "done": threading.Event(),
}


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def on_open(ws):
    log(f"Connected. Sending session.update (model={MODEL})...")
    ws.send(json.dumps(SESSION_UPDATE))


def on_message(ws, message):
    if isinstance(message, bytes):
        log(f"<< binary frame, {len(message)} bytes")
        return

    try:
        event = json.loads(message)
    except json.JSONDecodeError:
        log(f"<< non-JSON text frame, {len(message)} bytes")
        return

    etype = event.get("type", "(missing type)")

    if etype == "response.output_audio.delta":
        state["audio_delta_count"] += 1
        b64 = event.get("delta", "")
        state["audio_bytes_total"] += len(b64)
        log(f"<< {etype}  (base64 span {len(b64)} chars, delta #{state['audio_delta_count']})")
        return

    if etype == "response.output_audio_transcript.delta":
        sys.stdout.write(event.get("delta", ""))
        sys.stdout.flush()
        return

    log(f"<< {etype}")

    if etype == "error":
        log(f"   error detail: {event.get('error')}")

    if etype == "session.updated" and not state["session_updated"]:
        state["session_updated"] = True
        log("Session ready - sending conversation.item.create + response.create")
        ws.send(json.dumps(CONVERSATION_ITEM))
        ws.send(json.dumps(RESPONSE_CREATE))
        state["response_start"] = time.time()

    if etype == "response.done":
        elapsed = time.time() - state["response_start"] if state["response_start"] else None
        print()
        suffix = f", elapsed: {elapsed:.2f}s" if elapsed is not None else ""
        log(f"Response done. Audio deltas: {state['audio_delta_count']}, "
            f"total base64 chars: {state['audio_bytes_total']}{suffix}")
        state["done"].set()
        ws.close()


def on_error(ws, error):
    log(f"on_error: {error!r}")


def on_close(ws, close_status_code, close_msg):
    elapsed = time.time() - state["response_start"] if state["response_start"] else None
    print()
    log(f"Connection closed. code={close_status_code} reason={close_msg!r}")
    log(f"Audio deltas received before close: {state['audio_delta_count']} "
        f"(total base64 chars: {state['audio_bytes_total']})")
    if elapsed is not None:
        log(f"Elapsed since response.create was sent: {elapsed:.2f}s")
    state["done"].set()


def main():
    if not API_KEY:
        print("Set OPENAI_API_KEY first, e.g.:  $env:OPENAI_API_KEY = 'sk-...'")
        sys.exit(1)

    log(f"Connecting to {URL} ...")
    ws = websocket.WebSocketApp(
        URL,
        header=[f"Authorization: Bearer {API_KEY}"],
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close,
    )
    thread = threading.Thread(target=ws.run_forever, kwargs={"ping_interval": 20})
    thread.daemon = True
    thread.start()

    if not state["done"].wait(timeout=30):
        log("Timed out waiting for a result after 30s - closing.")
        ws.close()


if __name__ == "__main__":
    main()
