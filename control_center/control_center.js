"use strict";

// ===== Settings =====

const COMMAND_TIMEOUT_MS = 5000;  // LIST and DELETE answers
const DELETE_ALL_TIMEOUT_MS = 30000;
// The board's limits (MAX_KEPT_RECORDINGS ... VAD_HANGOVER_MAX_MS) are only
// defaults: the page uses `limits` (below), which the board's @@LIMITS replaces.
const MAX_KEPT_RECORDINGS = 5;  // mirrors parameters.h's kMaxKeptRecordings - display only
const BOARD_MIC_GAIN = 2;       // mirrors parameters.h's kMicSoftwareGain (x4 until 2026-10-06) - for "what OpenAI hears"
const GET_START_TIMEOUT_MS = 5000;
const GET_STALL_MS = 2000;  // a GET's bytes stop arriving for this long: the transfer is given up
// What a board restart prints first - the ESP32 ROM's "rst:0x.. (POWERON_RESET)"
// and the firmware's own first status line. Seen inside a GET's bytes, the
// board restarted mid-transfer: the rest is boot output, not the recording.
const RESTART_SIGNATURES = ["rst:0x", "@@STATE booting"].map((text) => new TextEncoder().encode(text));
const BUSY_RETRY_MS = 1500;
const BUSY_RETRY_LIMIT = 12;
const MAX_LOG_LINES_SHOWN = 2000;
const LOG_FILE_FLUSH_MS = 2000;
const MAX_UNTERMINATED_LINE_BYTES = 4096;  // flush a line that never gets its newline (e.g. boot-time noise)
const SILENCE_WINDOW_S = 0.02;
const SILENCE_THRESHOLD_DBFS = -45;
const PAUSE_REPORT_MIN_S = 0.25;
const ENVELOPE_POINTS = 600;
const PCM16_FULL_SCALE = 32768;
const USB_CHIP_NAMES = { 0x10c4: "CP210x", 0x1a86: "CH340/CH9102" };
const SAFE_FILE_NAME = /^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,30}$/;  // a plain name, no path

// Status and controls (step 28) - the board's side is ESP32/status_report_utils.h.
const STATUS_TAGS = new Set(["@@STAGE", "@@STATE", "@@RESULT", "@@MIC_LEVEL", "@@HEARD", "@@SAID", "@@WIFI",
                             "@@LED", "@@VOLUME", "@@LIGHT", "@@BEEP", "@@VAD", "@@VAD_LEVEL", "@@VAD_EVENT",
                             "@@TIMING", "@@BOOT_TIMING", "@@TIMER", "@@CLOCK", "@@WIFI_NET", "@@BUTTON",
                             "@@SOUNDS", "@@ALIVE", "@@LIMITS", "@@OPENAI_KEY"]);
const STATUS_REQUEST_DELAY_MS = 400;   // after connecting, ask for the full picture
const NO_STATUS_HINT_MS = 4000;        // no @@STATE by then: the board runs older firmware
const BOARD_SILENT_MS = 15000;         // no line from the board for this long (it sends @@ALIVE every 5 s): "not responding"
const ALIVE_CHECK_MS = 1000;
const BOARD_SILENT_TITLE = `Board not responding - no report for ${BOARD_SILENT_MS / 1000} s.`;
const TALK_START_TIMEOUT_MS = 2500;    // LISTEN sent but no "listening" yet: unlock Talk again
const MAX_ASK_BYTES = 200;             // mirrors parameters.h's kMaxTypedQuestionBytes - the board cuts a longer question
const MAX_SSID_BYTES = 32;             // mirrors wifi_utils.cpp's kMaxSsidLength
const MIN_PASSWORD_BYTES = 8;          // WPA2 - wifi_utils.cpp checks the same
const MAX_PASSWORD_BYTES = 64;         // mirrors wifi_utils.cpp's kMaxPasswordLength
const WIFI_ACK_TIMEOUT_MS = 4000;      // WIFI_PASSWORD sent but no @@WIFI_NET saved / "ignored" by then
const WIFI_ACK_BOOT_TIMEOUT_MS = 40000;  // the same while booting: setup() reads no commands in its Wi-Fi (20 s) and clock (15 s) waits
const MIN_API_KEY_BYTES = 20;          // mirrors openai_realtime_utils.h's kMinApiKeyLength
const MAX_API_KEY_BYTES = 200;         // mirrors openai_realtime_utils.h's kMaxApiKeyLength
const API_KEY_PATTERN = /^sk-[A-Za-z0-9_-]+$/;  // what the board takes - openai_realtime_utils.cpp's isPlausibleApiKey()
// The player and the board's mic (LAPTOP_PLAYBACK - ESP32/hands_free_utils.h).
const PLAYBACK_HOLD_MARGIN_MS = 1500;  // LAPTOP_PLAYBACK on <ms>: the recording's time left, plus this
const PLAYBACK_HOLD_RESEND_MS = 500;   // a hold ending this close to the one already sent isn't sent again
const ASK_ECHO_TIMEOUT_MS = 20000;     // a typed question the board never echoed back stops counting as pending
const TOOL_MIN_SHOWN_MS = 1200;        // a tool call is over in milliseconds - keep it readable
const ERROR_SHOWN_MS = 3000;           // how long a failed LISTEN shows as an error
const CONTROL_SEND_DELAY_MS = 250;     // slider changes are sent once dragging pauses
const MAX_BUBBLES = 60;               // conversation items kept on screen (bubbles and action notes)
const LIGHT_SENSOR_MAX = 6142;         // mirrors light_sensor_utils.h's kMaxLightSensorValue
// The light sensor's auto day-cycle (a mock of a real LDR - see stepDayCycle()).
const DAY_CYCLE_PERIOD_MS = 120000;    // one simulated 24 h day takes 2 minutes (the Simulate menu's title says so)
const DAY_CYCLE_SEND_MS = 1500;        // a LIGHT command this often - 18 simulated minutes apart
const DAY_CYCLE_START_HOUR = 4;        // just before dawn, so a sunrise shows within the first half minute
const DAY_CYCLE_NIGHT_LEVEL = 0.03;    // darkest and brightest level, as parts of the scale (limits.light_max)
const DAY_CYCLE_NOON_LEVEL = 0.92;
const DAY_CYCLE_STEEPNESS = 4;         // how sharp sunrise and sunset are: "evening" ~4:15-6:00 and 18:00-19:45 at the default thresholds
const DAY_CYCLE_NOISE_COUNTS = 5;      // a real ADC reading jitters by a few counts, x1.5 on the board's scale (light_sensor_utils.cpp)
const MIC_METER_FLOOR_DBFS = -60;
// Hands-free (step 30) - the board's side is ESP32/hands_free_utils.h.
const VAD_METER_MIN_DBFS = -90;        // the voice-band level is the mic's own, before the gain: quieter than @@MIC_LEVEL
const VAD_METER_MAX_DBFS = -10;
const MAX_UTTERANCE_S = 10;            // mirrors vad_utils.h's kMaxUtteranceMs
const VAD_SENSITIVITY_MIN = 1;         // mirrors vad_utils.h's kMinSensitivity
const VAD_SENSITIVITY_MAX = 5;         // mirrors vad_utils.h's kMaxSensitivity
const VAD_HANGOVER_MIN_MS = 600;       // mirrors parameters.h's kMinVadHangoverMs
const VAD_HANGOVER_MAX_MS = 1500;      // mirrors parameters.h's kMaxVadHangoverMs
/** The limits as built in, keyed as in the board's @@LIMITS line. */
const DEFAULT_LIMITS = Object.freeze({
  max_ask_bytes: MAX_ASK_BYTES, max_ssid_bytes: MAX_SSID_BYTES,
  min_password_bytes: MIN_PASSWORD_BYTES, max_password_bytes: MAX_PASSWORD_BYTES,
  min_api_key_bytes: MIN_API_KEY_BYTES, max_api_key_bytes: MAX_API_KEY_BYTES,
  max_kept_recordings: MAX_KEPT_RECORDINGS, mic_gain: BOARD_MIC_GAIN, light_max: LIGHT_SENSOR_MAX,
  max_utterance_ms: MAX_UTTERANCE_S * 1000,
  vad_sensitivity_min: VAD_SENSITIVITY_MIN, vad_sensitivity_max: VAD_SENSITIVITY_MAX,
  vad_hangover_min_ms: VAD_HANGOVER_MIN_MS, vad_hangover_max_ms: VAD_HANGOVER_MAX_MS,
});
/** The limits in use: the board's own once it sent @@LIMITS (applyLimits()),
    the defaults until then - and again after every connect, disconnect or
    replay (resetBoardStatus()). */
const limits = { ...DEFAULT_LIMITS };
const VAD_NOTE_DEFAULT = "Higher sensitivity hears quieter voices - and more of the room's noise.";
const PLAYBACK_VAD_NOTE = "Paused while a recording plays here - the board's mic would take it for a question.";
const PLAYER_PAUSED_FOR_LISTEN = "Paused - the board is recording a question, and its mic would hear this. Press play again once it's done.";
const DISCARD_TEXT = {                 // @@VAD_EVENT discarded <reason> - vad_utils.h's DiscardReason
  short: "too short to be speech (a click, a knock?)",
  unvoiced: "no voice in it (a cough, a laugh, running water?)",
  steady: "too steady to be speech (a machine, a bell?)",
};
const TOOL_LABELS = { set_board_state: "Switching the LED", get_time_of_day: "Reading the light sensor",
                      set_volume: "Changing the volume", get_current_time: "Checking the clock",
                      set_timer: "Setting a timer", get_board_status: "Checking the board",
                      set_hands_free: "Switching hands-free" };
const STATE_TEXT = {
  offline: ["Not connected", "Connect the board to start."],
  unknown: ["Connected", "Waiting for the board to report its state…"],
  booting: ["Starting up", "Running the startup checks."],
  ready: ["Ready", "Press Talk (or Space) and ask something."],
  armed: ["Waiting for speech", "Hands-free: just start talking. Talk still records 3 s."],
  listening: ["Listening", "Recording…"],
  processing: ["Processing", "Waiting for OpenAI's answer."],
  tool: ["Processing", "Running an action on the board."],
  speaking: ["Speaking", "Playing the reply on the speaker."],
  saving: ["Speaking", "Saving the reply to the SD card."],
  reconnecting: ["Reconnecting", "Lost the connection to OpenAI."],
  netdown: ["Offline", "No connection - the board keeps retrying. The timer and the controls still work."],
  halted: ["Stopped", "The board stopped - see the log."],
  error: ["Error", ""],
};
const STEP_OF_STATE = { ready: "ready", armed: "armed", listening: "listening", processing: "processing", tool: "processing",
                        speaking: "speaking", saving: "speaking" };
const IDLE_STATES = new Set(["ready", "armed"]);  // the board takes a LISTEN in these

/** The two kinds of recording the board keeps, told apart by extension
    (see the board's recording_folder_utils.h). */
const RECORDING_KINDS = {
  reply: {
    label: "Replies", noun: "reply", nounPlural: "replies", folderArgument: "replies",
    sampleRateHz: 8000, bytesPerSample: 1,  // G.711 mu-law
    fullScaleMagnitude: 32124,              // mu-law's loudest code
    decode: decodeUlaw,
  },
  command: {
    label: "Voice commands", noun: "voice command", nounPlural: "voice commands", folderArgument: "commands",
    sampleRateHz: 24000, bytesPerSample: 2,  // the mic's own 16-bit samples, before the board's gain
    fullScaleMagnitude: 32767,
    decode: decodePcm16,
  },
};

function kindOfFile(name) {
  return /\.pcm$/i.test(name) ? "command" : "reply";
}

// ===== Audio: decoding, WAV, CRC-32, analysis =====

/** Same ITU-T G.711 decode as the board's ulawToLinearPcm16(). */
const ULAW_TO_PCM16 = new Int16Array(256).map((_, code) => {
  const inverted = ~code & 0xff;
  let magnitude = ((inverted & 0x0f) << 3) + 0x84;
  magnitude <<= (inverted & 0x70) >> 4;
  return inverted & 0x80 ? 0x84 - magnitude : magnitude - 0x84;
});

function decodeUlaw(bytes) {
  const pcm = new Int16Array(bytes.length);
  for (let i = 0; i < bytes.length; i++) pcm[i] = ULAW_TO_PCM16[bytes[i]];
  return pcm;
}

/** Raw 16-bit little-endian PCM bytes -> samples. */
function decodePcm16(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const pcm = new Int16Array(bytes.length >> 1);
  for (let i = 0; i < pcm.length; i++) pcm[i] = view.getInt16(i * 2, true);
  return pcm;
}

/** The board's software gain: multiply and clamp to 16 bits, exactly as
    readMicChunkGained() does before sending audio to OpenAI. */
function applyBoardGain(pcm) {
  return pcm.map((sample) => Math.max(-32768, Math.min(32767, sample * limits.mic_gain)));
}

/** 16-bit mono PCM -> a WAV file's bytes. */
function makeWav(pcm, sampleRateHz) {
  const view = new DataView(new ArrayBuffer(44 + pcm.length * 2));
  const writeText = (offset, text) => [...text].forEach((c, i) => view.setUint8(offset + i, c.charCodeAt(0)));
  writeText(0, "RIFF");
  view.setUint32(4, 36 + pcm.length * 2, true);
  writeText(8, "WAVE");
  writeText(12, "fmt ");
  view.setUint32(16, 16, true);                 // fmt chunk size
  view.setUint16(20, 1, true);                  // PCM
  view.setUint16(22, 1, true);                  // mono
  view.setUint32(24, sampleRateHz, true);
  view.setUint32(28, sampleRateHz * 2, true);   // bytes per second
  view.setUint16(32, 2, true);                  // bytes per frame
  view.setUint16(34, 16, true);                 // bits per sample
  writeText(36, "data");
  view.setUint32(40, pcm.length * 2, true);
  pcm.forEach((sample, i) => view.setInt16(44 + i * 2, sample, true));
  return view.buffer;
}

/** Standard CRC-32 - the same variant as the board's crc32Update() and zlib. */
const CRC32_TABLE = new Uint32Array(256).map((_, n) => {
  let crc = n;
  for (let bit = 0; bit < 8; bit++) crc = crc & 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
  return crc >>> 0;
});
function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC32_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function toDbfs(level) {
  return level > 0 ? 20 * Math.log10(level / PCM16_FULL_SCALE) : null;
}

/** Level stats for one recording plus a min/max envelope for drawing it.
    Pauses are silent stretches inside the sound. For a reply, the file holds
    the bytes as received, not their timing, so a pause is in OpenAI's audio
    itself - not a network stall on the board. */
function analyze(pcm, sampleRateHz, fullScaleMagnitude) {
  const count = pcm.length;
  const result = {
    durationS: count / sampleRateHz, peakDbfs: null, rmsDbfs: null, fullScaleSamples: 0,
    leadingSilenceS: 0, trailingSilenceS: 0, pauses: [], envelope: [],
  };
  if (!count) return result;

  let peak = 0;
  let sumOfSquares = 0;
  for (const sample of pcm) {
    const magnitude = Math.abs(sample);
    if (magnitude > peak) peak = magnitude;
    if (magnitude >= fullScaleMagnitude) result.fullScaleSamples++;
    sumOfSquares += sample * sample;
  }
  result.peakDbfs = toDbfs(peak);
  result.rmsDbfs = toDbfs(Math.sqrt(sumOfSquares / count));

  const window = Math.round(sampleRateHz * SILENCE_WINDOW_S);
  const threshold = PCM16_FULL_SCALE * 10 ** (SILENCE_THRESHOLD_DBFS / 20);
  const windowIsSilent = [];
  for (let start = 0; start < count; start += window) {
    const end = Math.min(start + window, count);
    let windowSum = 0;
    for (let i = start; i < end; i++) windowSum += pcm[i] * pcm[i];
    windowIsSilent.push(Math.sqrt(windowSum / (end - start)) < threshold);
  }
  const firstSound = windowIsSilent.indexOf(false);
  const lastSound = windowIsSilent.lastIndexOf(false);
  if (firstSound < 0) {
    result.leadingSilenceS = result.durationS;
  } else {
    result.leadingSilenceS = firstSound * SILENCE_WINDOW_S;
    result.trailingSilenceS = (windowIsSilent.length - 1 - lastSound) * SILENCE_WINDOW_S;
    let runStart = -1;
    for (let i = firstSound; i <= lastSound; i++) {
      if (windowIsSilent[i] && runStart < 0) {
        runStart = i;
      } else if (!windowIsSilent[i] && runStart >= 0) {
        const lengthS = (i - runStart) * SILENCE_WINDOW_S;
        if (lengthS >= PAUSE_REPORT_MIN_S - 1e-9) result.pauses.push({ startS: runStart * SILENCE_WINDOW_S, lengthS });
        runStart = -1;
      }
    }
  }

  const bucket = Math.max(1, Math.ceil(count / ENVELOPE_POINTS));
  for (let start = 0; start < count; start += bucket) {
    let low = 0;
    let high = 0;
    for (let i = start; i < Math.min(start + bucket, count); i++) {
      if (pcm[i] < low) low = pcm[i];
      if (pcm[i] > high) high = pcm[i];
    }
    result.envelope.push([low / PCM16_FULL_SCALE, high / PCM16_FULL_SCALE]);
  }
  return result;
}

// ===== Serial link to the board =====

class BoardError extends Error {
  /** kind: "busy" (board answered @@BUSY), "unavailable" (not connected) or "transfer". */
  constructor(kind, message) {
    super(message);
    this.kind = kind;
  }
}

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

/** Resolves true if promise settles within ms, false otherwise. */
function settlesWithin(promise, ms) {
  let timer;
  return Promise.race([
    promise.then(() => true),
    new Promise((done) => { timer = setTimeout(() => done(false), ms); }),
  ]).finally(() => clearTimeout(timer));
}

/** Where in data[from, to) a board-restart signature (RESTART_SIGNATURES)
    starts, or -1. A signature split across two chunks isn't seen - the stall
    watchdog or the time budget still ends that transfer. */
function findRestartSignature(data, from, to) {
  let first = -1;
  for (const signature of RESTART_SIGNATURES) {
    const end = first < 0 ? to : first;  // only an earlier match matters
    for (let at = data.indexOf(signature[0], from); at >= 0 && at < end; at = data.indexOf(signature[0], at + 1)) {
      if (startsWithBytes(data, at, signature)) {
        first = at;
        break;
      }
    }
  }
  return first;
}

/** True if data holds the bytes of signature starting at index at. */
function startsWithBytes(data, at, signature) {
  if (at + signature.length > data.length) return false;
  for (let index = 0; index < signature.length; index++) {
    if (data[at + index] !== signature[index]) return false;
  }
  return true;
}

/** One LIST or GET in flight, filled in as the board's reply streams in. */
class Transaction {
  constructor(kind, fileName) {
    this.kind = kind;
    this.fileName = fileName;
    this.files = [];
    this.size = null;
    this.data = null;
    this.boardCrc = null;
    this.error = null;
    this.started = deferred();  // GET: @@GET_BEGIN seen (or finished early)
    this.done = deferred();
  }
  finish(error = null) {
    this.error = error;
    this.started.resolve();
    this.done.resolve();
  }
}

/** Owns the open port. The read loop splits the stream into log lines and
    "@@" protocol replies, switching to counted binary reads during a GET;
    list()/get() run one at a time. */
class BoardLink {
  constructor({ onLine, onNote, onConnectionChange, onListAnnounced, onRecordingSaved, onRecordingDeleted,
                onTransferProgress, onStatus, onActivity }) {
    this.onStatus = onStatus;  // (tag without "@@", rest of the line) for the status lines (STATUS_TAGS)
    this.onActivity = onActivity;  // () whenever anything arrives from the board
    this.lastReceivedAt = 0;       // performance.now() of the last bytes from the board
    this.onTransferProgress = onTransferProgress;  // (bytes received, total) while a GET's bytes arrive
    this.onLine = onLine;
    this.onNote = onNote;
    this.onConnectionChange = onConnectionChange;
    this.onListAnnounced = onListAnnounced;        // a full list the board sent unprompted (at boot, or a late LIST answer)
    this.onRecordingSaved = onRecordingSaved;      // "@@SAVED <name> <size>"
    this.onRecordingDeleted = onRecordingDeleted;  // "@@DELETED <name>"
    this.announcedFiles = null;                // collects an unprompted list block until its @@LIST_END
    this.port = null;
    this.writer = null;
    this.reader = null;
    this.baud = null;
    this.closing = false;
    this.pending = new Uint8Array(0);
    this.binary = null;  // {remaining, target, lastProgressAt} while a GET's raw bytes are arriving
    this.resync = false; // a GET was given up mid-stream: skip to the next newline before reading lines again
    this.transaction = null;
    this.queue = Promise.resolve();
    this.decoder = new TextDecoder();
    this.encoder = new TextEncoder();
  }

  get connected() { return this.writer !== null; }

  async open(port, baud) {
    await port.open({ baudRate: baud, bufferSize: 65536 });
    // Release DTR/RTS so the board's auto-reset circuit stays idle. Opening
    // may already have pulsed them on some setups, restarting the board once.
    await port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    this.port = port;
    this.baud = baud;
    this.closing = false;
    this.writer = port.writable.getWriter();
    this.onConnectionChange(true);
    this.readLoop();
  }

  async close() {
    this.closing = true;
    if (this.reader) await this.reader.cancel().catch(() => {});
  }

