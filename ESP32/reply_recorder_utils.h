#pragma once

#include <stdint.h>
#include "recording_folder_utils.h"

// Saves each spoken reply to the SD card as it arrives: one file per reply
// in esp32va_recordings::kReplyFolder (/replies/reply_NNNN.ulaw), holding the
// raw 8kHz G.711 mu-law bytes exactly as OpenAI sent them (no header - the
// laptop app adds one), keeping only the newest kMaxKeptRecordings. The
// bytes come from the playback ring buffer's recording tap
// (i2s_audio_utils.h), so recording needs no audio buffer of its own.
//
// Split by context: the note*() calls only flip state and are safe from
// inside the WebSocket callback; every SD card operation happens in
// serviceReplyRecorder(), called from loop() (CLAUDE.md: no blocking work
// inside the callback). Files are written with POSIX open()/write() rather
// than the SD library's File class, which would add a 4KB stdio buffer per
// open file on top of the FAT driver's own per-file cache.

namespace esp32va_reply_recorder {

enum class RecordingOutcome {
  kSaved,         // the whole reply was written and the file closed cleanly
  kSavedPartial,  // the reply was cut short (disconnect) - what arrived is kept
  kFailed,        // the file couldn't be created/written/closed - see the ERROR line
};

struct FinishedRecording {
  RecordingOutcome outcome;
  char file_name[esp32va_recordings::kMaxRecordingNameSize];  // e.g. "reply_0007.ulaw"
  uint32_t bytes_written;
};

/** Prepares the replies folder (creates it, picks the next number, trims it
    to kMaxKeptRecordings), telling on_recording_deleted about each file
    deleted - now and in the trim after every later save. Call once from
    setup(), after the SD card is mounted. Returns false (logged) if the
    folder can't be created or read. */
bool setupReplyRecorder(esp32va_recordings::RecordingDeletedListener on_recording_deleted);

/** Call for every reply audio delta, BEFORE it's written to the ring. Starts
    a new recording (and the ring's recording tap) if none is in progress;
    otherwise does nothing. Callback-safe. */
void noteReplyAudioArriving();

/** Call when a response.done arrives: the recording in progress, if any, is
    closed once its remaining bytes are written. Callback-safe. */
void noteReplyFinished();

/** Call on disconnect, BEFORE resetRingBuffer(): stops recording at once
    and keeps what was saved so far as a partial file. Callback-safe. */
void noteReplyAborted();

/** Does the file work, from loop(): creates the file for a newly started
    reply, writes what the tap has collected in 512-byte blocks (at most a
    few per call, so one loop() pass never stalls long), and closes the file
    once the reply is finished or aborted - then, if a file was kept, deletes
    the oldest recordings beyond kMaxKeptRecordings. Returns true exactly
    once per recording - when it's closed - and fills *finished; logs the
    outcome itself. */
bool serviceReplyRecorder(FinishedRecording* finished);

/** True when no recording is in progress or still being written. */
bool isRecorderIdle();

}  // namespace esp32va_reply_recorder
