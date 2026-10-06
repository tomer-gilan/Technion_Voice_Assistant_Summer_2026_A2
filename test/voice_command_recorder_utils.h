#pragma once

#include <stdint.h>
#include "recording_folder_utils.h"

// Saves each LISTEN's mic audio to the SD card (step 27), to hear what the
// mic actually picked up: one file per LISTEN in
// esp32va_recordings::kVoiceCommandFolder (/voice_commands/command_NNNN.pcm),
// raw 16-bit little-endian PCM at 24kHz, mono, no header (the laptop app
// adds one). These are the mic's own samples, BEFORE the sketch's software
// gain - the app can apply the gain to hear what OpenAI received. Keeps the
// newest kMaxKeptRecordings, like the replies.
//
// Written chunk by chunk from the LISTEN capture loop (loop() context) with
// POSIX calls, like the reply recorder. No audio buffer of its own: each
// chunk is packed into the caller's raw I2S buffer in place.

namespace esp32va_command_recorder {

struct FinishedVoiceCommand {
  char file_name[esp32va_recordings::kMaxRecordingNameSize];  // e.g. "command_0003.pcm"
  uint32_t bytes_written;
  bool write_failed;  // the file is incomplete - see the ERROR line
};

/** Prepares the voice commands folder (creates it, picks the next number,
    trims it to kMaxKeptRecordings), telling on_recording_deleted about each
    file deleted - now and in the trim after every later save. Call once
    from setup(), after the SD card is mounted. Returns false (logged) if the
    folder can't be created or read. */
bool setupVoiceCommandRecorder(esp32va_recordings::RecordingDeletedListener on_recording_deleted);

/** Creates the file for a new LISTEN. If that fails (logged), the LISTEN
    still goes ahead, just unrecorded. */
void beginVoiceCommand();

/** Appends one chunk of mic audio. raw_i2s_words is the raw 32-bit I2S
    buffer readMicChunkGained() filled; it's converted to the mic's 16-bit
    samples (the same conversion, minus the gain) in place, so its contents
    are destroyed. Does nothing if no file is open. */
void appendMicChunk(int32_t* raw_i2s_words, uint32_t sample_count);

/** Closes this LISTEN's file, logs it, and trims the folder to
    kMaxKeptRecordings. Returns false if nothing was recorded (no file);
    otherwise fills *finished and returns true. */
bool finishVoiceCommand(FinishedVoiceCommand* finished);

}  // namespace esp32va_command_recorder