  async readLoop() {
    // A recoverable error (framing, buffer overrun) ends one reader and gives
    // port.readable a fresh stream; a lost device leaves it null.
    while (this.port && this.port.readable && !this.closing) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value) this.consume(value);
        }
      } catch (error) {
        if (!this.closing) this.onNote(`Serial read error: ${error.message}`);
      } finally {
        this.reader.releaseLock();
        this.reader = null;
      }
    }
    await this.teardown();
  }

  async teardown() {
    if (this.writer) {
      try { this.writer.releaseLock(); } catch (_) { /* a write was still pending - it fails, which is fine */ }
      this.writer = null;
    }
    if (this.port) {
      await this.port.close().catch(() => {});
      this.port = null;
    }
    this.pending = new Uint8Array(0);
    this.binary = null;
    this.resync = false;
    this.announcedFiles = null;
    if (this.transaction) this.transaction.finish("the serial port closed mid-transfer");
    this.onConnectionChange(false, this.closing);
  }

  consume(chunk) {
    this.lastReceivedAt = performance.now();
    if (this.onActivity) this.onActivity();
    let data = chunk;
    if (this.pending.length) {
      data = new Uint8Array(this.pending.length + chunk.length);
      data.set(this.pending);
      data.set(chunk, this.pending.length);
    }
    let offset = 0;
    while (offset < data.length) {
      if (this.binary) {
        const take = Math.min(this.binary.remaining, data.length - offset);
        const restartAt = findRestartSignature(data, offset, offset + take);
        if (restartAt >= 0) {
          this.abortBinary("the board restarted during the transfer");
          offset = restartAt;  // its boot output is read as lines again
          continue;
        }
        this.binary.lastProgressAt = performance.now();
        if (this.binary.target) {
          this.binary.target.set(data.subarray(offset, offset + take), this.binary.target.length - this.binary.remaining);
        }
        this.binary.remaining -= take;
        offset += take;
        if (this.binary.target && this.onTransferProgress) {
          this.onTransferProgress(this.binary.target.length - this.binary.remaining, this.binary.target.length);
        }
        if (this.binary.remaining === 0) this.binary = null;
        continue;
      }
      if (this.resync) {
        const newlineAt = data.indexOf(10, offset);
        if (newlineAt < 0) {
          offset = data.length;
          break;
        }
        offset = newlineAt + 1;
        this.resync = false;
        continue;
      }
      let newlineAt = data.indexOf(10, offset);
      if (newlineAt < 0) {
        if (data.length - offset <= MAX_UNTERMINATED_LINE_BYTES) break;
        newlineAt = data.length;
      }
      const line = this.decoder.decode(data.subarray(offset, newlineAt)).replace(/\r+$/, "");
      offset = newlineAt + 1;
      if (line.startsWith("@@")) this.handleProtocolLine(line);
      else this.onLine(line);
    }
    this.pending = offset < data.length ? data.slice(offset) : new Uint8Array(0);
  }

  /** Gives up a GET's binary stream (the board restarted or went quiet) and
      fails the GET waiting for it, so status lines are read again at once. */
  abortBinary(reason) {
    const wanted = this.binary && this.binary.target;
    this.binary = null;
    if (wanted && this.transaction && this.transaction.kind === "GET") this.transaction.finish(reason);
    this.onNote(`Download stopped: ${reason}.`);
  }

  handleProtocolLine(line) {
    const parts = line.split(/\s+/);
    const tag = parts[0];
    const transaction = this.transaction;

    // Status lines never answer a LIST/GET - handled before the transaction logic.
    if (STATUS_TAGS.has(tag)) {
      this.onStatus(tag.slice(2), line.slice(tag.length + 1));
      return;
    }

    if (tag === "@@GET_BEGIN" && parts.length === 3 && /^\d+$/.test(parts[2])) {
      const size = Number(parts[2]);
      let target = null;
      if (transaction && transaction.kind === "GET" && transaction.fileName === parts[1]) {
        transaction.size = size;
        transaction.data = new Uint8Array(size);
        target = transaction.data;
        transaction.started.resolve();
      } else {
        this.onNote(`Skipping ${size} bytes of a GET this page didn't ask for (${parts[1]}).`);
      }
      if (size > 0) this.binary = { remaining: size, target, lastProgressAt: performance.now() };
      return;
    }

    if (tag === "@@SAVED" && parts.length === 3 && /^\d+$/.test(parts[2])) {
      this.onRecordingSaved({ name: parts[1], size: Number(parts[2]) });
      return;
    }
    if (tag === "@@DELETED" && parts.length === 2) {
      if (transaction && transaction.kind === "DELETE" && transaction.fileName === parts[1]) transaction.finish();
      this.onRecordingDeleted(parts[1]);
      return;
    }
    // A list block answers the pending LIST if there is one; otherwise it was
    // sent unprompted (at boot) or arrived after its LIST timed out - either
    // way it's the current list.
    if (tag === "@@LIST_BEGIN" || tag === "@@FILE" || tag === "@@LIST_END") {
      const request = transaction && transaction.kind === "LIST" ? transaction : null;
      if (tag === "@@LIST_BEGIN") {
        if (request) request.files = [];
        else this.announcedFiles = [];
      } else if (tag === "@@FILE") {
        const rest = line.slice("@@FILE ".length);
        const splitAt = rest.lastIndexOf(" ");
        const file = { name: rest.slice(0, splitAt), size: Number(rest.slice(splitAt + 1)) };
        if (request) request.files.push(file);
        else if (this.announcedFiles) this.announcedFiles.push(file);
      } else if (request) {
        request.finish();
      } else if (this.announcedFiles) {
        this.onListAnnounced(this.announcedFiles);
        this.announcedFiles = null;
      }
      return;
    }

    if (!transaction) {
      this.onLine(line);  // e.g. a late @@BUSY for a request that already timed out
      return;
    }
    if (tag === "@@BUSY") {
      transaction.finish("busy");
    } else if (tag === "@@LIST_ERROR") {
      transaction.finish(line.slice("@@LIST_ERROR ".length));
    } else if (tag === "@@GET_END" && transaction.kind === "GET" && parts.length === 3) {
      transaction.boardCrc = parseInt(parts[2], 16) >>> 0;
      transaction.finish();
    } else if (tag === "@@GET_ERROR") {
      transaction.finish(line.slice("@@GET_ERROR ".length));
    } else if (tag === "@@DELETE_ERROR") {
      transaction.finish(line.slice("@@DELETE_ERROR ".length));
    } else if (tag === "@@DELETE_ALL_ERROR") {
      transaction.finish(line.slice("@@DELETE_ALL_ERROR ".length));
    } else if (tag === "@@DELETE_ALL_DONE" && transaction.kind === "DELETE_ALL" && parts.length === 3) {
      const left = Number(parts[2]);
      transaction.finish(left > 0 ? `${left} recording${left === 1 ? "" : "s"} couldn't be deleted - see the log` : null);
    } else {
      this.onLine(line);
    }
  }

  async send(text) {
    if (!this.connected) throw new BoardError("unavailable", "The board isn't connected.");
    await this.writer.write(this.encoder.encode(text + "\n"));
  }

  /** Queues a LIST/GET behind any already running, so only one is ever in flight. */
  run(kind, command, fileName) {
    const job = this.queue.then(() => this.runNow(kind, command, fileName));
    this.queue = job.catch(() => {});
    return job;
  }

  async runNow(kind, command, fileName) {
    const transaction = new Transaction(kind, fileName);
    this.transaction = transaction;
    try {
      await this.send(command);
      let finished;
      if (kind !== "GET") {
        const timeoutMs = kind === "DELETE_ALL" ? DELETE_ALL_TIMEOUT_MS : COMMAND_TIMEOUT_MS;
        finished = await settlesWithin(transaction.done.promise, timeoutMs);
      } else {
        finished = await settlesWithin(transaction.started.promise, GET_START_TIMEOUT_MS);
        if (finished) {
          // The board writes at wire speed (10 bits per byte): a quarter more
          // for margin, plus slack. A stall is caught much sooner.
          const budgetMs = ((transaction.size || 0) * 10 / this.baud) * 1250 + 2000;
          const watchdog = setInterval(() => {
            if (this.binary && this.binary.target === transaction.data &&
                performance.now() - this.binary.lastProgressAt > GET_STALL_MS) {
              this.abortBinary(`no data from the board for ${GET_STALL_MS / 1000} s`);
              this.resync = true;
            }
          }, 500);
          try {
            finished = await settlesWithin(transaction.done.promise, budgetMs);
          } finally {
            clearInterval(watchdog);
          }
        }
      }
      if (!finished) {
        if (this.binary) this.resync = true;  // the rest of the recording may still arrive - not as lines
        this.binary = null;
        throw new BoardError("transfer", `No complete answer to ${command} - timed out.`);
      }
    } finally {
      this.transaction = null;
    }
    if (transaction.error === "busy") {
      throw new BoardError("busy", "The board is busy (a reply is in progress, playing, or being saved).");
    }
    if (transaction.error) throw new BoardError("transfer", transaction.error);
    return transaction;
  }

  async list() {
    return (await this.run("LIST", "LIST")).files;
  }

  /** Deletes one recording from the SD card. Success also reaches
      onRecordingDeleted, the same as a DELETE typed in the command box. */
  async deleteRecording(fileName) {
    await this.run("DELETE", `DELETE ${fileName}`, fileName);
  }

  /** Deletes every recording in one folder ("replies" or "commands"); each
      one reaches onRecordingDeleted as the board confirms it. Throws if any
      were left. */
  async deleteAll(folderArgument) {
    await this.run("DELETE_ALL", `DELETE_ALL ${folderArgument}`);
  }

  /** Downloads one recording and checks it against the board's CRC. */
  async get(fileName) {
    const transaction = await this.run("GET", `GET ${fileName}`, fileName);
    const data = transaction.data || new Uint8Array(0);
    const crc = crc32(data);
    if (crc !== transaction.boardCrc) {
      throw new BoardError("transfer", "CRC mismatch - the transfer was corrupted, try again.");
    }
    return { data, crc };
  }
}

// ===== Page state =====

const elements = {
  baud: document.getElementById("baud"),
  connect: document.getElementById("connect"),
  status: document.getElementById("status"),
  unsupported: document.getElementById("unsupported"),
  refresh: document.getElementById("refresh"),
  deleteAll: document.getElementById("delete-all"),
  listStatus: document.getElementById("list-status"),
  list: document.getElementById("recording-list"),
  tabs: document.querySelectorAll(".tab"),
  gainOption: document.getElementById("gain-option"),
  applyGain: document.getElementById("apply-gain"),
  playerHeading: document.getElementById("player-heading"),
  playerStatus: document.getElementById("player-status"),
  download: document.getElementById("download"),
  deleteButton: document.getElementById("delete"),
  waveform: document.getElementById("waveform"),
  audio: document.getElementById("audio"),
  stats: document.getElementById("stats"),
  pauseList: document.getElementById("pause-list"),
  log: document.getElementById("log"),
  logTarget: document.getElementById("log-target"),
  saveLog: document.getElementById("save-log"),
  downloadLog: document.getElementById("download-log"),
  clearLog: document.getElementById("clear-log"),
  autoscroll: document.getElementById("autoscroll"),
  command: document.getElementById("command"),
  send: document.getElementById("send"),
  showProtocol: document.getElementById("show-protocol"),
  boardPanel: document.getElementById("board-panel"),
  toggleTerminal: document.getElementById("toggle-terminal"),
  healthSummary: document.getElementById("health-summary"),
  checks: {
    wifi: document.getElementById("check-wifi"),
    sd: document.getElementById("check-sd"),
    mic: document.getElementById("check-mic"),
    speaker: document.getElementById("check-speaker"),
    openai: document.getElementById("check-openai"),
  },
  wifiBars: document.getElementById("wifi-bars"),
  orb: document.getElementById("orb"),
  stateLabel: document.getElementById("state-label"),
  stateDetail: document.getElementById("state-detail"),
  steps: document.getElementById("steps"),
  talk: document.getElementById("talk"),
  askText: document.getElementById("ask-text"),
  askSend: document.getElementById("ask-send"),
  talkLabel: document.getElementById("talk-label"),
  cancelTalk: document.getElementById("cancel-talk"),
  micFill: document.getElementById("mic-fill"),
  meterName: document.getElementById("meter-name"),
  meterFloor: document.getElementById("meter-floor"),
  meterThreshold: document.getElementById("meter-threshold"),
  meterNote: document.getElementById("meter-note"),
  handsFree: document.getElementById("hands-free"),
  stepArmed: document.getElementById("step-armed"),
  vad: document.getElementById("vad"),
  vadSettings: document.getElementById("vad-settings"),
  vadSensitivity: document.getElementById("vad-sensitivity"),
  vadSensitivityValue: document.getElementById("vad-sensitivity-value"),
  vadHangover: document.getElementById("vad-hangover"),
  vadHangoverValue: document.getElementById("vad-hangover-value"),
  vadNote: document.getElementById("vad-note"),
  bubbles: document.getElementById("bubbles"),
  result: document.getElementById("result"),
  volume: document.getElementById("volume"),
  volumeValue: document.getElementById("volume-value"),
  volumeNote: document.getElementById("volume-note"),
  mute: document.getElementById("mute"),
  boardOptionsControl: document.getElementById("board-options-control"),
  talkButtonOption: document.getElementById("talk-button-option"),
  talkButton: document.getElementById("talk-button"),
  statusSoundsOption: document.getElementById("status-sounds-option"),
  statusSounds: document.getElementById("status-sounds"),
  wifiControl: document.getElementById("wifi-control"),
  wifiNet: document.getElementById("wifi-net"),
  wifiSsid: document.getElementById("wifi-ssid"),
  wifiPassword: document.getElementById("wifi-password"),
  wifiSave: document.getElementById("wifi-save"),
  wifiForget: document.getElementById("wifi-forget"),
  wifiNote: document.getElementById("wifi-note"),
  apiKeyControl: document.getElementById("api-key-control"),
  apiKeyInUse: document.getElementById("api-key-in-use"),
  apiKeyInput: document.getElementById("api-key"),
  apiKeySave: document.getElementById("api-key-save"),
  apiKeyForget: document.getElementById("api-key-forget"),
  apiKeyNote: document.getElementById("api-key-note"),
  beep: document.getElementById("beep"),
  ledControl: document.getElementById("led-control"),
  led: document.getElementById("led"),
  ledChip: document.getElementById("led-chip"),
  ledBrightness: document.getElementById("led-brightness"),
  ledBrightnessValue: document.getElementById("led-brightness-value"),
  lightValue: document.getElementById("light-value"),
  lightMode: document.getElementById("light-mode"),
  lightClock: document.getElementById("light-clock"),
  lightReading: document.getElementById("light-reading"),
  lightBand: document.getElementById("light-band"),
  lightNeedle: document.getElementById("light-needle"),
  bandNight: document.getElementById("band-night"),
  bandEvening: document.getElementById("band-evening"),
  bandDay: document.getElementById("band-day"),
  eveningMin: document.getElementById("evening-min"),
  dayMin: document.getElementById("day-min"),
  applyBands: document.getElementById("apply-bands"),
  lightNote: document.getElementById("light-note"),
};

const state = {
  recordings: [],         // both kinds: { name, size, kind, durationS }
  activeKind: "reply",    // which tab is showing
  downloaded: new Map(),  // name -> { kind, size, crc, pcm, variants: { plain, gained } } for this page session
  selectedName: null,
  analysis: null,
  busyRetries: 0,
  listTimer: null,
  reconnectWanted: false,
  logLines: [],
  logFile: null,  // { handle, bytesWritten, queued: [] } once a folder is picked
  downloadingName: null,    // the recording a GET is running for
  downloadsRunning: 0,      // GETs asked for and not over yet - the board's loop() is held meanwhile, so Talk and Ask wait
  lastProgressPercent: -1,
  selectedSource: "",       // "Downloaded from the board ..." / "Playing the copy already on the laptop ..."
  sessionStamp: fileStamp(new Date()),
  board: freshBoardStatus(),
  replayActive: false,  // a saved log is being replayed - its lines stay out of the live log and statistics
  liveUsage: null,      // the live statistics, set aside while a replay fills its own
};

/** What the page knows about the board's status - reset on every connect. */
function freshBoardStatus() {
  return {
    stages: {},              // stage -> { state, detail } from @@STAGE
    pipeline: null,          // from @@STATE: { state, detail }
    shownState: "offline",   // what the big indicator shows (may lag pipeline - see showPipelineState)
    shownSince: 0,
    deferredState: null,
    deferTimer: null,
    errorTimer: null,
    listenTimer: null,
    talkPendingTimer: null,
    talkPending: false,
    wifiRssi: null,
    led: null,               // { on, brightness, source }
    volume: null,            // { percent, muted }
    light: null,             // { value, band, eveningMin, dayMin }
    vad: null,               // { on, sensitivity, hangoverMs } from @@VAD - null: the firmware has no hands-free
    listenLeadS: 0,          // a hands-free recording's pre-roll, from @@VAD_EVENT started
    noStatusTimer: null,
    legacyFirmware: false,   // connected for NO_STATUS_HINT_MS with no @@STATE at all
    cycleFirstBubble: null,  // the first bubble added since the current LISTEN started
    cycleFirstSaid: null,    // the assistant's first bubble in it - gets the response time from @@TIMING
    cycleTools: new Set(),   // tools already noted in the conversation this LISTEN
    timer: null,             // { endsAt (performance.now() ms), totalS } from @@TIMER, null when none runs
    timerTicker: null,
    timerNote: null,         // the set_timer note waiting for the board's @@TIMER <left> <total> (see noteToolInConversation())
    timerOffThisTurn: null,  // "cancelled" / "none" once @@TIMER off came in this LISTEN or typed question
    askPending: null,  // a typed question sent with ASK, until the board echoes it as @@HEARD
    wifiSavePending: null,  // { ssid, timer } from Save & connect until @@WIFI_NET saved or an "ignored" line
    apiKey: null,           // { source, hint } from @@OPENAI_KEY - null: the firmware can't take a key from here
    apiKeySavePending: null,  // { hint, timer } from Save & connect until @@OPENAI_KEY saved <hint> or an "ignored" line
    playbackHoldUntil: 0,   // performance.now() the board was told the player plays until (LAPTOP_PLAYBACK) - 0: not playing
    aliveSeen: false,  // an @@ALIVE came since connecting or the last restart - only then is silence flagged (older firmware sends none)
    aliveTicker: null, // checkBoardAlive() once a second while connected
    quietFrom: 0,      // silence isn't counted before this (performance.now()) - moved on while it isn't watched
    silent: false,     // shown as "not responding" (setBoardSilent)
  };
}

const link = new BoardLink({
  onLine: (line) => { appendLog(line); noteAskRefused(line); noteWifiRefused(line); noteApiKeyRefused(line); },
  onNote: (text) => appendLog(`(app) ${text}`),
  onConnectionChange: handleConnectionChange,
  onListAnnounced: (files) => {
    clearTimeout(state.listTimer);  // nothing left to ask for
    showRecordings(files);
  },
  onRecordingSaved: addSavedRecording,
  onRecordingDeleted: removeRecording,
  onTransferProgress: showDownloadProgress,
  onStatus: handleStatusLine,
  onActivity: () => { if (state.board.silent) setBoardSilent(false); },
});

function pad(number, width = 2) { return String(number).padStart(width, "0"); }
function fileStamp(date) {
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}_` +
         `${pad(date.getHours())}-${pad(date.getMinutes())}-${pad(date.getSeconds())}`;
}
function clockStamp(date) {
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`;
}
function formatSeconds(seconds) { return `${seconds.toFixed(1)} s`; }
const utf8Encoder = new TextEncoder();
/** The text's length in UTF-8 bytes - what the firmware's limits count (a Hebrew letter is 2). */
function utf8Length(text) { return utf8Encoder.encode(text).length; }
function formatBytes(bytes) { return bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`; }
function formatDbfs(value) { return value === null ? "silent" : `${value.toFixed(1)} dBFS`; }
function hex32(value) { return value.toString(16).padStart(8, "0"); }

// ===== Connection =====

async function connectPort(port) {
  try {
    await link.open(port, Number(elements.baud.value));
  } catch (error) {
    appendLog(`(app) Couldn't open the port (${error.message}). Is it open in another program - the logger, ` +
              "the Arduino Serial Monitor, or an upload?");
  }
}

async function onConnectClick() {
  if (link.connected) {
    state.reconnectWanted = false;
    await link.close();
    return;
  }
  try {
    const port = await navigator.serial.requestPort();
    await connectPort(port);
  } catch (error) {
    if (error.name !== "NotFoundError") appendLog(`(app) ${error.message}`);  // NotFoundError = chooser cancelled
  }
  elements.connect.blur();  // else the next Space (= Talk) would click Disconnect
}

/** The red banner above the panels - for problems someone across the room should notice. */
function showAlert(title, detail) {
  document.getElementById("connection-alert-title").textContent = title;
  document.getElementById("connection-alert-detail").textContent = detail;
  document.getElementById("connection-alert").hidden = false;
}
function hideAlert() { document.getElementById("connection-alert").hidden = true; }

function handleConnectionChange(connected, closedOnPurpose) {
  if (connected) {
    hideAlert();
    stopReplay("the board connected");
    if (state.portLost) setTimeout(() => autoTickDemo("reconnect"), 0);  // after link.connected is set
    state.portLost = false;
  } else if (!closedOnPurpose) {
    state.portLost = true;
    showAlert("Lost the board's USB connection.",
              "Check the cable and plug it back in - the app reconnects by itself. A board powered through this cable restarts (about 10 s to the greeting).");
  }
  document.getElementById("replay-log").disabled = connected;
  elements.status.classList.toggle("connected", connected);
  elements.connect.textContent = connected ? "Disconnect" : "Connect board";
  elements.baud.disabled = connected;
  elements.refresh.disabled = !connected;
  elements.deleteAll.disabled = !connected || recordingsOfActiveKind().length === 0;
  elements.send.disabled = !connected;
  elements.deleteButton.disabled = !connected || !state.selectedName;
  if (connected) {
    const info = link.port.getInfo();
    const chip = USB_CHIP_NAMES[info.usbVendorId] || "USB serial";
    elements.status.textContent = `${chip} · ${link.baud} baud`;
    appendLog(`(app) Connected (${chip}, ${link.baud} baud).`);
    state.reconnectWanted = true;
    scheduleListRefresh(500);
    resetBoardStatus("unknown");
    state.board.aliveTicker = setInterval(checkBoardAlive, ALIVE_CHECK_MS);
    setTimeout(() => sendQuietly("STATUS"), STATUS_REQUEST_DELAY_MS);
    state.board.noStatusTimer = setTimeout(() => {
      if (!state.board.pipeline) {
        state.board.legacyFirmware = true;
        elements.stateDetail.textContent = "This board doesn't report its status - flash step 28's firmware. Talk still sends LISTEN.";
        elements.healthSummary.textContent = "No status from the board (older firmware).";
        updateTalkButton();
      }
    }, NO_STATUS_HINT_MS);
  } else {
    resetBoardStatus("offline");
    elements.status.textContent = "Not connected";
    appendLog(closedOnPurpose
      ? "(app) Disconnected."
      : "(app) Lost the board's port - it reconnects by itself when the board is plugged back in.");
    elements.listStatus.textContent = "Connect the board to see its recordings.";
  }
}

// ===== Log =====

/** Adds a line to the terminal and the log buffers. protocol: true for the
    @@ status lines and the app's own STATUS requests - hidden on screen
    unless "Show @@ lines" is ticked, always kept in a saved log. A replayed
    line is only shown: in the saved log it would pass for a new live one. */
function appendLog(text, { protocol = false } = {}) {
  const stamped = `[${clockStamp(new Date())}] ${text}`;
  if (!state.replayActive) {
    state.logLines.push(stamped);
    if (state.logFile) state.logFile.queued.push(stamped);
  }

  const line = document.createElement("div");
  if (protocol) line.className = "protocol";
  else if (text.startsWith(">> ")) line.className = "sent";
  else if (text.startsWith("(app)")) line.className = "app";
  else if (text.includes("[ERROR]") || text.includes("FAILED")) line.className = "error";
  else if (text.startsWith("=====")) line.className = "banner";
  line.textContent = stamped;
  elements.log.appendChild(line);
  while (elements.log.childElementCount > MAX_LOG_LINES_SHOWN) elements.log.firstChild.remove();
  if (elements.autoscroll.checked) elements.log.scrollTop = elements.log.scrollHeight;
}

