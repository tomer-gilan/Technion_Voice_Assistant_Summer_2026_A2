#pragma once

#include <stdint.h>

// Timing and resource measurements for the presentation's numbers (step 29):
// how long each startup stage takes, and for every LISTEN where the time
// goes between the end of recording and the end of playback, plus mic
// levels, upload/download sizes and heap headroom. Printed as two raw "@@"
// lines, never gated behind the log level, so every run's log (from
// tools/serial_logger.ps1 or the laptop app's Save log) can be turned into
// a table by tools/measurements_report.py:
//
//   @@BOOT_TIMING hw_ms=<n> sd_ms=<n> wifi_ms=<n> time_ms=<n> connect_ms=<n>
//                 session_ms=<n> total_ms=<n> heap_free=<n> heap_lowest=<n>
//                 heap_largest=<n> rssi=<dBm>
//       once, on the first session.updated. Each *_ms is that stage's own
//       duration; total_ms is boot to session ready.
//
//   @@TIMING n=<LISTEN number> result=<pass|fail> <key>=<value> ...
//       once per LISTEN, right after its verdict. Every *_after_rec_ms is
//       measured from the moment recording ended (commit + response.create
//       sent); -1 means the event never happened in that cycle. Key list in
//       reportTurn()'s doc comment.
//
// Nothing here changes what the board does - it only reads millis() and
// counts what the sketch already handles.

namespace esp32va_measure {

/** The startup stages timed for @@BOOT_TIMING, in the order setup() runs them. */
enum class BootMilestone : uint8_t {
  kHardwareReady,  // ring buffer, LED, I2S, mic check done
  kSdReady,        // SD mount, self-test, recorders done
  kWifiConnected,
  kTimeSynced,
  kSocketConnected,  // TLS + WebSocket handshake done (WStype_CONNECTED)
  kSessionReady,     // first session.updated
  kCount,
};

/** One-off turn events, each stamped the first time it happens in a LISTEN. */
enum class TurnEvent : uint8_t {
  kRecordingDone,     // commit + response.create sent - the reference point
  kFirstServerEvent,  // any server message after that
  kHeardTranscript,   // input transcription completed
  kFirstTranscript,   // first output transcript delta
  kFirstAudio,        // first output audio delta
  kToolCall,          // function call arguments done
  kToolResponseDone,  // the function-call-only response.done
  kResponseDone,      // the final response.done
  kPlaybackDone,      // ring buffer drained - the reply was heard in full
  kCount,
};

/** Stamps a startup milestone with millis(). Later calls for the same one
    are ignored, so a reconnect never overwrites the first boot's numbers. */
void markBootMilestone(BootMilestone milestone);

/** Prints @@BOOT_TIMING once (later calls are no-ops). Call on the first
    session.updated, after kSessionReady is marked. */
void reportBootTiming();

/** Starts a new LISTEN's measurements, clearing the previous one's. */
void beginTurn(uint32_t listen_number);

/** Stamps a turn event with millis(), the first time only. */
void markTurnEvent(TurnEvent event);

/** Records how the recording went: total JSON bytes sent and the time spent
    inside sendTXT() for them, the gained mic level (peak and RMS as raw
    sample values, the count of full-scale samples), and the slowest /
    average time to handle one 100 ms chunk. */
void setCaptureStats(uint32_t sent_bytes, uint32_t send_ms, int32_t peak_abs_sample, double rms_sample,
                     uint32_t clipped_samples, uint32_t chunk_max_ms, uint32_t chunk_avg_ms);

/** Adds one output audio delta's base64 length to this turn's total. */
void addReplyAudio(uint32_t base64_chars);

/** Counts one backpressure pause (WebSocket reads stopped for a full ring). */
void noteBackpressurePause();

/** Records the input transcript's length in characters (0 = it heard nothing). */
void setHeardLength(uint32_t characters);

/** Records which tool the model called this turn ("-" when none). */
void setToolName(const char* tool_name);

/** Prints this LISTEN's @@TIMING line, once. Keys, in order:
    n, result, first_event_after_rec_ms, heard_after_rec_ms,
    first_text_after_rec_ms, first_audio_after_rec_ms, tool, tool_after_rec_ms,
    tool_roundtrip_ms (tool call -> its response.done), response_done_after_rec_ms,
    playback_done_after_rec_ms, rec_ms (recording length), sent_bytes,
    upload_kbps (sent_bytes over time spent sending), reply_audio_bytes (mu-law
    bytes = 1/8000 s each), reply_audio_ms, pauses, heard_chars, mic_peak_dbfs,
    mic_rms_dbfs, clipped, chunk_max_ms, chunk_avg_ms, heap_free, heap_lowest,
    heap_largest, rssi. */
void reportTurn(bool passed);

}  // namespace esp32va_measure
