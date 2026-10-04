# CLAUDE.md — Rules, Conventions & Workflow

This file holds the durable **rules, conventions, and workflow procedures**
for working in this repo. Historical narrative, design decisions and their
reasoning, bug discoveries, and the step-by-step build log all live in
[`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) instead — that file is the
project's journal; this one is the rulebook. When adding new content, keep
that split: a "why we chose X" or "what happened when we built step N" entry
goes in `PROJECT_OVERVIEW.md`; a standing "always do X" or "how the workflow
works" entry goes here.

## Testing Workflow

### Incremental isolation philosophy
Each roadmap step (tracked in `PROJECT_OVERVIEW.md`'s Development Roadmap) is
built and tested in isolation before the next one is added, so a failure is
always traceable to the newest piece.

### One active `test.ino` at a time
Arduino compiles a sketch folder as one unit — every `.ino` it contains gets
concatenated together — so more than one live `.ino` directly in `test/`
would collide (duplicate `setup()`/`loop()`). Only one test is ever "active"
at a time, named `test/test.ino` (matching the `test/` folder name, as
Arduino requires). Every other step's test sits alongside it as
`test/<NN><letter?>_<step_name>.ino.inactive` (step number first,
zero-padded, with a letter suffix for sub-steps — e.g. `02_440hz_A`,
`07b_gemini_setup_full`), invisible to the build.

### "Test passed" procedure
When Tomer says a test has passed, do **both** of the following (not just
one):
1. Rename the current `test/test.ino` to its descriptive
   `<NN><letter?>_<step_name>.ino.inactive` name per the naming convention
   above, and mark its roadmap checkbox `[x]` in `PROJECT_OVERVIEW.md` (with
   a note on what was confirmed).
2. Write the next not-yet-built roadmap step directly as the new
   `test/test.ino`, reusing the shared `test/*_utils.h`/`.cpp` modules
   rather than re-implementing what they already cover, and update that
   step's roadmap entry to reflect what was actually built (design notes,
   filename reference) — leave its checkbox unchecked until *that* one is
   confirmed passed in turn. Also write that step's "What to expect?"
   section (see below) — don't skip it because the code already speaks for
   itself.

If there's no already-planned next step, ask what it should be rather than
inventing one.

### "What to expect?" section
Every time a new or substantively-changed step is written as `test/test.ino`
(whether via the "test passed" procedure above, or generated standalone —
e.g. answering "generate/build the next step"), write a **"What to
expect?"** section immediately after presenting the code, both in the chat
reply and copied into that step's `PROJECT_OVERVIEW.md` roadmap entry (this
formalizes a pattern several earlier steps already used ad hoc). It has
three parts:
1. **What the test should do** — the expected sequence of behavior/output as
   it runs (connect, prompts, what to type/press, what should happen next),
   so a real run can be compared against it line by line.
2. **What to watch out for** — concrete, specific failure modes anticipated
   for *this* step's new code specifically (not a generic "something might
   go wrong"), framed explicitly as anticipated/unconfirmed rather than
   presented as settled fact — consistent with this file's "Don't assume API
   schema fields in advance" doctrine below applied to bugs, not just
   schemas.
3. **What determines PASS** — the exact signal that settles it, spelled out
   concretely: the literal `===== ... PASSED =====` banner where Code
   Convention #4 already covers it, or, when the step needs Tomer's own
   senses to judge (e.g. speaking into the mic, confirming audible playback,
   watching the LED actually change), the exact manual action and
   observation that counts as a pass.

### "The next step"
Whenever Tomer refers to "the next step" (or similar - "what's next",
"move to the next one"), read `PROJECT_OVERVIEW.md`'s Development Roadmap to
determine which step that actually is (the lowest-numbered unchecked `[ ]`
item, unless context points elsewhere) rather than asking or guessing.

### Shared test utilities
`.cpp`/`.h` files placed directly in `test/` are compiled alongside whichever
sketch is currently `test.ino` automatically (unlike `.ino` files, this is
the sanctioned way Arduino splits one sketch across multiple files). Reusable
plumbing — anything that isn't the actual LISTEN-cycle test logic under
test — belongs in one of these modules, not copy-pasted into every new test
or left inline in `test.ino`:
- `wifi_utils.h`/`.cpp` — Wi-Fi connect, SNTP time sync.
- `openai_realtime_utils.h`/`.cpp` — WSS connection setup, root CA, auth
  header, reconnect backoff, generic WebSocket event logging.
- `i2s_audio_utils.h`/`.cpp` — full-duplex mic+speaker I2S setup, gained mic
  reads, the playback ring buffer, mu-law decode.
- `realtime_json_utils.h`/`.cpp` — cheap raw-JSON scanning
  (`extractEventType()`/`findRawStringField()`) to avoid a full parse of
  large audio-delta events.
- `log_utils.h`/`.cpp` — the leveled logging macros (see Logging Doctrine
  below).
- `board_state_utils.h`/`.cpp` — the onboard LED and `set_board_state`
  tool-call handling.

`test.ino` itself should keep only what's actually under test: Wi-Fi/session
wiring, the LISTEN cycle's mechanics, the server-event-driven state machine,
and the PASS/FAIL/HALT verdict logic — i.e. whatever the current step's PASS
criterion actually depends on. New capabilities that aren't specific to the
current step's test go into a new or existing `*_utils` module instead.

All test sketches `#include` the single shared `test/config.h` rather than
each carrying its own copy of pins/credentials.

### Don't assume API schema fields in advance
Confirm exact endpoint URLs, event names, and schema fields against the
provider's *current* docs (or by testing) when a step is actually built,
rather than assuming them ahead of time. When guessing is unavoidable short
term, say so explicitly and flag it as unconfirmed rather than presenting it
as settled.

## Code Conventions

1. **Comments** — keep them short and meaningful; avoid restating what the
   code already says. Only add one when the *why* is non-obvious.
2. **Doc comments** — every function must have an idiomatic description
   comment (a standard-form doc comment, e.g. Doxygen-style `/** ... */` for
   Arduino/C++).
3. **No silent `switch` `default`** — no `switch` `default` case should go
   un-logged, even an "unhandled/unexpected" case should print something to
   Serial rather than silently `break`ing, so nothing goes unnoticed while
   debugging.
4. **Unambiguous test verdicts** — every test must print an unambiguous
   `TEST PASSED`/`FAILED` (or this project's `===== LISTEN #N
   PASSED/FAILED =====` form) once its outcome is known — never leave it to
   interpreting raw log lines. For tests that keep running/retrying
   afterward (e.g. an auto-reconnecting session), log the verdict once when
   it's first determined rather than repeating or contradicting it on every
   retry cycle, and reserve `FAILED` for a genuine, non-transient problem
   (e.g. a parse error on a real message) rather than an expected,
   already-handled retry (e.g. one disconnect that a backoff will retry).

## Naming & Namespacing

1. **Every symbol always goes inside a namespace** — globals, free
   functions, and types alike, not just "the ones that seem risky."
   Prefer one single project namespace (e.g. `esp32va`); a module-specific
   sub-namespace (e.g. `esp32va_log`) is fine when a module's identifiers
   benefit from their own grouping. If there's a strong argument against
   namespacing something in a specific case, raise it and get explicit
   sign-off before breaking this rule — don't just quietly leave something
   unnamespaced.
   **Standing exception**: Arduino's runtime calls `setup()` and `loop()`
   unqualified in the global namespace, so those two entry points can't be
   namespaced (short of wrapper indirection, which isn't worth it here) —
   this is a hard technical constraint, not a style call, so it doesn't need
   to be re-raised each time.
   **Why this isn't just defensive-programming polish**: a bare global
   named `g_log_level` once collided at the linker level with an
   identically-named internal symbol inside Espressif's precompiled
   `libnet80211.a` (the WiFi stack), silently clobbering it and producing a
   genuinely confusing bug (see `PROJECT_OVERVIEW.md` step 17). Namespacing
   is the actual fix for an already-hit bug class in this exact codebase,
   not a hypothetical concern.
2. **Symbol names must be descriptive.** If a name can't reasonably be made
   descriptive (e.g. a conventional short loop index, or a name forced by
   an external API/library), add a short comment at its declaration
   explaining what it means and why it isn't spelled out more descriptively.

## Serial Logging

### Style
Log what's happening on the board to Serial in a way that's descriptive for
both a human and an AI reading the log — clear, plain-language status/event
messages (not just terse codes), so the log is useful for debugging by eye
or by pasting into a conversation.

### Mechanism
Use the `LOG_ERROR`/`LOG_INFO`/`LOG_DEBUG` macros from `log_utils.h`, not raw
`Serial.print`/`println`/`printf`, for anything diagnostic. All three expand
to one generic `LOG(level, level_str, fmt, ...)` macro that prepends
`"[LEVEL] [func:line] "` (severity tag plus `__func__`/`__LINE__` — standard
C++11, there is no `__FUNC__` macro) before handing off to `Serial.printf`,
gated on the runtime `g_log_level` variable (`esp32va_log::g_log_level`,
namespaced — see "Naming & Namespacing" above). Do **not** add a
per-module tag like `[wifi]`/`[openai_playback]` — `__func__` plus a
descriptive function name already identifies the source.

`fmt` must be a string literal (it's concatenated with the `"[LEVEL]
[%s:%d] "` prefix at compile time) — never pass a computed/ternary format
string; branch and call the macro once per literal instead.

Default level is `LOG_LEVEL_INFO`. Bump to `LOG_LEVEL_DEBUG` locally
(`log_utils.cpp`) for deeper analysis.

### Severity doctrine
Each level is a cumulative superset of the one below it (`g_log_level >=
LOG_LEVEL_DEBUG` shows everything), so this is about what belongs at *each*
level, not what it excludes:
- **`ERROR`** — simple and concise. One line, states the problem, no
  protocol dumps or rationale paragraphs. A human should be able to scan a
  wall of `ERROR` lines and know what broke without re-reading.
- **`INFO`** — human-readable and non-noisy. The narrative a person watching
  Serial actually wants (connected, LISTEN #N started, got a reply,
  reconnected). Never repeats content already shown via raw streamed output
  (e.g. re-printing a transcript that already streamed live), and never
  fires more than a few times per LISTEN cycle — anything that could fire
  many times within one cycle (e.g. ring-buffer backpressure pause/resume)
  belongs at `DEBUG` instead.
- **`DEBUG`** — extremely verbose, written for analysis (by a human or an AI
  reading the log back) rather than for live reading. Deliberately "covers
  the actual info": everywhere `INFO`/`ERROR`'s wording is trimmed or a
  message is demoted out of `INFO` entirely, the fuller/more technical
  version lives at `DEBUG`, so nothing is ever truly lost — only
  deprioritized out of the default reading experience.

**Never gated behind a log level** (always plain `Serial.print`/`println`/
`printf`, no level tag): the `PASSED`/`FAILED`/`HALTED` verdict banners and
their immediate "Type LISTEN..." follow-up hints (per Code Convention #4
above), and raw streamed content (the model's transcript text as it streams
in, Wi-Fi/SNTP wait-dot progress indicators) — none of that is diagnostic
noise to be filtered.

## Engineering Constraints

- **No PSRAM (520KB SRAM total)**: TLS + WebSocket framing + JSON parsing of
  base64-encoded audio are all RAM-hungry, and audio sent/received as base64
  inside JSON is a further 33% size inflation. Stream audio in small chunks
  (20–50ms) and parse JSON incrementally — never buffer a full utterance or
  full reply in RAM. Prefer static/reusable buffers over per-message
  allocation to avoid heap fragmentation in long-running sessions.
- **Never call a blocking send/write from inside a WebSocket event
  callback** — `webSocket.sendTXT()` from inside `onEvent()` can deadlock the
  WebSockets (Links2004) library (it fires from within its own internal
  send/receive processing), and `i2s_write()` can block waiting for DMA
  buffer space. Set a flag in the callback and do the actual call from the
  next `loop()` iteration instead.
- **`config.h` holds real credentials** (Wi-Fi password, API keys) — never
  commit it. Add it to `.gitignore` before this project is ever put under
  git/pushed to a remote.
- **Debounce any digital input with a saturating integrator** (sample at a
  fixed real-time cadence, nudge a counter up/down by 1 per sample, act only
  once it saturates), not a "reset a stability timer on every raw
  transition" debounce — the latter can be defeated by continuous/periodic
  noise faster than its stability window (every glitch resets the timer, so
  the window can almost never actually elapse clean), starving *every*
  edge, real presses included, not just producing occasional false
  positives. Learned from the BOOT-button click-to-record feature (steps
  19/20a/20b, since removed per Tomer's decision — see `PROJECT_OVERVIEW.md`)
  — worth re-applying if a future physical-input feature (e.g. the Todo
  list's push-to-talk idea) needs debouncing.
  **Caveat, corrected 2026-09-20**: a prior version of this entry stated as
  confirmed fact that GPIO0 "picks up periodic electrical noise once Wi-Fi
  is active." That was an unverified guess presented with more confidence
  than it earned — Tomer pushed back, correctly (see `PROJECT_OVERVIEW.md`
  step 20a's retraction and step 20b's re-investigation, which was never
  concluded — the BOOT-button feature was dropped before it was settled).
  Don't cite that specific electrical explanation as settled; the
  integrator-over-timer debounce advice above stands on its own as a
  reasonable general practice regardless.

## Build Requirements

External libraries (install via the Arduino IDE's Library Manager) needed to
compile the sketches in this project. Everything else used (`WiFi.h`,
`driver/i2s.h`, `math.h`, `stdint.h`) ships with the ESP32 Arduino core
itself. See `PROJECT_OVERVIEW.md`'s Dependencies table for the full
library/version/history breakdown; keep that table current when a sketch
first pulls in a new external library.

## Tools

**`tools/serial_logger.ps1`**: two-way ESP32 Serial Monitor replacement —
echoes everything the board sends to both the console and a timestamped log
file, and forwards typed lines (e.g. `LISTEN`) to the board, so a full test
run (both directions) is saved instead of copy-pasted out of the Arduino
Serial Monitor. Run from PowerShell as (adjust `-Port` to whichever COM port
the board is on):
```
powershell -ExecutionPolicy Bypass -File "C:\Users\tomer\Claude\Projects\ESP32_Voice_Assistant\tools\serial_logger.ps1" -Port COM9
```
The `-ExecutionPolicy Bypass` is needed because this script isn't signed and
the default PowerShell execution policy blocks unsigned `.ps1` files — plain
`.\tools\serial_logger.ps1 -Port COM9` from inside an already-running
PowerShell session may or may not hit that block depending on the current
policy, so the full `powershell -ExecutionPolicy Bypass -File ...` form is
the one to reach for.

### "Read log"
When Tomer says "read log" (or similar - "read the log", "check the log")
with no filename given, go to `tools/` and read the most recently created
`esp32_log_<yyyy-MM-dd_HH-mm-ss>.txt` file (the default output of
`serial_logger.ps1` above) — the timestamp in the filename sorts
chronologically, so the lexicographically-last one is also the newest, but
confirm against actual file modification time rather than filename alone if
there's ever a mismatch. If Tomer names a specific log file, read that one
instead.