async function sendCommand(text) {
  const command = text.trim();
  if (!command) return;
  if (/^GET\b/i.test(command)) {
    appendLog("(app) Not sent: pick a recording from the list instead - a raw GET would stream binary into the log.");
    return;
  }
  try {
    await link.send(command);
    appendLog(`>> ${withoutSecrets(command)}`);
  } catch (error) {
    appendLog(`(app) Not sent: ${error.message}`);
  }
}

/** A command as the log shows it: an API key or a Wi-Fi password typed in
    the command box becomes ******** (the log can be saved and shared). */
function withoutSecrets(command) {
  const secret = /^(OPENAI_KEY(?!_FORGET)|WIFI_PASSWORD)(.*)$/i.exec(command);
  return secret && secret[2].trim() ? `${secret[1]} ********` : command;
}

/** Saves the log as it grows into a folder of your choice - pick tools/ so
    "read log" finds it, same name format as serial_logger.ps1. */
async function onSaveLogClick() {
  try {
    const folder = await window.showDirectoryPicker({ id: "esp32-logs", mode: "readwrite" });
    const fileName = `esp32_log_${state.sessionStamp}.txt`;
    const handle = await folder.getFileHandle(fileName, { create: true });
    state.logFile = { handle, bytesWritten: 0, queued: [...state.logLines] };
    await flushLogFile();
    elements.logTarget.textContent = `Saving to ${folder.name}/${fileName}`;
    elements.saveLog.textContent = "Saving log ✓";
    elements.saveLog.disabled = true;
  } catch (error) {
    if (error.name !== "AbortError") appendLog(`(app) Couldn't save the log there (${error.message}). Use Download log instead.`);
  }
}

async function flushLogFile() {
  const sink = state.logFile;
  if (!sink || sink.flushing || !sink.queued.length) return;
  sink.flushing = true;
  try {
    const text = sink.queued.join("\n") + "\n";
    sink.queued = [];
    const bytes = new TextEncoder().encode(text);
    const writable = await sink.handle.createWritable({ keepExistingData: true });
    await writable.write({ type: "write", position: sink.bytesWritten, data: bytes });
    await writable.close();
    sink.bytesWritten += bytes.length;
  } finally {
    sink.flushing = false;
  }
}

/** Empties the panel and the Download log buffer. A log file being saved to
    a folder is left whole - it's the session's record for "read log" - and
    just gets a line marking when the clear happened. */
function onClearLogClick() {
  elements.log.replaceChildren();
  state.logLines = [];
  if (state.logFile) state.logFile.queued.push(`[${clockStamp(new Date())}] (app) Log cleared on screen.`);
}

function onDownloadLogClick() {
  const blob = new Blob([state.logLines.join("\n") + "\n"], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const anchor = Object.assign(document.createElement("a"), { href: url, download: `esp32_log_${state.sessionStamp}.txt` });
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

// ===== Recordings list =====

function scheduleListRefresh(delayMs) {
  if (!link.connected) return;
  clearTimeout(state.listTimer);
  state.busyRetries = 0;
  state.listTimer = setTimeout(loadList, delayMs);
}

async function loadList() {
  if (!link.connected) return;
  elements.listStatus.textContent = "Asking the board…";
  let files;
  try {
    files = await link.list();
  } catch (error) {
    if (error.kind === "busy" && state.busyRetries < BUSY_RETRY_LIMIT) {
      state.busyRetries += 1;
      elements.listStatus.textContent = "The board is busy with a reply - retrying…";
      state.listTimer = setTimeout(loadList, BUSY_RETRY_MS);
    } else if (error.message.includes("timed out")) {
      // Usually the board restarting: it announces its list as it boots.
      elements.listStatus.textContent = "No answer yet - the board may be starting up. The list appears as soon as it reports.";
    } else {
      elements.listStatus.textContent = error.message;
    }
    return;
  }
  showRecordings(files);
}

/** A board-reported file with its kind and length worked out. */
function describeRecording(file) {
  const kind = kindOfFile(file.name);
  const format = RECORDING_KINDS[kind];
  return { ...file, kind, durationS: file.size / (format.sampleRateHz * format.bytesPerSample) };
}

/** Replaces the list with files reported by the board (a LIST answer or an announcement). */
function showRecordings(files) {
  state.recordings = files.map(describeRecording).sort(newestFirst);
  updateListStatus();
  renderList();
}

/** Adds (or updates) one recording the board just announced with @@SAVED. */
function addSavedRecording(file) {
  state.recordings = state.recordings.filter((existing) => existing.name !== file.name);
  state.recordings.push(describeRecording(file));
  state.recordings.sort(newestFirst);
  updateListStatus();
  renderList();
}

/** Drops a recording the board reported deleted (@@DELETED) from the list,
    from the laptop's copies, and from the player if it was showing it. */
function removeRecording(fileName) {
  state.recordings = state.recordings.filter((recording) => recording.name !== fileName);
  const copy = state.downloaded.get(fileName);
  if (copy) releaseCopy(copy);
  state.downloaded.delete(fileName);
  if (state.selectedName === fileName) resetPlayer(`${fileName} was deleted from the SD card.`);
  updateListStatus();
  renderList();
}

/** Frees the WAV files made for a downloaded copy. */
function releaseCopy(copy) {
  Object.values(copy.variants).forEach((variant) => URL.revokeObjectURL(variant.wavUrl));
}

/** Sort order for the list: highest number first. Numeric, so
    reply_10000 still sorts above reply_9999. */
function newestFirst(a, b) {
  return b.name.localeCompare(a.name, undefined, { numeric: true });
}

function recordingsOfActiveKind() {
  return state.recordings.filter((recording) => recording.kind === state.activeKind);
}

/** Updates the tab labels (with counts), the status line under them, and
    whether Delete all applies. */
function updateListStatus() {
  elements.tabs.forEach((tab) => {
    const kind = tab.dataset.kind;
    const count = state.recordings.filter((recording) => recording.kind === kind).length;
    tab.textContent = `${RECORDING_KINDS[kind].label} (${count})`;
    tab.setAttribute("aria-selected", String(kind === state.activeKind));
  });
  const format = RECORDING_KINDS[state.activeKind];
  const count = recordingsOfActiveKind().length;
  let text;
  if (count && state.activeKind === "command") {
    const downloadSecondsPerAudioSecond = (format.sampleRateHz * format.bytesPerSample * 10) / Number(elements.baud.value);
    text = `${count} ${count === 1 ? format.noun : format.nounPlural} - newest first. The board keeps the newest ` +
           `${limits.max_kept_recordings}. Uncompressed, so each second of audio takes about ` +
           `${downloadSecondsPerAudioSecond.toFixed(1)} s to download.`;
  } else if (count) {
    text = `${count} ${count === 1 ? format.noun : format.nounPlural} - newest first. The board keeps the newest ` +
           `${limits.max_kept_recordings}.`;
  } else if (state.activeKind === "command") {
    text = "No voice commands yet. Every LISTEN saves what the mic heard.";
  } else {
    text = "No replies yet. Send LISTEN and talk to the board - every reply is saved.";
  }
  elements.listStatus.textContent = text;
  elements.deleteAll.disabled = !link.connected || count === 0;
}

/** Switches the list between replies and voice commands. */
function selectTab(kind) {
  state.activeKind = kind;
  updateListStatus();
  renderList();
}

/** Asks for confirmation, then deletes every recording of the kind showing.
    The list empties as the board confirms each file (@@DELETED). */
async function onDeleteAllClick() {
  const format = RECORDING_KINDS[state.activeKind];
  const count = recordingsOfActiveKind().length;
  const question = `Delete all ${count} ${count === 1 ? format.noun : format.nounPlural} from the ESP32's SD card?` +
                   "\n\nThis can't be undone.";
  if (!count || !window.confirm(question)) return;
  elements.deleteAll.disabled = true;
  elements.listStatus.textContent = "Deleting…";
  try {
    await link.deleteAll(format.folderArgument);
    updateListStatus();
  } catch (error) {
    elements.listStatus.textContent = error.kind === "busy"
      ? "The board is busy with a reply - try Delete all again in a moment."
      : `Couldn't delete everything: ${error.message}`;
    elements.deleteAll.disabled = !link.connected || recordingsOfActiveKind().length === 0;
    if (error.kind !== "busy") scheduleListRefresh(500);  // show what's actually left
  }
}

function isOnLaptop(recording) {
  const copy = state.downloaded.get(recording.name);
  return Boolean(copy && copy.size === recording.size);
}

function renderList() {
  elements.list.replaceChildren(...recordingsOfActiveKind().map((recording) => {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "recording" + (recording.name === state.selectedName ? " selected" : "");
    const name = document.createElement("span");
    name.className = "name";
    name.textContent = recording.name;
    const meta = document.createElement("span");
    meta.className = "meta";
    meta.textContent = `${formatSeconds(recording.durationS)} · ${formatBytes(recording.size)}`;
    const badge = document.createElement("span");
    const local = isOnLaptop(recording);
    badge.className = "badge" + (local ? " local" : "");
    badge.textContent = local ? "on laptop" : "on board";
    button.append(name, meta, badge);
    button.addEventListener("click", () => selectRecording(recording));
    item.appendChild(button);
    return item;
  }));
}

// ===== Player =====

/** Shows how far a download has got, while one is running for the
    selected recording. */
function showDownloadProgress(bytesReceived, bytesTotal) {
  if (!state.downloadingName || state.downloadingName !== state.selectedName) return;
  const percent = Math.floor((bytesReceived * 100) / bytesTotal);
  if (percent === state.lastProgressPercent) return;
  state.lastProgressPercent = percent;
  elements.playerStatus.textContent = `Downloading from the board… ${percent}%`;
}

async function selectRecording(recording) {
  if (!SAFE_FILE_NAME.test(recording.name)) {
    elements.playerStatus.textContent = "That file name can't be fetched from the board.";
    return;
  }
  state.selectedName = recording.name;
  state.analysis = null;
  renderList();
  elements.playerHeading.textContent = recording.name;
  elements.gainOption.hidden = recording.kind !== "command";
  elements.deleteButton.disabled = !link.connected;
  elements.download.setAttribute("aria-disabled", "true");
  elements.stats.replaceChildren();
  elements.pauseList.replaceChildren();
  elements.audio.removeAttribute("src");
  drawWaveform();

  let copy = state.downloaded.get(recording.name);
  const fromLaptop = Boolean(copy && copy.size === recording.size);
  if (!fromLaptop) {
    elements.playerStatus.textContent = "Downloading from the board…";
    state.downloadingName = recording.name;
    state.lastProgressPercent = -1;
    state.downloadsRunning += 1;
    updateTalkButton();
    try {
      const { data, crc } = await link.get(recording.name);
      if (copy) releaseCopy(copy);
      copy = { kind: recording.kind, size: data.length, crc, pcm: RECORDING_KINDS[recording.kind].decode(data), variants: {} };
      state.downloaded.set(recording.name, copy);
    } catch (error) {
      if (state.selectedName === recording.name) {
        elements.playerStatus.textContent = error.kind === "busy"
          ? "The board is busy with a reply - pick it again in a moment."
          : error.message;
      }
      return;
    } finally {
      state.downloadingName = null;
      state.downloadsRunning -= 1;
      updateTalkButton();
    }
  }
  if (state.selectedName !== recording.name) return;  // another one was picked meanwhile

  renderList();
  state.selectedSource = fromLaptop
    ? `Playing the copy already on the laptop · CRC ${hex32(copy.crc)}`
    : `Downloaded from the board · CRC ${hex32(copy.crc)} verified`;
  showSelectedCopy(copy, { autoplay: true });
}

/** The playable version of a copy: as recorded, or - for a voice command
    with the gain option ticked - with the board's gain applied. Each is
    built once and kept. */
function playableVersion(copy) {
  const withGain = copy.kind === "command" && elements.applyGain.checked;
  const key = withGain ? "withGain" : "asRecorded";
  if (!copy.variants[key]) {
    const format = RECORDING_KINDS[copy.kind];
    const pcm = withGain ? applyBoardGain(copy.pcm) : copy.pcm;
    copy.variants[key] = {
      analysis: analyze(pcm, format.sampleRateHz, format.fullScaleMagnitude),
      wavUrl: URL.createObjectURL(new Blob([makeWav(pcm, format.sampleRateHz)], { type: "audio/wav" })),
    };
  }
  return { version: copy.variants[key], withGain };
}

/** Loads the selected copy into the player, stats and waveform. With
    keepPosition, carries on from the same point (used when the gain option
    is switched mid-playback). */
function showSelectedCopy(copy, { autoplay = false, keepPosition = false } = {}) {
  const { version, withGain } = playableVersion(copy);
  const position = elements.audio.currentTime;
  const wasPlaying = !elements.audio.paused;
  state.analysis = version.analysis;
  let levelNote = "";
  if (copy.kind === "command") {
    levelNote = withGain
      ? ` · with the board's ×${limits.mic_gain} gain, as OpenAI received it`
      : ` · the mic's own level, before the ×${limits.mic_gain} gain`;
  }
  elements.playerStatus.textContent = state.selectedSource + levelNote;
  elements.audio.src = version.wavUrl;
  elements.download.href = version.wavUrl;
  elements.download.download =
    state.selectedName.replace(/\.(ulaw|pcm)$/i, "") + (withGain ? `_gain_x${limits.mic_gain}` : "") + ".wav";
  elements.download.setAttribute("aria-disabled", "false");
  renderStats(copy, version.analysis);
  drawWaveform();
  if (keepPosition) {
    elements.audio.addEventListener("loadedmetadata", () => {
      elements.audio.currentTime = position;
      if (wasPlaying) elements.audio.play().catch(() => {});
    }, { once: true });
  } else if (autoplay) {
    elements.audio.play().catch(() => { /* autoplay blocked - the controls still work */ });
  }
}

/** Tells the board whether the player is playing a recording out loud
    (LAPTOP_PLAYBACK - ESP32/hands_free_utils.h), so its mic doesn't take
    the recording for a question: "on <ms>" - the time left at this speed,
    plus PLAYBACK_HOLD_MARGIN_MS - whenever playback starts or resumes,
    jumps or changes speed; "off" once it's paused, over or replaced. The
    board ends a hold by itself when that time is up, so a page closed
    mid-playback can't keep its mic off. */
function syncPlaybackHold() {
  const board = state.board;
  if (!link.connected || state.replayActive) return;
  const audio = elements.audio;
  const playing = !audio.paused && !audio.ended && !audio.error && audio.readyState >= HTMLMediaElement.HAVE_FUTURE_DATA;
  if (!playing) {
    if (board.playbackHoldUntil) {
      board.playbackHoldUntil = 0;
      sendQuietly("LAPTOP_PLAYBACK off");
      if (elements.vadNote.textContent === PLAYBACK_VAD_NOTE) elements.vadNote.textContent = VAD_NOTE_DEFAULT;
    }
    return;
  }
  const holdMs = Number.isFinite(audio.duration)
    ? Math.ceil((Math.max(0, audio.duration - audio.currentTime) / (audio.playbackRate || 1)) * 1000) + PLAYBACK_HOLD_MARGIN_MS
    : null;  // length unknown: the board holds for its longest
  const until = performance.now() + (holdMs === null ? 5 * 60 * 1000 : holdMs);
  if (board.playbackHoldUntil && Math.abs(until - board.playbackHoldUntil) < PLAYBACK_HOLD_RESEND_MS) return;
  board.playbackHoldUntil = until;
  sendQuietly(holdMs === null ? "LAPTOP_PLAYBACK on" : `LAPTOP_PLAYBACK on ${holdMs}`);
  if (board.vad && board.vad.on) elements.vadNote.textContent = PLAYBACK_VAD_NOTE;
}

/** The player's status line from before pausePlayerForListen(), to put back. */
let playerStatusBeforePause = "";

/** Pauses the player for a question the user asked for (Talk, the BOOT
    button, LISTEN): the board's mic is recording, and mustn't record the
    recording too. */
function pausePlayerForListen() {
  const status = elements.playerStatus.textContent;
  if (status !== PLAYER_PAUSED_FOR_LISTEN) playerStatusBeforePause = status;
  elements.playerStatus.textContent = PLAYER_PAUSED_FOR_LISTEN;
  elements.audio.pause();
}

/** The player starting - not while the board records a question the user
    asked for (pausePlayerForListen()). A hands-free recording gives way
    instead: the board cancels it once told (LAPTOP_PLAYBACK). */
function onPlayerPlay() {
  requestAnimationFrame(animatePlayhead);
  const pipeline = state.board.pipeline;
  if (link.connected && pipeline && pipeline.state === "listening" && pipeline.detail !== "vad") {
    pausePlayerForListen();
  } else if (elements.playerStatus.textContent === PLAYER_PAUSED_FOR_LISTEN) {
    elements.playerStatus.textContent = playerStatusBeforePause;
  }
}

/** Empties the player back to its "nothing selected" state, with a message. */
function resetPlayer(message) {
  state.selectedName = null;
  state.analysis = null;
  elements.playerHeading.textContent = "Select a recording";
  elements.playerStatus.textContent = message;
  elements.gainOption.hidden = true;
  elements.audio.removeAttribute("src");
  elements.audio.load();
  elements.download.setAttribute("aria-disabled", "true");
  elements.deleteButton.disabled = true;
  elements.stats.replaceChildren();
  elements.pauseList.replaceChildren();
  drawWaveform();
}

/** Asks for confirmation, then deletes the selected recording from the SD
    card. The list and player update when the board confirms (@@DELETED). */
async function onDeleteClick() {
  const fileName = state.selectedName;
  if (!fileName) return;
  const keepTip = state.downloaded.has(fileName) ? "\n\nTo keep a copy, use Download WAV first." : "";
  if (!window.confirm(`Delete ${fileName} from the ESP32's SD card?\n\nThis can't be undone.${keepTip}`)) return;
  elements.deleteButton.disabled = true;
  try {
    await link.deleteRecording(fileName);
  } catch (error) {
    if (state.selectedName === fileName) {
      elements.playerStatus.textContent = error.kind === "busy"
        ? "The board is busy with a reply - try deleting again in a moment."
        : `Couldn't delete it: ${error.message}`;
      elements.deleteButton.disabled = !link.connected;
    }
  }
}

function renderStats(copy, analysis) {
  const rows = [
    ["Duration", formatSeconds(analysis.durationS)],
    ["Size", `${copy.size.toLocaleString()} bytes`],
    ["Peak level", formatDbfs(analysis.peakDbfs)],
    ["Average (RMS) level", formatDbfs(analysis.rmsDbfs)],
    ["Samples at full scale", analysis.fullScaleSamples.toLocaleString(), analysis.fullScaleSamples > 0],
    ["Silence before / after sound",
      `${formatSeconds(analysis.leadingSilenceS)} / ${formatSeconds(analysis.trailingSilenceS)}`],
    ["Pauses ≥ 0.25 s inside", String(analysis.pauses.length)],
  ];
  elements.stats.replaceChildren(...rows.map(([label, value, warn]) => {
    const wrapper = document.createElement("div");
    const term = document.createElement("dt");
    term.textContent = label;
    const detail = document.createElement("dd");
    detail.textContent = value;
    if (warn) detail.className = "warn";
    wrapper.append(term, detail);
    return wrapper;
  }));
  elements.pauseList.replaceChildren(...analysis.pauses.map((pause) => {
    const item = document.createElement("li");
    item.textContent = `${formatSeconds(pause.lengthS)} pause at ${formatSeconds(pause.startS)}`;
    return item;
  }));
}

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function drawWaveform() {
  const canvas = elements.waveform;
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);
  const context = canvas.getContext("2d");
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);
  const analysis = state.analysis;
  if (!analysis || !analysis.envelope.length) return;

  const duration = analysis.durationS || 1;
  context.fillStyle = cssVar("--pause-band");
  analysis.pauses.forEach((pause) => {
    context.fillRect((pause.startS / duration) * width, 0, (pause.lengthS / duration) * width, height);
  });

  const middle = height / 2;
  const columnWidth = width / analysis.envelope.length;
  context.fillStyle = cssVar("--wave");
  analysis.envelope.forEach(([low, high], index) => {
    const top = middle - high * middle;
    const bottom = middle - low * middle;
    context.fillRect(index * columnWidth, top, Math.max(1, columnWidth - 0.5), Math.max(1, bottom - top));
  });

  const audio = elements.audio;
  if (audio.duration) {
    context.fillStyle = cssVar("--playhead");
    context.fillRect((audio.currentTime / audio.duration) * width - 1, 0, 2, height);
  }
}

function animatePlayhead() {
  drawWaveform();
  if (!elements.audio.paused) requestAnimationFrame(animatePlayhead);
}

// ===== Board status (step 28) =====

/** Sends a command the app needs but the user didn't type (STATUS) - logged
    as a protocol line, so it doesn't clutter the terminal. */
async function sendQuietly(command) {
  try {
    await link.send(command);
    appendLog(`>> ${command}`, { protocol: true });
  } catch (_) { /* not connected any more - nothing to ask */ }
}

/** Promises waiting for a status line (the self-test's answers). */
const statusWaiters = new Set();
let selfTestRunning = false;  // runSelfTest() below

/** Resolves with the next @@<tag> line's words that pass test, or null after timeoutMs. */
function waitForStatus(tag, test = () => true, timeoutMs = 2500) {
  return new Promise((resolve) => {
    const waiter = { tag, test, resolve };
    waiter.timer = setTimeout(() => { statusWaiters.delete(waiter); resolve(null); }, timeoutMs);
    statusWaiters.add(waiter);
  });
}

/** One status line from the board: "@@<tag> <rest>". */
function handleStatusLine(tag, rest) {
  // The levels come ~10 a second (MIC_LEVEL while recording, VAD_LEVEL while waiting for speech) - left out.
  if (tag !== "MIC_LEVEL" && tag !== "VAD_LEVEL") appendLog(`@@${tag} ${rest}`, { protocol: true });
  const words = rest.split(" ");
  const board = state.board;
  for (const waiter of [...statusWaiters]) {
    if (waiter.tag === tag && waiter.test(words)) {
      clearTimeout(waiter.timer);
      statusWaiters.delete(waiter);
      waiter.resolve(words);
    }
  }
  switch (tag) {
    case "STAGE":
      board.stages[words[0]] = { state: words[1], detail: words.slice(2).join(" ") };
      renderHealth();
      break;
    case "STATE": {
      clearTimeout(board.noStatusTimer);
      const wasRunning = Boolean(board.pipeline) && board.pipeline.state !== "booting";
      // The board's "offline" (no Wi-Fi / no OpenAI) is shown as "netdown": the
      // app's own "offline" already means "no board connected".
      const boardState = words[0] === "offline" ? "netdown" : words[0];
      board.pipeline = { state: boardState, detail: words.slice(1).join(" ") };
      if (words[0] === "booting") {
        onUsageBoardBooting();
        board.aliveSeen = false;  // setup()'s waits send no @@ALIVE - watched again from its next one
        board.playbackHoldUntil = 0;  // a restarted board forgot LAPTOP_PLAYBACK - told again at its first @@ALIVE
      }
      if (wasRunning && words[0] === "booting") {
        elements.result.className = "result";
        elements.result.textContent = "The board restarted.";
        board.timer = null;  // the timer lived in the board's RAM - gone with the restart
        settleTimerNote("⏰ Timer not set - the board restarted");
        renderTimer();
      }
      onPipelineState(board.pipeline);
      if (words[0] === "halted") showAlert("The board stopped.", board.pipeline.detail || "See the serial terminal for why, then press Reset on the board.");
      else if (words[0] === "booting" && wasRunning) hideAlert();
      break;
    }
    case "RESULT":
      if (words[0] === "cancel") {  // the Cancel button (firmware CANCEL)
        elements.result.className = "result";
        elements.result.textContent = `LISTEN #${words[1]} cancelled.`;
        addConversationNote(`LISTEN #${words[1]} cancelled - no answer.`);
        autoTickDemo("cancel");
        break;
      }
      showResult(words[0] === "pass", words[1], words.slice(2).join(" "));
      break;
    case "MIC_LEVEL":
      showMicLevel(Number(words[0]));
      break;
    case "HEARD":
      // Transcription can arrive after the reply - keep it first. Empty: OpenAI heard no words (a noise).
      addBubble("heard", rest.trim() ? rest : "(no words - just a sound)", board.cycleFirstBubble);
      if (board.askPending && rest.trim() === board.askPending && performance.now() - board.askPendingAt < ASK_ECHO_TIMEOUT_MS) {  // the board echoes a typed question back
        const typed = [...elements.bubbles.querySelectorAll(".bubble.heard")].pop();
        if (typed) typed.classList.add("typed");
        board.askPending = null;
        autoTickDemo("typed");
      }
      break;
    case "SAID":
      addBubble("said", rest);
      autoTickDemo("transcript");
      break;
    case "WIFI":
      board.wifiRssi = Number(words[0]);
      renderHealth();
      break;
    case "LED":
      board.led = { on: words[0] === "on", brightness: Number(words[1]), source: words[2] };
      renderLed();
      if (words[2] === "voice") countUsage("ledByVoice");
      break;
    case "VOLUME":
      board.volume = { percent: Number(words[0]), muted: words[1] === "muted" };
      renderVolume();
      break;
    case "LIGHT":
      board.light = { value: Number(words[0]), band: words[1], eveningMin: Number(words[2]), dayMin: Number(words[3]) };
      renderLight();
      onUsageLight(words[1]);
      break;
    case "BEEP":
      elements.volumeNote.textContent = words[0] === "played"
        ? (board.volume && board.volume.muted ? "Beep played - but the speaker is muted." : "Beep played - did you hear it?")
        : "Busy - try again when the assistant is Ready.";
      break;
    case "VAD":
      board.vad = { on: words[0] === "on", sensitivity: Number(words[1]), hangoverMs: Number(words[2]) };
      renderVad();
      break;
    case "VAD_LEVEL":
      if (board.pipeline && board.pipeline.state === "armed") showVadLevel(Number(words[0]), Number(words[1]), Number(words[2]));
      break;
    case "VAD_EVENT":
      onVadEvent(words);
      noteVadEventInConversation(words);
      if (words[0] === "discarded") countUsage("noisesIgnored");
      if (words[0] === "discarded") autoTickDemo("cough");
      if (words[0] === "paused") autoTickDemo("manynoises");
      break;
    case "TIMING":
      onUsageTiming(parseKeyValues(rest));
      showAnswerTime(parseKeyValues(rest));
      autoTickDemo(parseKeyValues(rest).mode === "vad" ? "handsfree" : "talk");
      break;
    case "TIMER":
      onTimerLine(words);
      break;
    case "BOOT_TIMING":
      usage.bootMs = Number(parseKeyValues(rest).total_ms);
      renderUsage();
      break;
    case "WIFI_NET":
      board.wifiNet = { source: words[0], ssid: words.slice(1).join(" ") };
      if (board.wifiSavePending && board.wifiNet.source === "saved" && board.wifiNet.ssid === board.wifiSavePending.ssid) {
        clearTimeout(board.wifiSavePending.timer);
        board.wifiSavePending = null;
        elements.wifiNote.textContent = `Saved - the board is connecting to ${board.wifiNet.ssid}. If it can't, it says so in Board health.`;
        autoTickDemo("netswitch");
      }
      renderBoardOptions();
      break;
    case "OPENAI_KEY":
      board.apiKey = { source: words[0], hint: words.slice(1).join(" ") };
      if (board.apiKeySavePending && board.apiKey.source === "saved" && board.apiKey.hint === board.apiKeySavePending.hint) {
        clearTimeout(board.apiKeySavePending.timer);
        board.apiKeySavePending = null;
        elements.apiKeyNote.textContent = `Saved - the board is reconnecting to OpenAI with ${board.apiKey.hint}. If OpenAI refuses it, Board health says so.`;
      }
      renderBoardOptions();
      break;
    case "BUTTON":
      board.talkButton = words[0] === "on";
      renderBoardOptions();
      break;
    case "SOUNDS":
      board.statusSounds = words[0] === "on";
      renderBoardOptions();
      break;
    case "CLOCK":
      break;  // only the self-test reads it (waitForStatus, above)
    case "ALIVE":
      // The first since connecting or a restart: loop() reads commands now,
      // so a recording already playing is announced (LAPTOP_PLAYBACK).
      if (!board.aliveSeen) syncPlaybackHold();
      board.aliveSeen = true;  // the heartbeat: from now on, silence counts (checkBoardAlive)
      break;
    case "LIMITS":
      applyLimits(rest);
      break;
    default:
      appendLog(`(app) Unhandled status line @@${tag} - this page doesn't know it yet.`);
      break;
  }
}

/** @@LIMITS key=value ... - the board's own limits replace the ones in use.
    Unknown keys and values that aren't whole numbers are ignored. */
function applyLimits(rest) {
  const keptBefore = limits.max_kept_recordings;
  for (const [key, value] of Object.entries(parseKeyValues(rest))) {
    if (Object.hasOwn(DEFAULT_LIMITS, key) && /^\d+$/.test(value)) limits[key] = Number(value);
  }
  applyLimitsToInputs();
  renderVad();
  renderLight();
  if (limits.max_kept_recordings !== keptBefore && state.recordings.length) updateListStatus();
}

/** Puts the limits into the inputs that carry them: text lengths, slider
    and number ranges, and the gain option's label. */
function applyLimitsToInputs() {
  elements.askText.maxLength = limits.max_ask_bytes;
  elements.wifiSsid.maxLength = limits.max_ssid_bytes;
  elements.wifiPassword.maxLength = limits.max_password_bytes;
  elements.apiKeyInput.maxLength = limits.max_api_key_bytes;
  elements.vadSensitivity.min = limits.vad_sensitivity_min;
  elements.vadSensitivity.max = limits.vad_sensitivity_max;
  elements.vadHangover.min = limits.vad_hangover_min_ms;
  elements.vadHangover.max = limits.vad_hangover_max_ms;
  for (const input of [elements.lightValue, elements.eveningMin, elements.dayMin]) input.max = limits.light_max;
  document.getElementById("gain-factor").textContent = `×${limits.mic_gain}`;
}

/** A hands-free event: "started <pre-roll ms>", "sent <speech ms>
    <silence|length>", "discarded <reason> <ms>" or "paused <ms>". */
function onVadEvent([kind, first, second]) {
  const board = state.board;
  if (kind === "started") {
    board.listenLeadS = (Number(first) || 0) / 1000;
  } else if (kind === "sent") {
    const ending = second === "length" ? `stopped at the ${limits.max_utterance_ms / 1000} s limit` : "ended by your pause";
    elements.vadNote.textContent = `Heard ${formatSeconds(Number(first) / 1000)} of speech - ${ending}.`;
  } else if (kind === "discarded") {
    elements.vadNote.textContent = `Ignored ${formatSeconds(Number(second) / 1000)} of sound - ${DISCARD_TEXT[first] || first}.`;
  } else if (kind === "paused") {
    elements.vadNote.textContent = `Several noises in a row - hands-free pauses for ${Math.round(Number(first) / 1000)} s.`;
  } else {
    appendLog(`(app) Unknown hands-free event "${kind}" in @@VAD_EVENT - ignored.`);
  }
}

/** Empties everything the board reported - "offline" when disconnected,
    "unknown" when just connected and waiting for its first report. */
function resetBoardStatus(shownState) {
  const old = state.board;
  [old.deferTimer, old.errorTimer, old.talkPendingTimer, old.noStatusTimer].forEach(clearTimeout);
  if (old.wifiSavePending) clearTimeout(old.wifiSavePending.timer);
  if (old.apiKeySavePending) clearTimeout(old.apiKeySavePending.timer);
  settleTimerNote("⏰ Timer not confirmed - the board disconnected", old);
  clearInterval(old.timerTicker);
  clearInterval(old.listenTimer);
  clearInterval(old.aliveTicker);
  if (old.silent) paintBoardSilent(false);  // quietly - a connect or disconnect says enough
  state.board = freshBoardStatus();
  Object.assign(limits, DEFAULT_LIMITS);  // until this board (or log) sends its own @@LIMITS
  applyLimitsToInputs();
  elements.healthSummary.textContent = shownState === "offline"
    ? "Connect the board to see its startup checks." : "Waiting for the board's report…";
  renderHealth();
  showPipelineState(shownState, "");
  showMicLevel(null);
  renderLed();
  renderVolume();
  renderLight();
  renderVad();
  renderTimer();
  renderBoardOptions();
  elements.vadNote.textContent = VAD_NOTE_DEFAULT;
  updateControlsEnabled();
}

/** Once a second while connected: the board counts as not responding when
    nothing came from it for BOARD_SILENT_MS. Only once it has sent an
    @@ALIVE since connecting or restarting (older firmware never does), and
    not while a recording downloads (the transfer holds its loop() on
    purpose) or a log is replayed - silence is counted afresh after those.
    A halted board already has its own banner. */
function checkBoardAlive() {
  const board = state.board;
  const now = performance.now();
  const halted = Boolean(board.pipeline) && board.pipeline.state === "halted";
  if (!link.connected || !board.aliveSeen || state.downloadsRunning > 0 || state.replayActive || halted) {
    board.quietFrom = now;
    return;
  }
  if (!board.silent && now - Math.max(link.lastReceivedAt, board.quietFrom) > BOARD_SILENT_MS) setBoardSilent(true);
}

/** Shows (silent) or clears the "board not responding" warning: the state
    and the health lights greyed out, and the red banner. Cleared by the next
    line from the board. */
function setBoardSilent(silent) {
  state.board.silent = silent;
  paintBoardSilent(silent);
  appendLog(silent
    ? `(app) Nothing from the board for ${BOARD_SILENT_MS / 1000} s - it may be stuck. If it stays like this, press Reset (EN) on the board.`
    : "(app) The board is reporting again.");
}

/** The page side of setBoardSilent(), without the log line. */
function paintBoardSilent(silent) {
  elements.orb.classList.toggle("stale", silent);
  document.querySelector(".health").classList.toggle("stale", silent);
  if (silent) showAlert(BOARD_SILENT_TITLE, "If it stays like this, press Reset (EN) on the board.");
  else if (document.getElementById("connection-alert-title").textContent === BOARD_SILENT_TITLE) hideAlert();
}

const STAGE_TEXT = {
  sd: { pending: "Waiting", active: "Checking…" },
  mic: { pending: "Waiting", active: "Checking the signal…" },
  speaker: { pending: "Waiting", active: "Starting…" },
  wifi: { pending: "Waiting", active: "Connecting…" },
  openai: { pending: "Waiting", active: "Connecting…" },
};

/** Redraws the five health lights from the board's @@STAGE lines. The time
    sync has no light of its own: until it's done it shows on OpenAI's, since
    there's no secure connection without it. Shown while a board is
    connected or a log is being replayed. */
function renderHealth() {
  const stages = state.board.stages;
  const live = link.connected || state.replayActive;
  const shown = { ...stages };
  const time = stages.time;
  if (time && time.state !== "ok" && (!stages.openai || stages.openai.state === "pending")) {
    shown.openai = { state: time.state, detail: time.state === "active" ? `Syncing the clock… ${time.detail}`.trim() : time.detail };
  }
  for (const [name, element] of Object.entries(elements.checks)) {
    const stage = shown[name];
    const stageState = live && stage ? stage.state : "unknown";
    element.dataset.state = stageState;
    let detail = stage && stage.detail ? stage.detail : (STAGE_TEXT[name][stageState] || "-");
    if (name === "wifi" && stageState === "ok" && state.board.wifiRssi !== null) {
      detail += ` · ${state.board.wifiRssi} dBm`;
    }
    element.querySelector(".check-detail").textContent = live ? detail : "-";
  }
  const rssi = state.board.wifiRssi;
  const bars = !live || rssi === null || !stages.wifi || stages.wifi.state !== "ok" ? 0
    : rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
  [...elements.wifiBars.children].forEach((bar, index) => bar.classList.toggle("on", index < bars));

  if (!live) return;
  const all = ["sd", "mic", "speaker", "wifi", "time", "openai"].map((name) => stages[name]);
  if (!all.some(Boolean)) return;
  const failed = all.filter((stage) => stage && stage.state === "fail").length;
  const done = all.filter((stage) => stage && stage.state === "ok").length;
  elements.healthSummary.textContent = failed
    ? `${failed} check${failed === 1 ? "" : "s"} failed`
    : done === all.length ? "All checks passed" : "Running startup checks…";
  if (done === all.length) autoTickDemo("boot");
}

/** A new @@STATE from the board. */
function onPipelineState({ state: pipelineState, detail }) {
  const board = state.board;
  if (pipelineState === "listening") {
    board.cycleFirstBubble = null;
    board.cycleFirstSaid = null;
    board.cycleTools.clear();
    settleTimerNote("⏰ Timer not set");  // a timer the last turn set has started (and said so) by now
    board.timerOffThisTurn = null;
    // A question the user asked for (Talk, BOOT, LISTEN - not hands-free):
    // its mic mustn't record the player too.
    if (link.connected && detail !== "vad" && !elements.audio.paused) pausePlayerForListen();
  }
  if (pipelineState === "tool" && detail && !board.cycleTools.has(detail)) {
    board.cycleTools.add(detail);
    noteToolInConversation(detail);
    if (DEMO_TOOL_ITEMS[detail]) autoTickDemo(DEMO_TOOL_ITEMS[detail]);
  }
  if (board.talkPending && !IDLE_STATES.has(pipelineState)) {
    board.talkPending = false;
    clearTimeout(board.talkPendingTimer);
  }
  showPipelineState(pipelineState, detail);
}

/** Shows a state on the big indicator. A tool call stays up for at least
    TOOL_MIN_SHOWN_MS, and a failed LISTEN's error for ERROR_SHOWN_MS, so
    neither just flickers past. */
function showPipelineState(shownState, detail) {
  const board = state.board;
  const sinceShown = performance.now() - board.shownSince;
  const holdFor = board.shownState === "tool" ? TOOL_MIN_SHOWN_MS - sinceShown
    : board.shownState === "error" ? ERROR_SHOWN_MS - sinceShown : 0;
  clearTimeout(board.deferTimer);
  if (holdFor > 0 && shownState !== "offline" && shownState !== "listening") {
    board.deferredState = { shownState, detail };
    board.deferTimer = setTimeout(() => {
      const deferred = board.deferredState;
      board.deferredState = null;
      board.shownSince = 0;
      if (deferred) showPipelineState(deferred.shownState, deferred.detail);
    }, holdFor);
    return;
  }
  board.shownState = shownState;
  board.shownSince = performance.now();
  const [label, defaultDetail] = STATE_TEXT[shownState] || [shownState, ""];
  elements.orb.dataset.state = shownState;
  elements.stateLabel.textContent = label;
  let detailText = defaultDetail;
  if (shownState === "tool" && detail) detailText = `${TOOL_LABELS[detail] || detail}…`;
  else if (shownState === "reconnecting" && detail) detailText = `Lost the connection to OpenAI - ${detail}.`;
  else if (shownState === "netdown" && detail) detailText = `Offline - ${detail}. It keeps retrying; the timer and the controls still work.`;
  else if ((shownState === "halted" || shownState === "error") && detail) detailText = detail;
  else if (shownState === "speaking" && detail === "greeting") detailText = "Playing the opening greeting.";
  else if (shownState === "speaking" && detail === "timer") detailText = "Saying that the timer is done.";
  else if (shownState === "speaking" && detail === "typed") detailText = "Answering your typed question.";
  elements.stateDetail.textContent = detailText;
  const step = STEP_OF_STATE[shownState];
  [...elements.steps.children].forEach((span) => span.classList.toggle("now", span.dataset.step === step));

  clearInterval(board.listenTimer);
  if (shownState === "listening") {
    // Manual (detail: its length in seconds) counts down; hands-free ("vad")
    // counts up - it lasts until the speaker stops, limits.max_utterance_ms at most,
    // pre-roll included.
    const byVoice = detail === "vad";
    const seconds = byVoice ? limits.max_utterance_ms / 1000 : Number(detail) || 3;
    const startedAt = performance.now();
    board.listenLeadS = 0;
    const tick = () => {
      const elapsed = (performance.now() - startedAt) / 1000;
      const recorded = Math.min(seconds, (byVoice ? board.listenLeadS : 0) + elapsed);
      elements.talk.style.setProperty("--progress", `${(recorded / seconds) * 100}%`);
      if (byVoice) {
        elements.talkLabel.textContent = `Listening… ${recorded.toFixed(1)}`;
        elements.stateDetail.textContent = `Recording until you stop talking - ${recorded.toFixed(1)} s (${seconds} s at most).`;
      } else {
        const left = seconds - recorded;
        elements.talkLabel.textContent = `Listening… ${left.toFixed(1)}`;
        elements.stateDetail.textContent = `Recording - ${left.toFixed(1)} s left.`;
      }
    };
    tick();
    board.listenTimer = setInterval(tick, 100);
  } else {
    elements.talk.style.removeProperty("--progress");
    showMicLevel(null);
  }
  updateTalkButton();
}

/** Talk is usable only while the assistant is Ready or waiting for speech
    (or the board doesn't report its state at all). Locked while a LISTEN is
    starting, and while a recording downloads (the board would only read the
    LISTEN once the transfer is over). */
function updateTalkButton() {
  const board = state.board;
  const listening = board.shownState === "listening";
  const noReports = link.connected && !board.pipeline && board.legacyFirmware;
  const downloading = state.downloadsRunning > 0;
  const ready = link.connected && !board.talkPending && !downloading &&
    ((board.pipeline && IDLE_STATES.has(board.pipeline.state)) || noReports);
  elements.talk.disabled = !ready;
  document.getElementById("selftest-run").disabled = !ready || noReports || selfTestRunning;
  // readOnly, not disabled: a disabled field loses focus mid-typing, and the next keys would go to the page.
  elements.askText.readOnly = elements.askSend.disabled = !ready || noReports;
  elements.talk.classList.toggle("is-listening", listening);
  // Cancel: only while a LISTEN is recording, waiting for or saying its answer (not the greeting/timer/typed).
  const shown = board.shownState;
  elements.cancelTalk.hidden = !link.connected ||
    !(["listening", "processing", "tool"].includes(shown) || (shown === "speaking" && !(board.pipeline && board.pipeline.detail)));
  if (!listening) {
    elements.talkLabel.textContent = downloading && link.connected ? "Downloading…"
      : board.talkPending ? "Starting…" : ready || !link.connected ? "Talk" : "Busy";
  }
}

function onTalkClick() {
  if (elements.talk.disabled) return;
  if (!elements.audio.paused) pausePlayerForListen();  // before the board starts recording
  const board = state.board;
  board.talkPending = true;
  updateTalkButton();
  sendCommand("LISTEN");
  board.talkPendingTimer = setTimeout(() => {
    board.talkPending = false;
    updateTalkButton();
  }, TALK_START_TIMEOUT_MS);
}

/** Sends a typed question (ASK <text>, firmware step 31+): a new turn in the
    conversation, answered out loud like a spoken one. */
function onAskSubmit(event) {
  event.preventDefault();
  const text = elements.askText.value.replace(/\s+/g, " ").trim();
  if (!text || elements.askSend.disabled) return;
  if (utf8Length(text) > limits.max_ask_bytes) {  // the board would cut it mid-question without saying so
    elements.result.className = "result fail";
    elements.result.textContent = `Too long - the board takes ${limits.max_ask_bytes} bytes (about ${limits.max_ask_bytes} English or ${limits.max_ask_bytes / 2} Hebrew letters).`;
    return;
  }
  const board = state.board;
  board.cycleFirstBubble = null;  // a new turn: its bubbles go after the last one's
  board.cycleFirstSaid = null;
  board.cycleTools.clear();
  settleTimerNote("⏰ Timer not set");
  board.timerOffThisTurn = null;
  board.askPending = text;
  board.askPendingAt = performance.now();
  sendCommand(`ASK ${text}`);
  elements.askText.value = "";
}

/** The board's answer to an ASK it didn't take. */
function noteAskRefused(line) {
  if (line.startsWith("ASK ignored - ")) {
    elements.result.className = "result fail";
    elements.result.textContent = `Typed question not sent: ${line.slice(14)}`;
  } else if (line.startsWith('Unknown command "ASK ')) {
    elements.result.className = "result fail";
    elements.result.textContent = "This board's firmware can't take typed questions yet - flash the latest firmware (ESP32/ESP32.ino).";
  } else {
    return;
  }
  state.board.askPending = null;
}

/** Where dbfs sits on a meter running from min to max, in percent. */
function meterPercent(dbfs, min, max) {
  return Number.isFinite(dbfs) ? Math.max(0, Math.min(100, ((dbfs - min) / (max - min)) * 100)) : 0;
}

/** The meter as a plain mic level (@@MIC_LEVEL, while recording) - null
    empties it. */
function showMicLevel(dbfs) {
  elements.orb.style.setProperty("--level", dbfs === null ? 0 : meterPercent(dbfs, MIC_METER_FLOOR_DBFS, 0) / 100);
  elements.meterName.textContent = "Mic level";
  elements.micFill.classList.remove("quiet");
  elements.micFill.style.width = `${dbfs === null ? 0 : meterPercent(dbfs, MIC_METER_FLOOR_DBFS, 0)}%`;
  elements.meterFloor.hidden = true;
  elements.meterThreshold.hidden = true;
  elements.meterNote.textContent = "";
}

/** The meter while the board waits for speech (@@VAD_LEVEL): the voice-band
    level, green once it's loud enough to count as speech, with ticks at the
    noise floor and at that threshold. */
function showVadLevel(levelDbfs, floorDbfs, thresholdDbfs) {
  elements.meterName.textContent = "Voice level";
  elements.orb.style.setProperty("--level", meterPercent(levelDbfs, VAD_METER_MIN_DBFS, VAD_METER_MAX_DBFS) / 100);
  elements.micFill.style.width = `${meterPercent(levelDbfs, VAD_METER_MIN_DBFS, VAD_METER_MAX_DBFS)}%`;
  elements.micFill.classList.toggle("quiet", !(levelDbfs >= thresholdDbfs));
  elements.meterFloor.style.left = `${meterPercent(floorDbfs, VAD_METER_MIN_DBFS, VAD_METER_MAX_DBFS)}%`;
  elements.meterThreshold.style.left = `${meterPercent(thresholdDbfs, VAD_METER_MIN_DBFS, VAD_METER_MAX_DBFS)}%`;
  elements.meterFloor.hidden = false;
  elements.meterThreshold.hidden = false;
  elements.meterNote.textContent = `Room noise ${floorDbfs} · speech from ${thresholdDbfs} dBFS`;
}

/** Adds a chat bubble with the time it arrived - before `before` when given
    and still shown, otherwise at the end. */
function addBubble(kind, text, before = null) {
  const bubble = document.createElement("div");
  bubble.className = `bubble ${kind}`;
  if (kind === "heard" && text.startsWith("(no words")) bubble.classList.add("empty");
  const who = document.createElement("b");
  const time = document.createElement("time");
  time.textContent = clockStamp(new Date()).slice(0, 8);
  who.append(document.createTextNode(kind === "heard" ? "You" : "Assistant"), time);
  const body = document.createElement("span");
  body.className = "bubble-text";
  body.dir = "auto";  // the page is LTR; a Hebrew transcript needs its own direction or its punctuation lands on the wrong end
  body.textContent = text;
  bubble.append(who, body);
  if (kind === "said" && !state.board.cycleFirstSaid) state.board.cycleFirstSaid = bubble;
  insertConversationItem(bubble, before);
}

/** Adds a small centered note to the conversation - what the board did
    (a tool), or what happened to a sound (ignored, failed). */
function addConversationNote(text, kind = "") {
  const note = document.createElement("div");
  note.className = `action ${kind}`.trim();
  note.textContent = text;
  insertConversationItem(note, null);
  return note;
}

/** Puts a bubble or note into the conversation, keeping the current LISTEN's
    first item known (a late transcription goes before it), and the list short. */
function insertConversationItem(item, before) {
  if (before && before.isConnected) elements.bubbles.insertBefore(item, before);
  else elements.bubbles.appendChild(item);
  if (!state.board.cycleFirstBubble || !state.board.cycleFirstBubble.isConnected || item.nextSibling === state.board.cycleFirstBubble) {
    state.board.cycleFirstBubble = item;
  }
  while (elements.bubbles.childElementCount > MAX_BUBBLES) elements.bubbles.firstChild.remove();
  elements.bubbles.scrollTop = elements.bubbles.scrollHeight;
}

const PART_OF_DAY_ICONS = { day: "☀️", evening: "🌇", night: "🌙" };
/** Drawn icons for the light sensor's part of the day (emoji differ too much between systems at that size). */
const SKY_ICONS = {
  day: '<svg viewBox="0 0 24 24" fill="none" stroke="#e0a100" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4.5" fill="#ffc83a"/>' +
       '<path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M4.9 19.1l1.8-1.8M17.3 6.7l1.8-1.8"/></svg>',
  evening: '<svg viewBox="0 0 24 24" fill="none" stroke="#e0662a" stroke-width="2" stroke-linecap="round"><path d="M6.5 17a5.5 5.5 0 0 1 11 0z" fill="#ff9a4d"/>' +
           '<path d="M2 17h20M12 6.5V9M4.6 9.6l1.7 1.7M19.4 9.6l-1.7 1.7M5 20.5h14"/></svg>',
  night: '<svg viewBox="0 0 24 24" fill="#7f8cff" stroke="#5b67d8" stroke-width="1.5" stroke-linejoin="round"><path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z"/></svg>',
};

/** A tool call from @@STATE tool <name>, as a note: what it did on the board. */
function noteToolInConversation(tool) {
  const board = state.board;
  if (tool === "set_board_state") {
    const on = Boolean(board.led && board.led.on);
    const dimmed = on && board.led.brightness < 100 ? ` at ${board.led.brightness}%` : "";
    addConversationNote(`💡 LED turned ${on ? "on" : "off"}${dimmed} by voice`, "tool");
  } else if (tool === "get_time_of_day") {
    const band = board.light ? board.light.band : null;
    addConversationNote(band
      ? `${PART_OF_DAY_ICONS[band] || "🔆"} Read the light sensor: ${band} (${board.light.value})`
      : "🔆 Read the light sensor", "tool");
  } else if (tool === "set_timer") {
    // A cancel shows at once: the board prints @@TIMER off before this
    // state. A new timer only starts once its spoken confirmation is over,
    // and reports @@TIMER <left> <total> then - so its note is filled in by
    // onTimerLine(), and says "not set" if no timer came (a refused call).
    if (board.timerOffThisTurn) {
      addConversationNote(board.timerOffThisTurn === "cancelled" ? "⏰ Timer cancelled" : "⏰ No timer was running", "tool");
    } else {
      settleTimerNote("⏰ Timer not set");
      board.timerNote = addConversationNote("⏰ Setting a timer…", "tool");
    }
  } else if (tool === "get_board_status") {
    addConversationNote(`📶 Checked the board: Wi-Fi${board.wifiRssi !== null ? ` ${board.wifiRssi} dBm` : ""}, memory, uptime`, "tool");
  } else if (tool === "get_current_time") {
    addConversationNote(`🕒 Read the board's clock`, "tool");
  } else if (tool === "set_volume") {
    addConversationNote(`🔊 Volume set to ${board.volume ? board.volume.percent : "?"}% by voice`, "tool");
  } else if (tool === "set_hands_free") {
    addConversationNote(board.vad
      ? `🎙️ Hands-free ${board.vad.on ? "on - just start talking" : "off - press Talk to ask"} (by voice)`
      : "🎙️ Switched hands-free by voice", "tool");
  } else {
    addConversationNote(`⚙️ Ran ${tool}`, "tool");
  }
}

/** "75" -> "1:15". */
function formatDuration(seconds) {
  return `${Math.floor(seconds / 60)}:${pad(Math.round(seconds % 60))}`;
}

/** @@TIMER <left> <total> [label] | off | done [label] - the voice timer
    or reminder (ESP32/clock_timer_utils.h). */
function onTimerLine([first, second, ...rest]) {
  const board = state.board;
  if (first === "done") {
    const label = [second, ...rest].filter(Boolean).join(" ");
    board.timer = null;
    const lightAction = /^light (on|off)$/.exec(label);  // "Turn off the light in 20 seconds"
    addConversationNote(lightAction ? `⏰ Timer done - the board rings and turns the light ${lightAction[1]}`
      : label ? `⏰ Reminder: ${label} - the board is ringing!` : "⏰ Timer done - the board is ringing!", "tool");
    const chip = document.getElementById("timer-chip");
    chip.hidden = false;
    chip.textContent = "⏰ Ringing!";
    chip.classList.add("ringing");
    setTimeout(() => { chip.classList.remove("ringing"); renderTimer(); }, 4000);
    autoTickDemo("timer");
    if (!board.stages.openai || board.stages.openai.state !== "ok") autoTickDemo("timeroffline");
    return;
  }
  if (first === "off") {
    board.timerOffThisTurn = board.timer ? "cancelled" : "none";
    board.timer = null;
  } else {
    board.timer = { endsAt: performance.now() + Number(first) * 1000, totalS: Number(second) || Number(first), label: rest.join(" ") };
    settleTimerNote(board.timer.label ? `⏰ Reminder set: ${board.timer.label}, in ${formatDuration(board.timer.totalS)}`
      : `⏰ Timer set for ${formatDuration(board.timer.totalS)}`);
  }
  renderTimer();
}

/** Fills in the set_timer note that's still waiting for the board's
    @@TIMER line, if there is one (see noteToolInConversation()). */
function settleTimerNote(text, board = state.board) {
  if (!board.timerNote) return;
  board.timerNote.textContent = text;
  board.timerNote = null;
}

/** The countdown chip next to the state - ticks once a second while a timer runs. */
function renderTimer() {
  const board = state.board;
  const chip = document.getElementById("timer-chip");
  clearInterval(board.timerTicker);
  if (!board.timer) {
    if (!chip.classList.contains("ringing")) chip.hidden = true;
    return;
  }
  const tick = () => {
    if (!board.timer) return;
    const left = Math.max(0, Math.ceil((board.timer.endsAt - performance.now()) / 1000));
    chip.hidden = false;
    chip.textContent = `⏰ ${board.timer.label ? `${board.timer.label} · ` : ""}${formatDuration(left)}`;
  };
  tick();
  board.timerTicker = setInterval(tick, 1000);
}

/** Hands-free sounds that never became a question, as notes. */
function noteVadEventInConversation([kind, first, second]) {
  if (kind === "discarded") {
    addConversationNote(`🔇 Ignored a sound (${formatSeconds(Number(second) / 1000)}) - ${DISCARD_TEXT[first] || first}`);
  } else if (kind === "paused") {
    addConversationNote(`⏸ Several noises in a row - hands-free paused for ${Math.round(Number(first) / 1000)} s`);
  }
}

/** Adds "answered in X s" under the assistant's first bubble of the LISTEN
    that @@TIMING just measured. */
function showAnswerTime(fields) {
  const bubble = state.board.cycleFirstSaid;
  const firstAudioMs = Number(fields.first_audio_after_rec_ms);
  if (!bubble || !bubble.isConnected || !(firstAudioMs >= 0) || bubble.querySelector(".answer-time")) return;
  const line = document.createElement("span");
  line.className = "answer-time";
  line.textContent = `⏱ answered in ${(firstAudioMs / 1000).toFixed(2)} s` + (fields.mode === "vad" ? " · hands-free" : "");
  bubble.appendChild(line);
}

function showResult(passed, listenNumber, reason) {
  elements.result.className = `result ${passed ? "ok" : "fail"}`;
  elements.result.textContent = passed ? `LISTEN #${listenNumber} passed.` : `LISTEN #${listenNumber} failed: ${reason}`;
  if (!passed) {
    const board = state.board;
    addConversationNote(`⚠️ LISTEN #${listenNumber} failed: ${reason || "no reason given"}`, "fail");
    showPipelineState("error", reason);
    clearTimeout(board.errorTimer);
    board.errorTimer = setTimeout(() => {
      if (board.shownState === "error" && board.pipeline) showPipelineState(board.pipeline.state, board.pipeline.detail);
    }, ERROR_SHOWN_MS);
  }
}

// ===== Device controls (step 28) =====

/** The controls work once connected and the board has reported that
    control's state (so a slider never starts from a made-up value). */
function updateControlsEnabled() {
  const board = state.board;
  const connected = link.connected;
  elements.volume.disabled = !connected || !board.volume || board.volume.muted;
  elements.mute.disabled = !connected || !board.volume;
  elements.beep.disabled = !connected;
  elements.led.disabled = !connected || !board.led;
  elements.ledBrightness.disabled = !connected || !board.led;
  elements.lightValue.disabled = !connected || !board.light;
  elements.lightMode.disabled = !connected || !board.light;
  elements.eveningMin.disabled = !connected || !board.light;
  elements.dayMin.disabled = !connected || !board.light;
  elements.applyBands.disabled = !connected || !board.light;
  elements.vad.disabled = !connected || !board.vad;
  elements.vadSensitivity.disabled = !connected || !board.vad;
  elements.vadHangover.disabled = !connected || !board.vad;
  elements.talkButton.disabled = !connected || board.talkButton === undefined;
  elements.statusSounds.disabled = !connected || board.statusSounds === undefined;
  for (const input of [elements.wifiSsid, elements.wifiPassword, elements.wifiSave, elements.wifiForget]) {
    input.disabled = !connected || !board.wifiNet;
  }
  for (const input of [elements.apiKeyInput, elements.apiKeySave, elements.apiKeyForget]) {
    input.disabled = !connected || !board.apiKey;
  }
}

/** The board-only options (BOOT button, status sounds), the Wi-Fi card and
    the API key card, each shown once the board has reported it (older
    firmware doesn't). */
function renderBoardOptions() {
  const board = state.board;
  const hasButton = board.talkButton !== undefined;
  const hasSounds = board.statusSounds !== undefined;
  elements.talkButtonOption.hidden = !hasButton;
  elements.statusSoundsOption.hidden = !hasSounds;
  elements.boardOptionsControl.hidden = !hasButton && !hasSounds;
  elements.talkButton.checked = Boolean(board.talkButton);
  elements.statusSounds.checked = Boolean(board.statusSounds);
  elements.wifiControl.hidden = !board.wifiNet;
  if (board.wifiNet) {
    elements.wifiNet.textContent = `${board.wifiNet.ssid} (${board.wifiNet.source === "saved" ? "saved here" : "default"})`;
  }
  elements.apiKeyControl.hidden = !board.apiKey;
  if (board.apiKey) {
    elements.apiKeyInUse.textContent = `${board.apiKey.hint} (${board.apiKey.source === "saved" ? "saved here" : "default"})`;
  }
  updateControlsEnabled();
}

/** The hint the board shows for a key - its first 3 and last 4 characters,
    as openai_realtime_utils.cpp's describeActiveKey() writes it. */
function apiKeyHint(key) {
  return `${key.slice(0, 3)}...${key.slice(-4)}`;
}

/** Saves an OpenAI API key on the board (OPENAI_KEY <key>), which then
    reconnects with it. The key never reaches the log. "Saved" is shown once
    the board answers @@OPENAI_KEY saved <hint> (see handleStatusLine); a
    refusal reaches noteApiKeyRefused. */
async function saveApiKey() {
  const key = elements.apiKeyInput.value.trim();  // a paste often brings a space or a line break along
  if (!key) {
    elements.apiKeyNote.textContent = "Paste the key first.";
    return;
  }
  if (key.length < limits.min_api_key_bytes || key.length > limits.max_api_key_bytes || !API_KEY_PATTERN.test(key)) {
    elements.apiKeyNote.textContent = `That doesn't look like an OpenAI API key: it starts with sk-, has ${limits.min_api_key_bytes} to ` +
      `${limits.max_api_key_bytes} characters, and only letters, digits, - and _.`;
    return;
  }
  const board = state.board;
  if (board.apiKeySavePending) clearTimeout(board.apiKeySavePending.timer);
  const pending = { hint: apiKeyHint(key), timer: null };
  board.apiKeySavePending = pending;
  elements.apiKeyNote.textContent = `Sent - waiting for the board to save ${pending.hint}…`;
  try {
    await link.send(`OPENAI_KEY ${key}`);
    appendLog(">> OPENAI_KEY ********");
    elements.apiKeyInput.value = "";
    if (state.board.apiKeySavePending === pending) {  // not answered while sending
      // Still starting up: setup() reads no commands in its Wi-Fi and clock waits.
      const booting = !state.board.pipeline || state.board.pipeline.state === "booting";
      if (booting) elements.apiKeyNote.textContent = "Sent - the board reads it once its Wi-Fi and clock waits end (up to 35 s)…";
      pending.timer = setTimeout(() => {
        if (state.board.apiKeySavePending !== pending) return;
        state.board.apiKeySavePending = null;
        elements.apiKeyNote.textContent = "The board didn't confirm the new key - check the serial terminal.";
      }, booting ? WIFI_ACK_BOOT_TIMEOUT_MS : WIFI_ACK_TIMEOUT_MS);
    }
  } catch (error) {
    if (state.board.apiKeySavePending === pending) state.board.apiKeySavePending = null;
    elements.apiKeyNote.textContent = `Not sent: ${error.message}`;
  }
}

/** The board's answer to an OPENAI_KEY it didn't take. */
function noteApiKeyRefused(line) {
  if (!line.startsWith("OPENAI_KEY ignored")) return;
  const pending = state.board.apiKeySavePending;
  if (!pending) return;
  clearTimeout(pending.timer);
  state.board.apiKeySavePending = null;
  elements.apiKeyNote.textContent = `Not saved: ${line}`;
}

/** Saves a Wi-Fi network on the board: WIFI_SSID, then WIFI_PASSWORD - which
    saves both and connects. The password never reaches the log. "Saved" is
    shown once the board answers @@WIFI_NET saved <ssid> (see
    handleStatusLine); a refusal reaches noteWifiRefused. */
async function saveWifiNetwork() {
  const ssid = elements.wifiSsid.value.trim();
  const password = elements.wifiPassword.value;
  if (!ssid) {
    elements.wifiNote.textContent = "Type the network's name first.";
    return;
  }
  if (utf8Length(ssid) > limits.max_ssid_bytes) {
    elements.wifiNote.textContent = `The network name is too long - ${limits.max_ssid_bytes} bytes at most (a Hebrew letter takes 2).`;
    return;
  }
  // Spaces at either end are part of the password and are sent as typed; a
  // password of only spaces is "no password" to the board (an open network).
  const passwordBytes = password.trim() === "" ? 0 : utf8Length(password);
  if (passwordBytes > 0 && (passwordBytes < limits.min_password_bytes || passwordBytes > limits.max_password_bytes)) {
    elements.wifiNote.textContent = `A Wi-Fi password is ${limits.min_password_bytes} to ${limits.max_password_bytes} bytes (or none for an open network).`;
    return;
  }
  const board = state.board;
  if (board.wifiSavePending) clearTimeout(board.wifiSavePending.timer);
  const pending = { ssid, timer: null };
  board.wifiSavePending = pending;
  elements.wifiNote.textContent = `Sent - waiting for the board to save ${ssid}…`;
  try {
    await link.send(`WIFI_SSID ${ssid}`);
    appendLog(`>> WIFI_SSID ${ssid}`);
    // One separator space, then the password untouched (the board keeps its edge spaces).
    await link.send(passwordBytes > 0 ? `WIFI_PASSWORD ${password}` : "WIFI_PASSWORD");
    appendLog(passwordBytes > 0 ? ">> WIFI_PASSWORD ********" : ">> WIFI_PASSWORD");
    elements.wifiPassword.value = "";
    if (state.board.wifiSavePending === pending) {  // not answered while sending
      // Still starting up (no @@STATE yet, or "booting"): the board may be in
      // its boot Wi-Fi wait, and saves the network only once that ends.
      const booting = !state.board.pipeline || state.board.pipeline.state === "booting";
      if (booting) elements.wifiNote.textContent = "Sent - the board reads it once its Wi-Fi wait ends (up to 20 s)…";
      pending.timer = setTimeout(() => {
        if (state.board.wifiSavePending !== pending) return;
        state.board.wifiSavePending = null;
        elements.wifiNote.textContent = "The board didn't confirm the new network - check the serial terminal.";
      }, booting ? WIFI_ACK_BOOT_TIMEOUT_MS : WIFI_ACK_TIMEOUT_MS);
    }
  } catch (error) {
    if (state.board.wifiSavePending === pending) state.board.wifiSavePending = null;
    elements.wifiNote.textContent = `Not sent: ${error.message}`;
  }
}

/** The board's answer to a WIFI_SSID / WIFI_PASSWORD it didn't take. Only
    the first refusal is shown: after a refused WIFI_SSID the password is
    refused too, with a less useful reason. */
function noteWifiRefused(line) {
  if (!line.startsWith("WIFI_SSID ignored") && !line.startsWith("WIFI_PASSWORD ignored")) return;
  const pending = state.board.wifiSavePending;
  if (!pending) return;
  clearTimeout(pending.timer);
  state.board.wifiSavePending = null;
  elements.wifiNote.textContent = `Not saved: ${line}`;
}

/** Sends a control command once the user pauses dragging a slider. */
function sendSoon(key, command) {
  sendSoon.timers = sendSoon.timers || {};
  clearTimeout(sendSoon.timers[key]);
  sendSoon.timers[key] = setTimeout(() => sendCommand(command), CONTROL_SEND_DELAY_MS);
}

/** True while the user is dragging or focused on this control, so a report
    from the board doesn't yank it out from under them. */
function isBeingEdited(input) {
  return document.activeElement === input && input.dataset.dragging === "true";
}

function renderVolume() {
  const volume = state.board.volume;
  if (volume && !isBeingEdited(elements.volume)) elements.volume.value = volume.percent;
  elements.mute.checked = Boolean(volume && volume.muted);
  elements.volumeValue.textContent = !volume ? "-" : volume.muted ? "muted" : `${volume.percent}%`;
  if (!volume) elements.volumeNote.textContent = "";
  updateControlsEnabled();
}

function renderLed() {
  const led = state.board.led;
  const bulb = document.getElementById("bulb");
  bulb.dataset.on = String(Boolean(led && led.on));
  bulb.style.setProperty("--glow", led ? Math.max(0.15, led.brightness / 100) : 1);
  elements.led.checked = Boolean(led && led.on);
  if (led && !isBeingEdited(elements.ledBrightness)) elements.ledBrightness.value = led.brightness;
  elements.ledBrightnessValue.textContent = led ? `${led.brightness}%` : "-";
  const byVoice = Boolean(led && led.source === "voice");
  elements.ledChip.hidden = !byVoice;
  if (byVoice) {
    elements.ledControl.classList.remove("flash");
    void elements.ledControl.offsetWidth;  // restart the animation
    elements.ledControl.classList.add("flash");
  }
  updateControlsEnabled();
}

/** The hands-free switch and its settings, from the board's @@VAD - all
    hidden while the board hasn't sent one (firmware before step 30). */
function renderVad() {
  const vad = state.board.vad;
  elements.handsFree.hidden = !vad;
  elements.vadSettings.hidden = !vad;
  elements.meterNote.hidden = !vad;
  elements.stepArmed.hidden = !(vad && vad.on);
  elements.vad.checked = Boolean(vad && vad.on);
  elements.vadSettings.classList.toggle("off", !(vad && vad.on));
  if (vad && !isBeingEdited(elements.vadSensitivity)) elements.vadSensitivity.value = vad.sensitivity;
  if (vad && !isBeingEdited(elements.vadHangover)) elements.vadHangover.value = vad.hangoverMs;
  elements.vadSensitivityValue.textContent = vad ? `${vad.sensitivity} of ${limits.vad_sensitivity_max}` : "-";
  elements.vadHangoverValue.textContent = vad ? formatSeconds(vad.hangoverMs / 1000) : "-";
  updateControlsEnabled();
}

function partOfDay(value, eveningMin, dayMin) {
  return value >= dayMin ? "day" : value >= eveningMin ? "evening" : "night";
}

/** Draws the night/evening/day bar and the reading's needle. */
function drawLightBands(value, eveningMin, dayMin) {
  elements.bandNight.style.width = `${(eveningMin / limits.light_max) * 100}%`;
  elements.bandEvening.style.width = `${((dayMin - eveningMin) / limits.light_max) * 100}%`;
  elements.bandDay.style.width = `${((limits.light_max - dayMin) / limits.light_max) * 100}%`;
  elements.lightNeedle.style.left = `calc(${(value / limits.light_max) * 100}% - 1.5px)`;
  elements.lightReading.textContent = value;
  const band = partOfDay(value, eveningMin, dayMin);
  const icon = document.getElementById("light-icon");
  if (elements.lightBand.textContent !== band && elements.lightBand.textContent !== "-") {
    icon.classList.remove("changed");
    void icon.offsetWidth;  // restart the animation
    icon.classList.add("changed");
  }
  icon.innerHTML = SKY_ICONS[band];
  elements.lightBand.textContent = band;
}

function renderLight() {
  const light = state.board.light;
  if (light) {
    if (!isBeingEdited(elements.lightValue)) elements.lightValue.value = light.value;
    if (document.activeElement !== elements.eveningMin) elements.eveningMin.value = light.eveningMin;
    if (document.activeElement !== elements.dayMin) elements.dayMin.value = light.dayMin;
    drawLightBands(light.value, light.eveningMin, light.dayMin);
  } else {
    drawLightBands(Number(elements.lightValue.value), Number(elements.eveningMin.value), Number(elements.dayMin.value));
    elements.lightReading.textContent = "-";
    elements.lightBand.textContent = "-";
    document.getElementById("light-icon").textContent = "";
  }
  updateControlsEnabled();
}

function onApplyBandsClick() {
  const eveningMin = Number(elements.eveningMin.value);
  const dayMin = Number(elements.dayMin.value);
  const valid = Number.isInteger(eveningMin) && Number.isInteger(dayMin) && eveningMin >= 0 &&
    dayMin <= limits.light_max && eveningMin < dayMin;
  if (!valid) {
    elements.lightNote.textContent = `Evening must start below day, both whole numbers from 0 to ${limits.light_max}.`;
    return;
  }
  elements.lightNote.textContent = "Saved on the board - kept after a restart.";
  sendCommand(`LIGHT_BANDS ${eveningMin} ${dayMin}`);
}

/** The auto day-cycle's simulated clock (hours, 0-24) and its ticker. */
const dayCycle = { hour: DAY_CYCLE_START_HOUR, ticker: null };

/** Switches the simulated light sensor between "manual" (the slider) and
    "auto" (the day-cycle, stepDayCycle() every DAY_CYCLE_SEND_MS). */
function setLightMode(mode) {
  elements.lightMode.value = mode;
  clearInterval(dayCycle.ticker);
  dayCycle.ticker = null;
  if (mode === "auto") {
    dayCycle.ticker = setInterval(stepDayCycle, DAY_CYCLE_SEND_MS);
    stepDayCycle();
  } else {
    elements.lightClock.textContent = "";
  }
}

/** One step of the auto day-cycle: moves the simulated clock on, sends the
    light level for that time (LIGHT <v>) and moves the slider with it.
    Paused - the clock stands still - while no board is connected or it
    hasn't reported its light sensor yet, while a recording downloads, and
    during a log replay. */
function stepDayCycle() {
  const light = state.board.light;
  const paused = !link.connected || !light || state.downloadsRunning > 0 || state.replayActive;
  if (!paused) {
    dayCycle.hour = (dayCycle.hour + (DAY_CYCLE_SEND_MS / DAY_CYCLE_PERIOD_MS) * 24) % 24;
    const value = dayCycleLight(dayCycle.hour);
    elements.lightValue.value = value;
    drawLightBands(value, light.eveningMin, light.dayMin);
    sendQuietly(`LIGHT ${value}`);
  }
  const minutes = Math.floor(dayCycle.hour * 60);
  elements.lightClock.textContent = `🕒 ${pad(Math.floor(minutes / 60))}:${pad(minutes % 60)}${paused ? " (paused)" : ""}`;
}

/** The light level at a time of day (hours): dark at night, a smooth ramp
    at sunrise and sunset (around 6:00 and 18:00) through the evening and
    day bands, bright at noon - plus a real ADC reading's jitter. */
function dayCycleLight(hour) {
  const sunHeight = -Math.cos((hour / 24) * 2 * Math.PI);  // -1 at midnight, 0 at 6:00 and 18:00, 1 at noon
  const daylight = 1 / (1 + Math.exp(-DAY_CYCLE_STEEPNESS * sunHeight));
  const level = (DAY_CYCLE_NIGHT_LEVEL + (DAY_CYCLE_NOON_LEVEL - DAY_CYCLE_NIGHT_LEVEL) * daylight) * limits.light_max;
  const jitter = (Math.random() + Math.random() - 1) * DAY_CYCLE_NOISE_COUNTS * 1.5;  // most often near 0, like averaged ADC noise
  return Math.round(Math.min(limits.light_max, Math.max(0, level + jitter)));
}

function onTerminalToggle() {
  const collapsed = elements.boardPanel.classList.toggle("collapsed");
  elements.toggleTerminal.textContent = collapsed ? "Show" : "Hide";
  elements.toggleTerminal.setAttribute("aria-expanded", String(!collapsed));
}

/** Space presses Talk, unless typing or using another control. */
function onKeyDown(event) {
  if (event.code !== "Space" || event.repeat || event.ctrlKey || event.altKey || event.metaKey) return;
  const target = event.target;
  if (target.closest && target.closest("input, textarea, select, button, a, audio, [contenteditable]")) return;
  event.preventDefault();
  onTalkClick();
}

// ===== Statistics =====

/** Counters for the Statistics panel, from the board's @@TIMING / @@LED /
    @@LIGHT / @@VAD_EVENT / @@BOOT_TIMING lines. Kept in the page only. */
let usage = newUsage();

/** Empty counters. */
function newUsage() {
  return { turns: 0, failed: 0, handsFree: 0, responseMs: [], ledByVoice: 0, timeOfDayAsks: 0, volumeByVoice: 0,
           lightBand: null, lightChanges: 0, noisesIgnored: 0, bootMs: null,
           boardSession: 1,           // counts the board's restarts seen: its command number n starts at 1 again after each
           turnsThisBoardSession: 0,
           history: [] };  // one entry per @@TIMING: { n, boardSession, at, mode, tool, result, serverMs, firstSoundMs, doneMs }
}

/** Starts a new board session for the command numbers when the board
    restarted (@@STATE booting) after answering something. */
function onUsageBoardBooting() {
  if (usage.turnsThisBoardSession === 0) return;
  usage.boardSession++;
  usage.turnsThisBoardSession = 0;
}

/** A command's label, unique across board restarts: "#7", or "#2.7" (the
    7th command after the board's 2nd start) once it has restarted. */
function turnLabel(turn) {
  return usage.boardSession > 1 ? `#${turn.boardSession}.${turn.n}` : `#${turn.n}`;
}

/** "a=1 b=x" -> { a: "1", b: "x" }. */
function parseKeyValues(text) {
  const fields = {};
  for (const pair of text.trim().split(/\s+/)) {
    const at = pair.indexOf("=");
    if (at > 0) fields[pair.slice(0, at)] = pair.slice(at + 1);
  }
  return fields;
}

/** One answered (or failed) turn from its @@TIMING line. */
function onUsageTiming(fields) {
  usage.turns++;
  if (fields.result !== "pass") usage.failed++;
  if (fields.mode === "vad") usage.handsFree++;
  if (fields.tool === "get_time_of_day") usage.timeOfDayAsks++;
  if (fields.tool === "set_volume") usage.volumeByVoice++;
  const firstAudioMs = Number(fields.first_audio_after_rec_ms);
  if (firstAudioMs >= 0) usage.responseMs.push(firstAudioMs);  // -1: the turn had no reply audio
  const number = (key) => (Number(fields[key]) >= 0 ? Number(fields[key]) : null);
  const n = Number(fields.n) || usage.turns;
  const previous = usage.history[usage.history.length - 1];
  // A restart that happened while no board was connected shows as the number going back.
  if (previous && previous.boardSession === usage.boardSession && n <= previous.n) onUsageBoardBooting();
  usage.turnsThisBoardSession++;
  usage.history.push({
    n, boardSession: usage.boardSession, at: new Date(), mode: fields.mode || "button", tool: fields.tool || "-",
    result: fields.result || "?", serverMs: number("first_event_after_rec_ms"), firstSoundMs: number("first_audio_after_rec_ms"),
    doneMs: number("playback_done_after_rec_ms"),
  });
  renderUsage();
}

/** Counts a change of the light sensor's part of the day (the first reading only sets it). */
function onUsageLight(band) {
  if (usage.lightBand !== null && band !== usage.lightBand) usage.lightChanges++;
  usage.lightBand = band;
  renderUsage();
}

/** Adds one to a plain counter. */
function countUsage(name) {
  usage[name]++;
  renderUsage();
}

/** Redraws the Statistics panel. */
function renderUsage() {
  const seconds = (ms) => `${(ms / 1000).toFixed(2)} s`;
  const times = usage.responseMs;
  const average = times.length ? times.reduce((sum, ms) => sum + ms, 0) / times.length : null;
  const rows = [
    ["Commands", `${usage.turns}` + (usage.failed ? ` (${usage.failed} failed)` : "")],
    ["Hands-free / button", `${usage.handsFree} / ${usage.turns - usage.handsFree}`],
    ["Average response time", average === null ? "-" : seconds(average)],
    ["Fastest / slowest", times.length ? `${seconds(Math.min(...times))} / ${seconds(Math.max(...times))}` : "-"],
    ["LED changed by voice", `${usage.ledByVoice}`],
    ["Part-of-day questions", `${usage.timeOfDayAsks}`],
    ["Volume changed by voice", `${usage.volumeByVoice}`],
    ["Light sensor changes", `${usage.lightChanges}` + (usage.lightBand ? ` (now ${usage.lightBand})` : "")],
    ["Noises ignored", `${usage.noisesIgnored}`],
    ["Last boot time", usage.bootMs === null ? "-" : seconds(usage.bootMs)],
  ];
  renderResponseChart();
  if (!document.getElementById("summary").hidden) renderSummary();
  const list = document.getElementById("usage-stats");
  list.replaceChildren(...rows.map(([label, value]) => {
    const item = document.createElement("div");
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = label;
    detail.textContent = value;
    item.append(term, detail);
    return item;
  }));
}

const CHART_MAX_BARS = 40;
const SVG_NS = "http://www.w3.org/2000/svg";

/** Makes an SVG element with these attributes. */
function svgElement(name, attributes = {}, text = null) {
  const element = document.createElementNS(SVG_NS, name);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
  if (text !== null) element.textContent = text;
  return element;
}

/** A bar segment's outline - a rectangle, its top corners rounded when it's the top of the bar. */
function barPath(x, y, width, height, roundTop) {
  const r = roundTop ? Math.min(4, width / 2, height) : 0;
  return `M${x},${y + height}V${y + r}` + (r ? `Q${x},${y} ${x + r},${y}` : "") +
         `H${x + width - r}` + (r ? `Q${x + width},${y} ${x + width},${y + r}` : "") + `V${y + height}Z`;
}

/** Stacked bars of the last CHART_MAX_BARS commands' response times, split
    at the moment OpenAI started answering, with the median as a dashed line. */
function renderResponseChart() {
  const container = document.getElementById("response-chart");
  const turns = usage.history.slice(-CHART_MAX_BARS);
  if (!turns.length) {
    container.innerHTML = '<div class="chart-empty">No commands yet - each answer\'s timing shows up here.</div>';
    return;
  }
  const width = Math.max(320, container.clientWidth || 800);
  const height = 190, left = 40, right = 8, top = 16, bottom = 22;
  const plotW = width - left - right, plotH = height - top - bottom;
  const longestS = Math.max(1, ...turns.map((turn) => (turn.firstSoundMs ?? turn.serverMs ?? 0) / 1000));
  const stepS = longestS > 6 ? 2 : longestS > 3 ? 1 : 0.5;
  const maxS = Math.ceil(longestS / stepS) * stepS;
  const yOf = (ms) => top + plotH - (ms / 1000 / maxS) * plotH;
  const slot = plotW / CHART_MAX_BARS;
  const barW = Math.max(4, Math.min(22, slot * 0.62));

  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img",
    "aria-label": `Response time of the last ${turns.length} commands` });
  for (let s = 0; s <= maxS + 1e-9; s += stepS) {
    svg.append(svgElement("line", { class: "grid", x1: left, x2: width - right, y1: yOf(s * 1000), y2: yOf(s * 1000) }));
    svg.append(svgElement("text", { class: "axis-label", x: left - 6, y: yOf(s * 1000) + 4, "text-anchor": "end" },
      `${Number.isInteger(s) ? s : s.toFixed(1)} s`));
  }
  const tip = document.createElement("div");
  tip.className = "chart-tip";
  tip.hidden = true;
  const labelEvery = Math.ceil(turns.length / 10);
  turns.forEach((turn, index) => {
    const x = left + index * slot + (slot - barW) / 2;
    const group = svgElement("g");
    const server = turn.serverMs, sound = turn.firstSoundMs;
    if (server !== null && sound !== null && sound >= server) {
      const gap = sound - server > 0 ? 1 : 0;  // 2px surface gap between the two segments
      svg.append(svgElement("path", { d: barPath(x, yOf(server) + gap, barW, yOf(0) - yOf(server) - gap, false), fill: "var(--seg-server)" }));
      if (gap) svg.append(svgElement("path", { d: barPath(x, yOf(sound), barW, Math.max(1, yOf(server) - yOf(sound) - gap), true), fill: "var(--seg-sound)" }));
    } else if (sound !== null) {
      svg.append(svgElement("path", { d: barPath(x, yOf(sound), barW, yOf(0) - yOf(sound), true), fill: "var(--seg-sound)" }));
    } else {
      svg.append(svgElement("text", { class: "fail-mark", x: x + barW / 2, y: yOf(0) - 4, "text-anchor": "middle" }, "✕"));
    }
    const icon = { set_board_state: "💡", get_time_of_day: "🔆", set_volume: "🔊", set_timer: "⏰", get_current_time: "🕒", get_board_status: "📶" }[turn.tool] || "";
    if (icon && sound !== null) svg.append(svgElement("text", { x: x + barW / 2, y: yOf(sound) - 4, "text-anchor": "middle", "font-size": 11 }, icon));
    if (index % labelEvery === 0 || index === turns.length - 1) {
      svg.append(svgElement("text", { class: "axis-label", x: x + barW / 2, y: height - 6, "text-anchor": "middle" }, turnLabel(turn)));
    }
    const hit = svgElement("rect", { class: "hit", x: left + index * slot, y: top, width: slot, height: plotH, rx: 3 });
    hit.addEventListener("pointerenter", () => {
      const seconds = (ms) => (ms === null ? "-" : `${(ms / 1000).toFixed(2)} s`);
      const toolText = { set_board_state: "LED by voice", get_time_of_day: "part of day", set_volume: "volume by voice",
                         set_timer: "timer", get_current_time: "clock time", get_board_status: "board status" }[turn.tool];
      tip.innerHTML = "";
      const title = document.createElement("b");
      title.textContent = `Command ${turnLabel(turn)} · ${clockStamp(turn.at).slice(0, 8)}`;
      const lines = [
        ["var(--seg-server)", `OpenAI started answering: ${seconds(server)}`],
        ["var(--seg-sound)", `First sound of the reply: ${seconds(sound)}`],
        [null, `Reply finished playing: ${seconds(turn.doneMs)}`],
        [null, `${turn.mode === "vad" ? "Hands-free" : "Talk button"}${toolText ? ` · ${toolText}` : ""}${turn.result !== "pass" ? " · failed" : ""}`],
      ];
      tip.append(title, ...lines.map(([color, text]) => {
        const line = document.createElement("div");
        if (color) { const swatch = document.createElement("i"); swatch.style.background = color; line.append(swatch); }
        line.append(text);
        return line;
      }));
      tip.hidden = false;
      const barCenter = ((left + (index + 0.5) * slot) / width) * container.clientWidth;
      tip.style.left = `${Math.min(Math.max(0, barCenter - 90), container.clientWidth - 190)}px`;
      tip.style.top = "0px";
    });
    hit.addEventListener("pointerleave", () => { tip.hidden = true; });
    group.append(hit);
    svg.append(group);
  });
  const sounds = turns.map((turn) => turn.firstSoundMs).filter((ms) => ms !== null).sort((a, b) => a - b);
  if (sounds.length >= 2) {
    const middle = Math.floor(sounds.length / 2);
    const median = sounds.length % 2 ? sounds[middle] : (sounds[middle - 1] + sounds[middle]) / 2;
    svg.append(svgElement("line", { class: "median", x1: left, x2: width - right, y1: yOf(median), y2: yOf(median) }));
    svg.append(svgElement("text", { class: "median-label", x: width - right, y: yOf(median) - 5, "text-anchor": "end" },
      `median ${(median / 1000).toFixed(2)} s`));
  }
  container.replaceChildren(svg, tip);
}

/** Saves every command's timing in Statistics (since the page opened, or
    Reset) as a CSV file. index is the row's own number; board_session and
    board_command identify the command on the board, whose numbering starts
    over after a restart. UTF-8 with a BOM, so Excel reads any non-ASCII text. */
function onUsageExportClick() {
  const header = "index,date,time,board_session,board_command,mode,tool,result,openai_started_ms,first_sound_ms,reply_done_ms";
  const field = (value) => {
    const text = String(value);
    return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
  };
  const rows = usage.history.map((turn, index) => [index + 1,
    `${turn.at.getFullYear()}-${pad(turn.at.getMonth() + 1)}-${pad(turn.at.getDate())}`, clockStamp(turn.at).slice(0, 8),
    turn.boardSession, turn.n, turn.mode, turn.tool, turn.result,
    turn.serverMs ?? "", turn.firstSoundMs ?? "", turn.doneMs ?? ""].map(field).join(","));
  const blob = new Blob(["\uFEFF" + [header, ...rows].join("\r\n") + "\r\n"], { type: "text/csv;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const anchor = Object.assign(document.createElement("a"), { href: url, download: `esp32_statistics_${fileStamp(new Date())}.csv` });
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

document.getElementById("usage-export").addEventListener("click", onUsageExportClick);

/** Downloads the conversation as plain text: one line per bubble ("[time]
    You: ..."), with the notes about what the board did indented under them. */
function onSaveConversationClick() {
  const lines = [...elements.bubbles.children].map((item) => {
    if (!item.classList.contains("bubble")) return `           · ${item.textContent}`;
    const time = item.querySelector("time");
    const who = item.classList.contains("heard") ? "You" : "Assistant";
    const body = item.querySelector(".bubble-text");
    const words = body ? body.textContent : "";
    const answered = item.querySelector(".answer-time");
    return `[${time ? time.textContent : "--:--:--"}] ${who}: ${words.trim()}${answered ? `  (${answered.textContent.trim()})` : ""}`;
  });
  if (!lines.length) return;
  const blob = new Blob([lines.join("\n") + "\n"], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const anchor = Object.assign(document.createElement("a"), { href: url, download: `esp32_conversation_${fileStamp(new Date())}.txt` });
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

document.getElementById("save-conversation").addEventListener("click", onSaveConversationClick);

/** Example requests for whoever is trying the assistant - [icon, phrase].
    Shown as a list to read, not buttons: Talk (or Hands-free) asks. */
const EXAMPLE_PHRASES = [
  ["❓", "What can you do?"],
  ["💬", "What is the capital of France?"],
  ["💡", "Turn on the light"],
  ["💡", "Dim the light to 30 percent"],
  ["🌗", "Is it day or night?"],
  ["🧠", "My name is Dana"],
  ["🧠", "What is my name?"],
  ["🔊", "Set the volume to 30 percent"],
  ["🔊", "A bit louder"],
  ["⏰", "Set a timer for 30 seconds"],
  ["📝", "Remind me in 30 seconds to check the oven"],
  ["🌙", "Turn off the light in 20 seconds"],
  ["🕒", "What time is it?"],
  ["📶", "How are you doing?"],
  ["🎙️", "Switch to hands-free mode"],
];
document.getElementById("phrases").replaceChildren(...EXAMPLE_PHRASES.map(([icon, phrase]) => {
  const item = document.createElement("li");
  item.className = "phrase";
  item.append(Object.assign(document.createElement("span"), { textContent: icon, ariaHidden: "true" }), `“${phrase}”`);
  return item;
}));
document.getElementById("connection-alert-close").addEventListener("click", hideAlert);

// ===== Demo checklist =====

/** The demo's order: the numbered stories of Documentation/user-stories.md, then the edge cases of Documentation/edge-cases-extended.md the plan (Documentation/demo-plan.md) shows - each title carries its number there: [id, title, how]. */
const DEMO_SECTIONS = [
  ["סיפורי משתמש", [
    ["intro", "הצגת הפרויקט (דקה, בלי מצגת)", "משפט אחד על המטרה. לומר: חיישן האור מדומה בסליידר, ועברית עובדת אבל פחות טוב."],
    ["boot", "1. הדלקה ונורות תקינות", "Reset ללוח מול הבוחנים. Connect board, הנורות נהיות ירוקות אחת אחת וברכת פתיחה."],
    ["talk", "2. שאלה בלחיצה", "Talk או רווח: <code>What is the capital of France</code>. לתת לבוחן לבחור דוגמה מ-Try saying ולשאול אותה."],
    ["transcript", "3. רואים מה שמע ומה ענה", "המצב משתנה (Listening, Processing, Speaking), והשיחה בבועות עם שעה וזמן תגובה. תגיות: 💡 🌙 🔊 ⏰ 🕒 📶."],
    ["led", "4. הדלקת אור בקול", "<code>Turn on the light</code>, <code>Dim the light to 30 percent</code>, ואז <code>Turn off the light</code>. הנורה במסך זוהרת לפי הבהירות."],
    ["voicevolume", "5. עוצמה בקול", "<code>Set the volume to 30 percent</code>, ואז <code>A bit louder</code>."],
    ["timer", "6. תזכורת, טיימר ושעה", "<code>Remind me in 30 seconds to check the oven</code>: ספירה לאחור במסך, ובסוף 3 צפצופים, LED מהבהב ו-<code>Time's up - check the oven</code>. אחר כך <code>Turn off the light in 20 seconds</code>: האור נכבה בסוף הטיימר. ואז <code>What time is it</code>."],
    ["daynight", "7. חלק היום לפי חיישן האור", "מזיזים את סליידר Light sensor לערך נמוך, <code>Is it day or night</code> (night), ואז מעלים ושואלים שוב."],
    ["status", "8. איך הלוח מרגיש", "<code>How are you doing</code>: עוצמת ה-Wi-Fi, כמה זמן הלוח דלוק וכמה זיכרון פנוי. תגית 📶 בשיחה."],
    ["handsfree", "9. Hands-free בלי כפתור", "מדליקים את המתג ומדברים. הוא מתחיל ועוצר לבד."],
    ["memory", "10. זיכרון שיחה", "<code>My name is Omri</code> ואז <code>What is my name</code>"],
    ["hebrew", "11. עברית", "<code>מה בירת צרפת</code>: עונה בעברית. התמלול בבועה יכול להיות לא מדויק."],
    ["typed", "12. שאלה בכתב", "בשדה <code>Or type a question…</code> ולוחצים Ask. עונה בקול כרגיל. עד 200 בתים (כ-100 אותיות בעברית)."],
    ["cancel", "13. ביטול באמצע", "Talk ומיד Cancel: <code>LISTEN #N CANCELLED</code> והלוח חוזר ל-Ready."],
    ["selftest", "14. Self-test", "הכפתור ליד Board health: ה-LED מהבהב, צפצוף, וכל השורות עם ✓."],
    ["recordings", "15. ניגון הקלטות מה-SD", "לשונית ההקלטות: מנגנים את הפקודה האחרונה עם waveform, ואפשר למחוק."],
    ["settings", "16. עוצמה, השתקה ובהירות", "סליידר עוצמה ו-Test beep, Mute, בהירות LED."],
    ["vadtune", "17. כיוון Hands-free", "Sensitivity ו-Stop after silence, ובודקים דיבור ורעש."],
    ["bands", "18. ספי יום / ערב / לילה", "משנים ספים ולוחצים Apply thresholds."],
    ["persist", "19. ההגדרות נשמרות", "Reset ללוח, והעוצמה, הבהירות וההגדרות של Hands-free נשארות."],
    ["nopc", "20. בלי מחשב", "ב-On the board itself: BOOT button ו-Status sounds, ומדליקים Hands-free (כולם נשמרים בלוח). מנתקים את ה-USB מהמחשב, ורק אז מחברים את ה-power bank (אף פעם לא את שניהם יחד: הנחיית הבטיחות של הקורס). הלוח עולה מחדש עם ההגדרות השמורות, וצריך לחכות כ-10 שניות לברכה. שואלים בקול או ב-BOOT."],
    ["replay", "21. שחזור לוג, בלי הלוח", "Replay a log… עם לוג שנשמר, במהירות 4x. בסיום Stop, ו-Connect board שוב."],
    ["stats", "22. סטטיסטיקה", "חלונית Statistics: מספרים, גרף זמני תגובה, Export CSV."],
    ["summary", "22. סיכום במסך מלא", "כפתור Summary: הפקודות, אחוז ההצלחה, זמן התגובה וההגדרות של הלוח, בגדול. Save report שומר דף HTML."],
  ]],
  ["מקרי קצה (הרשימה המורחבת)", [
    ["noise", "13. רעש לא מפעיל", "Hands-free דלוק: דפיקה על השולחן או מחיאת כפיים, והמצב נשאר Waiting for speech."],
    ["cough", "13. שיעול נזרק", "בשיחה מופיעה הערה שהצליל נזרק (<code>too short to be speech</code>)."],
    ["manynoises", "13. הרבה רעשים ברצף", "אחרי 3 רעשים, הפסקה של 5 שניות."],
    ["silence", "14. שקט", "Hands-free ושותקים: לא קורה כלום. אחר כך Talk ושותקים: העוזר עונה במשפט כללי."],
    ["echo", "23. העוזר לא שומע את עצמו", "Hands-free דלוק, <code>Tell me about the moon</code>: אחרי התשובה חוזר ל-Waiting ולא מתחיל לבד."],
    ["farspeech", "24. דיבור ממרחק עם רעש", "אחד מתופף על השולחן והשני במרחק מטר אומר <code>Turn on the light</code>. ה-LED נדלק."],
    ["cantdo", "18. בקשה שאי אפשר לבצע", "<code>Make the light blue</code>: עונה שה-LED הוא בצבע אחד."],
    ["volrange", "19. ערך מחוץ לטווח", "<code>Set the volume to 150 percent</code>: נעצר על 100%. <code>Set the volume to 0</code>: 10%."],
    ["multi", "20. כמה בקשות בפקודה אחת", "<code>Set the light brightness and the volume to 100 percent</code>: שניהם עולים."],
    ["badlight", "12. ערך אור בקצוות", "סליידר האור לקצה התחתון ואז לעליון, <code>Is it day or night</code> בכל אחד. <code>LIGHT 99999</code> בטרמינל נדחה."],
    ["busy", "15. Talk באמצע תשובה", "הכפתור Busy והבקשה נדחית. מחכים שיסיים, או Cancel."],
    ["pause", "17. הפסקה קצרה באמצע משפט", "<code>What is the capital... of France</code> עם עצירה של חצי שנייה: נשלח כשאלה אחת."],
    ["reconnect", "9. ניתוק מרכז הבקרה", "מנתקים את כבל ה-USB מהמחשב: באנר אדום, והלוח נכבה כי הוא מקבל חשמל מהכבל הזה. מחברים שוב: הלוח עולה מחדש (כ-10 שניות) והדף מתחבר לבד."],
    ["timeroffline", "7. טיימר כשאין חיבור", "<code>Set a timer for 30 seconds</code> ומכבים hotspot: הלוח עדיין מצפצף בזמן."],
    ["wifi", "1. ה-Wi-Fi נופל", "ה-hotspot כבוי ל-30 שניות: Offline ושני צלילים יורדים. אחרי שחוזר: Ready תוך כ-15 שניות ושני צלילים עולים."],
    ["netswitch", "3. החלפת רשת", "בכרטיס Wi-Fi network: שם וסיסמה של hotspot שני ← Save & connect. בסוף Back to default."],
    ["sd", "8. בלי כרטיס SD", "מוציאים SD ועושים Reset. נורית SD אדומה, והעוזר עונה בלי הקלטות."],
  ]],
];
const DEMO_STORAGE_KEY = "esp32va-demo-checklist";
/** Tools whose use ticks a checklist item by itself (autoTickDemo). */
const DEMO_TOOL_ITEMS = { set_board_state: "led", set_volume: "voicevolume", get_time_of_day: "daynight", get_board_status: "status" };

/** Ticked items, kept in this browser (may be unavailable - then just for this page). */
let demoDone = new Set();
const demoAuto = new Set();  // ticked by autoTickDemo() on this page, not by hand
try { demoDone = new Set(JSON.parse(localStorage.getItem(DEMO_STORAGE_KEY) || "[]")); } catch (_) { /* no storage */ }

function saveDemoDone() {
  try { localStorage.setItem(DEMO_STORAGE_KEY, JSON.stringify([...demoDone])); } catch (_) { /* no storage */ }
}

/** Draws the checklist and its progress bar. */
function renderDemoChecklist() {
  const list = document.getElementById("demo-list");
  const all = DEMO_SECTIONS.flatMap(([, items]) => items);
  list.replaceChildren(...DEMO_SECTIONS.flatMap(([heading, items]) => {
    const title = document.createElement("h3");
    title.textContent = heading;
    return [title, ...items.map(([id, name, how]) => {
      const item = document.createElement("label");
      item.className = `demo-item${demoDone.has(id) ? " done" : ""}${demoAuto.has(id) ? " auto" : ""}`;
      const box = Object.assign(document.createElement("input"), { type: "checkbox", checked: demoDone.has(id) });
      box.addEventListener("change", () => {
        if (box.checked) demoDone.add(id); else demoDone.delete(id);
        demoAuto.delete(id);
        saveDemoDone();
        renderDemoChecklist();
      });
      const nameElement = Object.assign(document.createElement("span"), { className: "demo-title", textContent: name });
      const howElement = Object.assign(document.createElement("span"), { className: "demo-how", innerHTML: how });  // fixed text above, not board input
      item.append(box, nameElement, howElement);
      const row = document.createElement("li");
      row.append(item);
      return row;
    })];
  }));
  const done = all.filter(([id]) => demoDone.has(id)).length;
  document.getElementById("demo-bar").style.width = `${(done / all.length) * 100}%`;
  document.getElementById("demo-count").textContent = `${done} מתוך ${all.length} הודגמו`;
  document.getElementById("demo-open").textContent = `✓ Demo checklist ${done}/${all.length}`;
}

/** Ticks a checklist item by itself when the board just showed it working
    (a live board only - not a replayed log), with a short note at the bottom. */
let demoToastTimer = null;
function autoTickDemo(id) {
  if (!link.connected || demoDone.has(id)) return;
  const item = DEMO_SECTIONS.flatMap(([, items]) => items).find(([itemId]) => itemId === id);
  if (!item) return;
  demoDone.add(id);
  demoAuto.add(id);
  saveDemoDone();
  renderDemoChecklist();
  const toast = document.getElementById("demo-toast");
  toast.textContent = `✓ סומן בצ'ק-ליסט: ${item[1]}`;
  toast.classList.add("show");
  clearTimeout(demoToastTimer);
  demoToastTimer = setTimeout(() => toast.classList.remove("show"), 3500);
}

function setDemoOpen(open) {
  document.getElementById("demo-drawer").classList.toggle("open", open);
  document.getElementById("demo-open").setAttribute("aria-expanded", String(open));
}

document.getElementById("demo-open").addEventListener("click", () => {
  setDemoOpen(!document.getElementById("demo-drawer").classList.contains("open"));
});
document.getElementById("demo-close").addEventListener("click", () => setDemoOpen(false));
document.getElementById("demo-reset").addEventListener("click", () => {
  demoDone.clear();
  demoAuto.clear();
  saveDemoDone();
  renderDemoChecklist();
});
document.addEventListener("keydown", (event) => { if (event.key === "Escape") setDemoOpen(false); });

// ===== Self-test =====

/** Checks before a demo, each one -> [result ("ok" | "warn" | "fail"), detail].
    Only status commands the board already has (no new firmware needed). */
const SELF_TESTS = [
  ["Startup checks", async () => {
    const stages = ["sd", "mic", "speaker", "wifi", "time", "openai"].map((name) => state.board.stages[name]);
    const failed = stages.filter((stage) => stage && stage.state === "fail").length;
    const done = stages.filter((stage) => stage && stage.state === "ok").length;
    return failed ? ["fail", `${failed} failed - see the cards above`]
      : done === stages.length ? ["ok", "all green"] : ["warn", "still starting"];
  }],
  ["Wi-Fi signal", async () => {
    const rssi = state.board.wifiRssi;
    if (rssi === null) return ["warn", "no reading yet"];
    return rssi >= -67 ? ["ok", `${rssi} dBm - good`] : rssi >= -75 ? ["warn", `${rssi} dBm - weak, move the phone closer`]
      : ["fail", `${rssi} dBm - too weak, move the phone closer`];
  }],
  ["LED", async () => {
    const wasOn = Boolean(state.board.led && state.board.led.on);
    sendQuietly(wasOn ? "LED off" : "LED on");
    const first = await waitForStatus("LED", (words) => words[0] === (wasOn ? "off" : "on"));
    await new Promise((resolve) => setTimeout(resolve, 700));
    sendQuietly(wasOn ? "LED on" : "LED off");
    const back = await waitForStatus("LED", (words) => words[0] === (wasOn ? "on" : "off"));
    return first && back ? ["ok", "blinked once - did you see it?"] : ["fail", "the board didn't answer the LED command"];
  }],
  ["Speaker", async () => {
    sendQuietly("BEEP");
    const words = await waitForStatus("BEEP");
    if (!words) return ["fail", "no answer to BEEP"];
    if (words[0] !== "played") return ["warn", "busy - try again when Ready"];
    return state.board.volume && state.board.volume.muted ? ["warn", "beeped, but the speaker is muted"]
      : ["ok", `beeped at ${state.board.volume ? state.board.volume.percent : "?"}% - did you hear it?`];
  }],
  ["Light sensor", async () => {
    sendQuietly("LIGHT");
    const words = await waitForStatus("LIGHT");
    return words ? ["ok", `reads ${words[0]} - ${words[1]}`] : ["fail", "no answer to LIGHT"];
  }],
  ["SD card", async () => {
    try {
      const files = await link.list();
      showRecordings(files);
      return ["ok", `${files.length} recording${files.length === 1 ? "" : "s"} on the card`];
    } catch (error) {
      return [error.kind === "busy" ? "warn" : "fail", error.message];
    }
  }],
  ["Board clock", async () => {
    sendQuietly("CLOCK");
    const words = await waitForStatus("CLOCK");
    if (!words) return ["warn", "this firmware can't report it - ask \"What time is it?\" instead"];
    if (words[0] === "unset") return ["fail", "not set - the board never reached the time server"];
    const [hours, minutes] = words[0].split(":").map(Number);
    const now = new Date();
    let diff = (hours * 60 + minutes) - (now.getHours() * 60 + now.getMinutes());
    diff = ((diff + 720) % 1440 + 1440) % 1440 - 720;  // to -12 h .. +12 h
    if (Math.abs(diff) <= 2) return ["ok", `${words[0]}, same as this laptop`];
    const hoursOff = Math.round(Math.abs(diff) / 60);
    const off = hoursOff ? `${hoursOff} h` : `${Math.abs(diff)} min`;
    return ["warn", `${words[0]} - ${off} ${diff < 0 ? "behind" : "ahead of"} this laptop (the board keeps Israel time)`];
  }],
  ["OpenAI", async () => {
    const openai = state.board.stages.openai;
    return openai && openai.state === "ok" ? ["ok", "session ready"] : ["fail", openai ? openai.detail || openai.state : "no report yet"];
  }],
];
const SELF_TEST_ICONS = { run: "…", ok: "✓", warn: "!", fail: "✗" };

/** Runs SELF_TESTS one after another, showing each result as it comes. */
async function runSelfTest() {
  if (selfTestRunning || !link.connected) return;
  selfTestRunning = true;
  updateTalkButton();
  const box = document.getElementById("selftest");
  const list = document.getElementById("selftest-list");
  const title = document.getElementById("selftest-title");
  box.hidden = false;
  title.textContent = "Self-test - running…";
  const rows = SELF_TESTS.map(([name]) => {
    const row = document.createElement("li");
    row.dataset.result = "run";
    row.innerHTML = '<span class="st-icon"></span><b></b><span class="st-detail"></span>';
    row.querySelector(".st-icon").textContent = SELF_TEST_ICONS.run;
    row.querySelector("b").textContent = name;
    return row;
  });
  list.replaceChildren(...rows);
  const counts = { ok: 0, warn: 0, fail: 0 };
  for (const [index, [, check]] of SELF_TESTS.entries()) {
    let result = "fail";
    let detail = "";
    try {
      [result, detail] = await check();
    } catch (error) {
      detail = error.message;
    }
    if (!link.connected) { result = "fail"; detail = "the board disconnected"; }
    counts[result]++;
    rows[index].dataset.result = result;
    rows[index].querySelector(".st-icon").textContent = SELF_TEST_ICONS[result];
    rows[index].querySelector(".st-detail").textContent = detail;
    rows[index].title = detail;
  }
  title.textContent = counts.fail ? `Self-test - ${counts.fail} problem${counts.fail === 1 ? "" : "s"}`
    : counts.warn ? `Self-test - passed, ${counts.warn} to look at` : "Self-test - all passed";
  appendLog(`(app) ${title.textContent}.`);
  if (!counts.fail) autoTickDemo("selftest");
  selfTestRunning = false;
  updateTalkButton();
}

document.getElementById("selftest-run").addEventListener("click", runSelfTest);
document.getElementById("selftest-close").addEventListener("click", () => { document.getElementById("selftest").hidden = true; });

// ===== Demo clock =====

/** The 25 minutes of Documentation/demo-plan.md: [from minute, what to show]. */
const DEMO_PLAN = [
  [0, "הצגה קצרה"], [1, "הדלקה ונורות"], [2, "שאלה בלחיצה"], [3.5, "LED ועוצמה בקול"], [5, "תזכורת וטיימר"],
  [6.5, "חלק היום ומצב הלוח"], [8, "Hands-free ורעש"], [10, "זיכרון ועברית"], [11.5, "שאלה בכתב וביטול"],
  [13.5, "Self-test והקלטות SD"], [15, "הגדרות ושמירה"], [17, "בלי מחשב"], [19, "שחזור לוג"], [20, "רובסטיות: רשת וחומרה"],
  [23, "סטטיסטיקה וסיכום"], [24, "רזרבה"],
];
const DEMO_LENGTH_MIN = 25;
var demoClockStart = null;  // var: renderSummary() may read it before this line runs
let demoClockTicker = null;

function renderDemoClock() {
  const clock = document.getElementById("demo-clock");
  clock.hidden = demoClockStart === null;
  if (demoClockStart === null) return;
  const elapsedS = Math.floor((Date.now() - demoClockStart) / 1000);
  const minutes = elapsedS / 60;
  const step = [...DEMO_PLAN].reverse().find(([from]) => minutes >= from);
  const late = minutes >= DEMO_LENGTH_MIN;
  clock.classList.toggle("late", late);
  const where = document.createElement("bdi");
  where.textContent = late ? "הזמן נגמר" : step[1];
  clock.replaceChildren(`⏱ ${pad(Math.floor(elapsedS / 60))}:${pad(elapsedS % 60)} · `, where);
}

function setDemoClock(running) {
  clearInterval(demoClockTicker);
  demoClockStart = running ? Date.now() : null;
  if (running) demoClockTicker = setInterval(renderDemoClock, 1000);
  renderDemoClock();
}

document.getElementById("demo-clock-start").addEventListener("click", () => { setDemoClock(true); setDemoOpen(false); });
document.getElementById("demo-clock").addEventListener("click", () => setDemoClock(false));

// ===== Stage view =====

let stageTicker = null;

/** Copies what the main view shows into the stage view: the orb, the state,
    the last thing said each way, the LED, the part of the day and the timer. */
function syncStage() {
  const orb = document.getElementById("stage-orb");
  orb.dataset.state = elements.orb.dataset.state;
  orb.classList.toggle("stale", elements.orb.classList.contains("stale"));
  orb.style.setProperty("--level", elements.orb.style.getPropertyValue("--level") || 0);
  document.getElementById("stage-label").textContent = elements.stateLabel.textContent;
  document.getElementById("stage-detail").textContent = elements.stateDetail.textContent;

  const last = (kind) => [...elements.bubbles.querySelectorAll(`.bubble.${kind}`)].pop();
  const exchange = document.getElementById("stage-exchange");
  const shown = [last("heard"), last("said")].filter(Boolean)
    .sort((a, b) => (a.compareDocumentPosition(b) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1));
  const key = shown.map((bubble) => bubble.textContent).join("|");
  if (exchange.dataset.key !== key) {
    exchange.dataset.key = key;
    exchange.replaceChildren(...shown.map((bubble) => {
      const copy = bubble.cloneNode(true);
      copy.querySelectorAll("time").forEach((time) => time.remove());
      return copy;
    }));
  }

  const status = document.getElementById("stage-status");
  const parts = [];
  const led = state.board.led;
  if (led) {
    const bulb = document.getElementById("bulb").cloneNode(true);
    bulb.removeAttribute("id");
    const item = document.createElement("span");
    item.append(bulb, led.on ? "LED on" : "LED off");
    parts.push(item);
  }
  if (state.board.light) {
    const item = document.createElement("span");
    item.innerHTML = `<span class="sky">${SKY_ICONS[state.board.light.band] || ""}</span>`;
    item.append(state.board.light.band);
    parts.push(item);
  }
  if (state.board.volume) parts.push(Object.assign(document.createElement("span"),
    { textContent: state.board.volume.muted ? "🔇 muted" : `🔊 ${state.board.volume.percent}%` }));
  const chip = document.getElementById("timer-chip");
  if (!chip.hidden) parts.push(Object.assign(chip.cloneNode(true), { id: "" }));
  status.replaceChildren(...parts);
}

function setStageOpen(open) {
  const stage = document.getElementById("stage");
  stage.hidden = !open;
  clearInterval(stageTicker);
  if (open) {
    setDemoOpen(false);
    document.getElementById("summary").hidden = true;
    if (document.activeElement) document.activeElement.blur();  // so Space talks instead of re-pressing the button
    syncStage();
    stageTicker = setInterval(syncStage, 100);
  }
}

document.getElementById("stage-open").addEventListener("click", () => setStageOpen(true));
document.getElementById("stage-close").addEventListener("click", () => setStageOpen(false));
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape") setStageOpen(false);
  if ((event.key === "f" || event.key === "F") && !event.ctrlKey && !event.metaKey && !event.altKey &&
      !(event.target.closest && event.target.closest("input, textarea, select, [contenteditable]"))) {
    setStageOpen(document.getElementById("stage").hidden);
  }
});
renderDemoChecklist();

// ===== Session summary =====

/** Fills the summary screen from the Statistics counters and the board's
    last reported settings. */
function renderSummary() {
  const history = usage.history;
  const answered = history.filter((turn) => turn.result === "pass").length;
  const times = [...usage.responseMs].sort((a, b) => a - b);
  const median = times.length ? (times.length % 2 ? times[(times.length - 1) / 2]
    : (times[times.length / 2 - 1] + times[times.length / 2]) / 2) : null;
  const seconds = (ms) => (ms / 1000).toFixed(1);
  const span = history.length > 1 ? (history[history.length - 1].at - history[0].at) / 1000 : 0;

  document.getElementById("summary-sub").textContent = history.length
    ? `${history.length} commands` + (span >= 60 ? ` over ${Math.round(span / 60)} minutes` : "")
    : "No commands yet - ask the assistant something first.";
  if (demoClockStart) {  // the demo clock from the checklist drawer is running
    const demoS = Math.floor((Date.now() - demoClockStart) / 1000);
    document.getElementById("summary-sub").textContent += ` · demo time ${Math.floor(demoS / 60)}:${pad(demoS % 60)}`;
  }

  const tile = (value, label, sub) => {
    const item = document.createElement("div");
    item.className = "summary-tile";
    item.innerHTML = '<div class="value"></div><div class="label"></div><div class="sub"></div>';
    item.querySelector(".value").textContent = value;
    item.querySelector(".label").textContent = label;
    item.querySelector(".sub").textContent = sub;
    return item;
  };
  document.getElementById("summary-tiles").replaceChildren(
    tile(String(usage.turns), "Commands", `${usage.handsFree} hands-free · ${usage.turns - usage.handsFree} with Talk`),
    tile(history.length ? `${Math.round((answered / history.length) * 100)}%` : "-", "Answered",
         usage.failed ? `${usage.failed} failed` : "none failed"),
    tile(median === null ? "-" : `${seconds(median)} s`, "Median response",
         times.length ? `fastest ${seconds(times[0])} s · slowest ${seconds(times[times.length - 1])} s` : "end of speech to first sound"),
    tile(usage.bootMs === null ? "-" : `${seconds(usage.bootMs)} s`, "Startup", "power on to ready to talk"),
  );

  const toolCount = (name) => history.filter((turn) => turn.tool === name).length;
  const chip = (icon, label, count) => {
    const item = document.createElement("span");
    item.className = `summary-chip${count === 0 ? " zero" : ""}`;
    item.append(`${icon} ${label} `, Object.assign(document.createElement("b"), { textContent: `×${count}` }));
    return item;
  };
  document.getElementById("summary-tools").replaceChildren(
    chip("💬", "Just answered", history.filter((turn) => turn.tool === "-" && turn.result === "pass").length),
    chip("💡", "Switched the LED", toolCount("set_board_state")),
    chip("🌗", "Read the light sensor", toolCount("get_time_of_day")),
    chip("🔊", "Changed the volume", toolCount("set_volume")),
    chip("🕒", "Told the time", toolCount("get_current_time")),
    chip("⏰", "Set a timer", toolCount("set_timer")),
    chip("📶", "Checked the board", toolCount("get_board_status")),
    chip("🎙️", "Switched hands-free", toolCount("set_hands_free")),
    chip("🤫", "Ignored a noise", usage.noisesIgnored),
  );

  const board = state.board;
  const setting = (icon, text) => Object.assign(document.createElement("span"), { className: "summary-chip", textContent: `${icon} ${text}` });
  const settings = [];
  if (board.volume) settings.push(setting(board.volume.muted ? "🔇" : "🔊", board.volume.muted ? "Muted" : `Volume ${board.volume.percent}%`));
  if (board.led) settings.push(setting("💡", `LED ${board.led.on ? "on" : "off"}, brightness ${board.led.brightness}%`));
  if (board.vad) settings.push(setting("🎙️", `Hands-free ${board.vad.on ? "on" : "off"}, sensitivity ${board.vad.sensitivity} of ${limits.vad_sensitivity_max}`));
  if (board.light) settings.push(setting("🌗", `Evening from ${board.light.eveningMin}, day from ${board.light.dayMin} (now ${board.light.band})`));
  if (!settings.length) settings.push(setting("🔌", "Connect the board to see its settings."));
  document.getElementById("summary-settings").replaceChildren(...settings);
}

function setSummaryOpen(open) {
  document.getElementById("summary").hidden = !open;
  if (open) {
    setStageOpen(false);
    setDemoOpen(false);
    renderSummary();
    autoTickDemo("summary");
  }
}

/** Saves the session as one self-contained HTML page: the summary, the
    response-time chart, a table of every command and the conversation -
    for the project's results, or to show a run again later. */
function onSaveReportClick() {
  renderSummary();
  const css = [...document.querySelectorAll("style")].map((style) => style.textContent).join("\n");
  const summary = document.getElementById("summary").cloneNode(true);
  summary.removeAttribute("id");
  summary.hidden = false;
  summary.querySelectorAll("button").forEach((button) => button.remove());
  summary.querySelectorAll("[id]").forEach((element) => element.removeAttribute("id"));
  const chart = document.getElementById("response-chart").cloneNode(true);
  chart.removeAttribute("id");
  const conversation = elements.bubbles.cloneNode(true);
  conversation.removeAttribute("id");
  const escape = (text) => String(text).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  const seconds = (ms) => (ms === null ? "-" : (ms / 1000).toFixed(2));
  const rows = usage.history.map((turn) => `<tr><td>${escape(turnLabel(turn))}</td><td>${escape(clockStamp(turn.at).slice(0, 8))}</td>` +
    `<td>${turn.mode === "vad" ? "hands-free" : "Talk"}</td><td>${escape(turn.tool === "-" ? "" : turn.tool)}</td>` +
    `<td>${escape(turn.result)}</td><td>${seconds(turn.serverMs)}</td><td>${seconds(turn.firstSoundMs)}</td><td>${seconds(turn.doneMs)}</td></tr>`).join("");
  const board = state.board;
  const facts = [
    board.wifiRssi !== null ? `Wi-Fi ${board.wifiRssi} dBm` : "",
    usage.bootMs ? `startup ${(usage.bootMs / 1000).toFixed(1)} s` : "",
    link.connected ? elements.status.textContent : "",
  ].filter(Boolean).join(" · ");
  const page = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Voice assistant session ${escape(fileStamp(new Date()))}</title>
<style>${css}
body { padding: 0 0 40px; }
.report { max-width: 1100px; margin: 0 auto; padding: 0 16px; display: flex; flex-direction: column; gap: 22px; }
.report .summary { position: static; inset: auto; padding: 32px 0 0; overflow: visible; }
.report h2.part { font-size: 18px; margin: 10px 0 0; }
.report table { border-collapse: collapse; width: 100%; font-size: 13px; font-variant-numeric: tabular-nums; }
.report th, .report td { border-bottom: 1px solid var(--border); padding: 5px 8px; text-align: left; }
.report th { color: var(--muted); font-weight: 600; }
.report .bubbles { max-height: none; overflow: visible; display: flex; flex-direction: column; gap: 8px; }
.report .bubble { animation: none; }
.report .facts { color: var(--muted); text-align: center; margin: -12px 0 0; }
</style></head><body><div class="report">
${summary.outerHTML}
<p class="facts">${escape(clockStamp(new Date()).slice(0, 8))} · ${escape(new Date().toDateString())}${facts ? ` · ${escape(facts)}` : ""}</p>
<h2 class="part">Response time per command</h2>
${chart.outerHTML}
<h2 class="part">Every command</h2>
<table><thead><tr><th>#</th><th>time</th><th>how</th><th>tool</th><th>result</th><th>to OpenAI's first reply (s)</th><th>to first sound (s)</th><th>to end of reply (s)</th></tr></thead>
<tbody>${rows || '<tr><td colspan="8">No commands yet.</td></tr>'}</tbody></table>
<h2 class="part">Conversation</h2>
<div class="bubbles">${conversation.innerHTML}</div>
<p class="facts">Saved from the ESP32 voice assistant's control center (control_center/control_center.html).</p>
</div></body></html>`;
  const url = URL.createObjectURL(new Blob([page], { type: "text/html" }));
  const anchor = Object.assign(document.createElement("a"), { href: url, download: `esp32_session_report_${fileStamp(new Date())}.html` });
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

document.getElementById("summary-save").addEventListener("click", onSaveReportClick);
document.getElementById("summary-open").addEventListener("click", () => setSummaryOpen(true));
document.getElementById("summary-close").addEventListener("click", () => setSummaryOpen(false));
document.addEventListener("keydown", (event) => { if (event.key === "Escape") setSummaryOpen(false); });

// ===== Guided tour =====

/** The tour's stops: which part of the page (the first of the selectors
    that is showing - Hands-free appears only once the board reports it),
    and what to say about it. */
const TOUR_STEPS = [
  ["#connect", "1. Connect the board",
   "Plug the ESP32 into this computer with USB, press Connect board and pick its COM port. Chrome or Edge only."],
  [".health", "2. Startup checks",
   "Each part of the board reports here as it starts: Wi-Fi, SD card, microphone, speaker and the OpenAI connection. All green means it's ready - about 10 seconds after power-on, and it greets you out loud."],
  ["#talk", "3. Talk to it",
   "Press Talk (or the space bar) and speak for up to 3 seconds - best in English (Hebrew works too, less well). The circle shows what it's doing: listening, thinking, speaking."],
  ["#hands-free, .talk-row", "4. Or hands-free",
   "Once the board is connected, a Hands-free switch appears next to Talk. Switch it on and just start talking - the board notices speech by itself and ignores knocks and claps. Room too noisy? Type the question in the box below instead - it still answers out loud."],
  [".conversation", "5. The conversation",
   "What it heard and what it answered, with the response time, and a tag whenever it did something on the board (LED, volume, timer)."],
  ["#phrases", "6. Ideas to try",
   "Things it understands - press Talk and say one: turn the light on, set the volume, set a timer, ask the time or whether it's day or night, or switch hands-free on by voice."],
  ["#controls-heading", "7. Settings",
   "Volume, mute, LED brightness, and the light sensor's thresholds. The board saves them, so they stay after a reset."],
  ["#usage-heading", "8. Statistics",
   "Every command's response time, as numbers and a chart. Summary shows it all big at the end."],
  ["#stage-open", "9. For a room",
   "Stage view (key F) shows the assistant big, for people watching from further away. Demo checklist walks through everything to show."],
];

let tourIndex = -1;

/** Shows tour stop index (or ends the tour when out of range). */
function showTourStep(index) {
  const spot = document.getElementById("tour-spot");
  const card = document.getElementById("tour-card");
  if (index < 0 || index >= TOUR_STEPS.length) {
    tourIndex = -1;
    spot.hidden = card.hidden = true;
    return;
  }
  tourIndex = index;
  const [selector, title, text] = TOUR_STEPS[index];
  const target = [...document.querySelectorAll(selector)].find((element) => element.getClientRects().length);
  const panel = target.closest(".panel") && selector.endsWith("-heading") ? target.closest(".panel") : target;
  document.getElementById("tour-title").textContent = title;
  document.getElementById("tour-text").textContent = text;
  document.getElementById("tour-count").textContent = `${index + 1} of ${TOUR_STEPS.length}`;
  document.getElementById("tour-back").disabled = index === 0;
  document.getElementById("tour-next").textContent = index === TOUR_STEPS.length - 1 ? "Done" : "Next";
  spot.hidden = card.hidden = false;
  panel.scrollIntoView({ block: "center", behavior: "instant" });
  placeTour(panel);
}

/** Puts the spotlight around element and the card below it (or above, if there's no room). */
function placeTour(element) {
  const spot = document.getElementById("tour-spot");
  const card = document.getElementById("tour-card");
  const rect = element.getBoundingClientRect();
  const pad = 6;
  const top = Math.max(4, rect.top - pad), bottom = Math.min(window.innerHeight - 4, rect.bottom + pad);
  Object.assign(spot.style, { top: `${top}px`, left: `${rect.left - pad}px`, width: `${rect.width + 2 * pad}px`,
                              height: `${bottom - top}px` });
  const cardHeight = card.offsetHeight, cardWidth = card.offsetWidth;
  let cardTop = bottom + 12;
  if (cardTop + cardHeight > window.innerHeight - 8) cardTop = top - cardHeight - 12;
  if (cardTop < 8) cardTop = Math.min(window.innerHeight - cardHeight - 8, Math.max(8, top + 12));  // tall target: overlap it
  const cardLeft = Math.min(window.innerWidth - cardWidth - 16, Math.max(16, rect.left));
  Object.assign(card.style, { top: `${cardTop}px`, left: `${cardLeft}px` });
}

document.getElementById("tour-open").addEventListener("click", () => {
  setDemoOpen(false);
  showTourStep(0);
  document.getElementById("tour-next").focus();
});
document.getElementById("tour-next").addEventListener("click", () => showTourStep(tourIndex + 1));
document.getElementById("tour-back").addEventListener("click", () => showTourStep(tourIndex - 1));
document.addEventListener("keydown", (event) => {
  if (tourIndex < 0) return;
  if (event.key === "Escape") showTourStep(-1);
  else if (event.key === "ArrowRight") showTourStep(tourIndex + 1);
  else if (event.key === "ArrowLeft" && tourIndex > 0) showTourStep(tourIndex - 1);
});
window.addEventListener("resize", () => { if (tourIndex >= 0) showTourStep(tourIndex); });

// ===== Log replay =====

/** Pauses longer than this (at 1×) are shortened to it, so a replay doesn't sit idle. */
const REPLAY_MAX_GAP_MS = 3000;

const replay = { lines: [], index: 0, timer: null, startMs: 0 };

/** Turns a log the app or serial_logger.ps1 saved ("[hh:mm:ss.mmm] text" per line) into
    { atMs, text } items. Lines without a time take the previous line's time;
    the app's own notes and sent commands are left out. */
function parseReplayLog(text) {
  const items = [];
  let atMs = 0;
  // serial_logger.ps1's older logs end lines in \r\r\n and start with a BOM.
  for (const raw of text.replace(/^﻿/, "").split(/\r*\n/)) {
    const match = raw.match(/^\[(\d+):(\d\d):(\d\d)\.(\d{3})\]\s?(.*)$/);
    let line = raw;
    if (match) {
      let ms = ((+match[1] * 60 + +match[2]) * 60 + +match[3]) * 1000 + +match[4];
      while (items.length && ms < atMs - 12 * 3600 * 1000) ms += 24 * 3600 * 1000;  // the log went past midnight
      atMs = Math.max(atMs, ms);
      line = match[5];
    }
    if (!line.trim() || line.startsWith("(app)") || line.startsWith(">> ")) continue;
    items.push({ atMs, text: line });
  }
  return items;
}

/** Feeds one log line to the app the way the serial link would. */
function replayLine(text) {
  if (!text.startsWith("@@")) {
    appendLog(text);
    return;
  }
  const space = text.indexOf(" ");
  const tag = space < 0 ? text : text.slice(0, space);
  if (STATUS_TAGS.has(tag)) handleStatusLine(tag.slice(2), space < 0 ? "" : text.slice(space + 1));
  else appendLog(text, { protocol: true });
}

async function startReplay(file) {
  if (link.port) return;
  const items = parseReplayLog(await file.text());
  if (!items.some((item) => item.text.startsWith("@@"))) {
    appendLog(`(app) ${file.name} has no status lines (@@...) to replay - pick a log this app saved.`);
    return;
  }
  stopReplay();
  Object.assign(replay, { lines: items, index: 0, startMs: items[0].atMs });
  appendLog(`(app) Replaying ${file.name} (${items.length} lines).`);
  state.replayActive = true;
  state.liveUsage = usage;
  usage = newUsage();
  renderUsage();
  resetBoardStatus("unknown");
  addConversationNote(`▶ Replaying ${file.name}`, "replay");
  document.getElementById("replay-name").textContent = file.name;
  document.getElementById("replay-bar").hidden = false;
  replayStep();
}

/** Plays the next line now and schedules the one after, by the log's own timing. */
function replayStep() {
  const speed = Number(document.getElementById("replay-speed").value);
  do {
    replayLine(replay.lines[replay.index].text);
    replay.index += 1;
  } while (replay.index < replay.lines.length &&
           (speed === 0 || replay.lines[replay.index].atMs === replay.lines[replay.index - 1].atMs));

  const done = replay.index >= replay.lines.length;
  const previous = replay.lines[replay.index - 1];
  document.getElementById("replay-progress").value = replay.index / replay.lines.length;
  document.getElementById("replay-clock").textContent = formatSeconds((previous.atMs - replay.startMs) / 1000);
  if (done) {
    stopReplay();
    return;
  }
  const gap = Math.min(replay.lines[replay.index].atMs - previous.atMs, REPLAY_MAX_GAP_MS);
  replay.timer = setTimeout(replayStep, gap / speed);
}

/** Ends a replay (finished, stopped, or a board connected - reason says
    which) and brings back the live statistics. */
function stopReplay(reason = "") {
  if (document.getElementById("replay-bar").hidden) return;
  clearTimeout(replay.timer);
  replay.timer = null;
  document.getElementById("replay-bar").hidden = true;
  state.replayActive = false;
  if (state.liveUsage) {
    usage = state.liveUsage;
    state.liveUsage = null;
    renderUsage();
  }
  const finished = replay.index >= replay.lines.length;
  addConversationNote(finished ? "■ Replay finished" : `■ Replay stopped${reason ? ` - ${reason}` : ""}`, "replay");
  appendLog(`(app) Replay ${finished ? "finished" : "stopped"}.`);
}

document.getElementById("replay-log").addEventListener("click", () => document.getElementById("replay-file").click());
document.getElementById("replay-file").addEventListener("change", (event) => {
  const file = event.target.files[0];
  event.target.value = "";
  if (file) startReplay(file);
});
document.getElementById("replay-stop").addEventListener("click", () => stopReplay());
document.getElementById("replay-speed").addEventListener("change", () => {
  if (!replay.timer) return;
  clearTimeout(replay.timer);
  replayStep();
});
window.addEventListener("resize", renderResponseChart);
document.getElementById("usage-reset").addEventListener("click", () => {
  const band = usage.lightBand;
  usage = newUsage();
  usage.lightBand = band;
  renderUsage();
});
renderUsage();

// ===== Wiring =====

elements.connect.addEventListener("click", onConnectClick);
document.getElementById("clear-conversation").addEventListener("click", () => {
  elements.bubbles.replaceChildren();
  state.board.cycleFirstBubble = null;
  state.board.cycleFirstSaid = null;
  elements.result.className = "result";
  elements.result.textContent = "No conversation yet.";
});
elements.refresh.addEventListener("click", () => scheduleListRefresh(0));
elements.deleteAll.addEventListener("click", onDeleteAllClick);
elements.tabs.forEach((tab) => tab.addEventListener("click", () => selectTab(tab.dataset.kind)));
elements.applyGain.addEventListener("change", () => {
  const copy = state.selectedName && state.downloaded.get(state.selectedName);
  if (copy) showSelectedCopy(copy, { keepPosition: true });
});
elements.saveLog.addEventListener("click", onSaveLogClick);
elements.downloadLog.addEventListener("click", onDownloadLogClick);
elements.clearLog.addEventListener("click", onClearLogClick);
elements.deleteButton.addEventListener("click", onDeleteClick);
elements.talk.addEventListener("click", onTalkClick);
elements.cancelTalk.addEventListener("click", () => sendCommand("CANCEL"));
document.getElementById("ask-form").addEventListener("submit", onAskSubmit);
document.addEventListener("keydown", onKeyDown);
elements.toggleTerminal.addEventListener("click", onTerminalToggle);
elements.showProtocol.addEventListener("change", () => {
  elements.log.classList.toggle("hide-protocol", !elements.showProtocol.checked);
  if (elements.autoscroll.checked) elements.log.scrollTop = elements.log.scrollHeight;
});
[elements.volume, elements.ledBrightness, elements.lightValue, elements.vadSensitivity, elements.vadHangover].forEach((input) => {
  input.addEventListener("pointerdown", () => { input.dataset.dragging = "true"; });
  input.addEventListener("pointerup", () => { input.dataset.dragging = "false"; });
});
elements.vad.addEventListener("change", () => sendCommand(`VAD ${elements.vad.checked ? "on" : "off"}`));
elements.vadSensitivity.addEventListener("input", () => {
  elements.vadSensitivityValue.textContent = `${elements.vadSensitivity.value} of ${limits.vad_sensitivity_max}`;
  sendSoon("vadSensitivity", `VAD_SENSITIVITY ${elements.vadSensitivity.value}`);
});
elements.vadHangover.addEventListener("input", () => {
  elements.vadHangoverValue.textContent = formatSeconds(Number(elements.vadHangover.value) / 1000);
  sendSoon("vadHangover", `VAD_HANGOVER ${elements.vadHangover.value}`);
});
elements.volume.addEventListener("input", () => {
  elements.volumeValue.textContent = `${elements.volume.value}%`;
  sendSoon("volume", `VOLUME ${elements.volume.value}`);
});
elements.mute.addEventListener("change", () => sendCommand(`MUTE ${elements.mute.checked ? "on" : "off"}`));
elements.talkButton.addEventListener("change", () => sendCommand(`BUTTON ${elements.talkButton.checked ? "on" : "off"}`));
elements.statusSounds.addEventListener("change", () => sendCommand(`SOUNDS ${elements.statusSounds.checked ? "on" : "off"}`));
elements.wifiSave.addEventListener("click", saveWifiNetwork);
elements.wifiForget.addEventListener("click", () => {
  sendCommand("WIFI_FORGET");
  elements.wifiNote.textContent = "Back to the default network (from SECRETS.h) - connecting.";
});
elements.apiKeySave.addEventListener("click", saveApiKey);
elements.apiKeyInput.addEventListener("keydown", (event) => { if (event.key === "Enter") saveApiKey(); });
elements.apiKeyForget.addEventListener("click", () => {
  sendCommand("OPENAI_KEY_FORGET");
  elements.apiKeyNote.textContent = "Back to the default key (from SECRETS.h) - reconnecting.";
});
elements.beep.addEventListener("click", () => {
  elements.volumeNote.textContent = "Beeping…";
  sendCommand("BEEP");
});
elements.led.addEventListener("change", () => sendCommand(`LED ${elements.led.checked ? "on" : "off"}`));
elements.ledBrightness.addEventListener("input", () => {
  elements.ledBrightnessValue.textContent = `${elements.ledBrightness.value}%`;
  sendSoon("brightness", `LED_BRIGHTNESS ${elements.ledBrightness.value}`);
});
elements.lightMode.addEventListener("change", () => setLightMode(elements.lightMode.value));
elements.lightValue.addEventListener("input", () => {
  if (dayCycle.ticker) setLightMode("manual");  // touching the slider takes over from the day-cycle
  const light = state.board.light || { eveningMin: Number(elements.eveningMin.value), dayMin: Number(elements.dayMin.value) };
  drawLightBands(Number(elements.lightValue.value), light.eveningMin, light.dayMin);
  sendSoon("light", `LIGHT ${elements.lightValue.value}`);
});
elements.applyBands.addEventListener("click", onApplyBandsClick);
[elements.eveningMin, elements.dayMin].forEach((input) => input.addEventListener("keydown", (event) => {
  if (event.key === "Enter") onApplyBandsClick();
}));
elements.send.addEventListener("click", () => { sendCommand(elements.command.value); elements.command.value = ""; });
elements.command.addEventListener("keydown", (event) => {
  if (event.key === "Enter") { sendCommand(elements.command.value); elements.command.value = ""; }
});
elements.audio.addEventListener("play", onPlayerPlay);
elements.audio.addEventListener("seeked", drawWaveform);
elements.audio.addEventListener("ended", drawWaveform);
// The board's mic stays off while the player plays out loud (syncPlaybackHold()).
for (const type of ["playing", "pause", "ended", "emptied", "abort", "error", "seeked", "ratechange", "durationchange"]) {
  elements.audio.addEventListener(type, syncPlaybackHold);
}
elements.waveform.addEventListener("click", (event) => {
  const audio = elements.audio;
  if (!audio.duration) return;
  const bounds = elements.waveform.getBoundingClientRect();
  audio.currentTime = ((event.clientX - bounds.left) / bounds.width) * audio.duration;
});
window.addEventListener("resize", drawWaveform);
setInterval(() => flushLogFile().catch((error) => {
  appendLog(`(app) Writing the log file failed (${error.message}) - stopped saving it.`);
  state.logFile = null;
  elements.logTarget.textContent = "";
  elements.saveLog.textContent = "Save log to folder…";
  elements.saveLog.disabled = false;
}), LOG_FILE_FLUSH_MS);

if ("showDirectoryPicker" in window) elements.saveLog.hidden = false;
if ("serial" in navigator) {
  // Replugged board: reopen it, unless Disconnect was clicked.
  // Windows often isn't ready to open the COM port the moment it appears, so try a few times.
  navigator.serial.addEventListener("connect", async (event) => {
    for (let attempt = 0; attempt < 4 && !link.connected && state.reconnectWanted; attempt++) {
      if (attempt) await new Promise((resolve) => setTimeout(resolve, 700));
      if (!link.connected && state.reconnectWanted) await connectPort(event.target);
    }
  });
  // A port allowed earlier on this page's origin can reopen without the chooser.
  navigator.serial.getPorts().then((ports) => { if (ports.length === 1 && !link.connected) connectPort(ports[0]); });
} else {
  elements.unsupported.hidden = false;
  elements.connect.disabled = true;
}
drawWaveform();
resetBoardStatus("offline");

// For poking at from the browser console while debugging.
window.replyAudioApp = { link, connectPort, crc32, decodeUlaw, analyze, makeWav };
