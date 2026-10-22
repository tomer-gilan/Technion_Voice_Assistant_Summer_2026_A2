#include "reply_recorder_utils.h"

#include <Arduino.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "i2s_audio_utils.h"
#include "log_utils.h"
#include "sd_card_utils.h"

namespace esp32va_reply_recorder {

namespace {

using esp32va_recordings::kReplyFolder;

constexpr uint32_t kWriteBlockBytes = 512;      // one SD sector
constexpr uint32_t kMaxBlocksPerService = 4;    // caps one loop() pass at ~2KB of SD writing
constexpr uint32_t kMuLawBytesPerSecond = 8000;  // 8kHz, 1 byte per sample
constexpr size_t kMaxPathLen = 64;
// If a reply's audio stops and its response.done never comes (e.g. the
// turn timed out), close the file anyway rather than holding off LISTEN
// forever. Far longer than any backpressure pause between deltas.
constexpr unsigned long kStalledReplyTimeoutMs = 20000;

enum class RecorderState {
  kIdle,
  kRecording,  // reply audio still arriving
  kFinishing,  // response.done arrived - write what's left, then close
  kAborting,   // cut short (disconnect or write failure) - close now
};

RecorderState state = RecorderState::kIdle;
int file_descriptor = -1;  // POSIX file descriptor of the open recording, -1 if none
bool write_failed = false;
bool missed_reply_logged = false;
unsigned long last_audio_arrival_ms = 0;
uint32_t bytes_written = 0;
uint32_t next_reply_number = 1;
char current_file_name[sizeof(FinishedRecording::file_name)];
char path_buffer[kMaxPathLen];
uint8_t write_block[kWriteBlockBytes];
esp32va_recordings::RecordingDeletedListener deleted_listener = nullptr;  // from setupReplyRecorder(), for the trims after each save

/** Full POSIX path of a reply file, built in path_buffer. */
const char* repliesPath(const char* file_name) {
  return esp32va_recordings::recordingPath(kReplyFolder, file_name, path_buffer, sizeof(path_buffer));
}

/** Creates the file for the recording that just started. On failure, logs
    it and switches to kAborting so the tap stops holding ring space. */
void openRecordingFile() {
  file_descriptor = open(repliesPath(current_file_name), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (file_descriptor < 0) {
    LOG_ERROR("Couldn't create %s (errno %d) - this reply won't be saved.\n", current_file_name, errno);
    write_failed = true;
    ringStopRecordingTap();
    state = RecorderState::kAborting;
  }
}

/** Moves up to kMaxBlocksPerService blocks from the tap to the file - only
    whole blocks while the reply is still arriving, the final partial block
    too once it's finishing. On a write failure, logs it and switches to
    kAborting. */
void writePendingBlocks() {
  for (uint32_t block = 0; block < kMaxBlocksPerService; block++) {
    uint32_t pending = ringRecordingTapPendingBytes();
    bool is_final_flush = state == RecorderState::kFinishing;
    if (pending == 0 || (pending < kWriteBlockBytes && !is_final_flush)) {
      return;
    }
    uint32_t block_bytes = ringReadRecordingTap(write_block, kWriteBlockBytes);
    ssize_t result = write(file_descriptor, write_block, block_bytes);
    if (result != static_cast<ssize_t>(block_bytes)) {
      LOG_ERROR("Writing %s failed (errno %d) after %u bytes - stopping this recording.\n", current_file_name,
                errno, (unsigned)bytes_written);
      write_failed = true;
      ringStopRecordingTap();
      state = RecorderState::kAborting;
      return;
    }
    bytes_written += block_bytes;
  }
}

/** Closes the file, logs the outcome, fills *finished, and returns to idle. */
void closeRecording(FinishedRecording* finished) {
  bool was_aborted = state == RecorderState::kAborting;
  ringStopRecordingTap();
  if (file_descriptor >= 0 && close(file_descriptor) != 0) {
    LOG_ERROR("Closing %s failed (errno %d) - the file may be incomplete.\n", current_file_name, errno);
    write_failed = true;
  }
  file_descriptor = -1;
  if (bytes_written == 0) {
    unlink(repliesPath(current_file_name));  // no point keeping an empty file; fine if it was never created
  }

  if (write_failed) {
    finished->outcome = RecordingOutcome::kFailed;
  } else if (was_aborted) {
    finished->outcome = RecordingOutcome::kSavedPartial;
  } else {
    finished->outcome = RecordingOutcome::kSaved;
  }
  strncpy(finished->file_name, current_file_name, sizeof(finished->file_name));
  finished->bytes_written = bytes_written;

  float seconds = static_cast<float>(bytes_written) / kMuLawBytesPerSecond;
  switch (finished->outcome) {
    case RecordingOutcome::kSaved:
      LOG_INFO("Saved %s (%u bytes = %.1f s).\n", current_file_name, (unsigned)bytes_written, seconds);
      break;
    case RecordingOutcome::kSavedPartial:
      if (bytes_written == 0) {
        LOG_INFO("Recording %s abandoned - the reply was interrupted before anything was saved.\n",
                 current_file_name);
      } else {
        LOG_INFO("Saved %s, cut short (%u bytes = %.1f s) - the reply was interrupted.\n", current_file_name,
                 (unsigned)bytes_written, seconds);
      }
      break;
    case RecordingOutcome::kFailed:
      LOG_ERROR("Recording %s failed (%u bytes written).\n", current_file_name, (unsigned)bytes_written);
      break;
    default:
      LOG_ERROR("Unexpected recording outcome %d for %s.\n", (int)finished->outcome, current_file_name);
      break;
  }
  state = RecorderState::kIdle;
}

}  // namespace

bool setupReplyRecorder(esp32va_recordings::RecordingDeletedListener on_recording_deleted) {
  deleted_listener = on_recording_deleted;
  uint32_t kept_count = 0;
  if (!esp32va_recordings::prepareRecordingFolder(kReplyFolder, deleted_listener, &next_reply_number,
                                                  &kept_count)) {
    return false;
  }
  esp32va_recordings::formatRecordingName(kReplyFolder, next_reply_number, current_file_name,
                                          sizeof(current_file_name));
  LOG_INFO("Reply recorder ready - %u earlier recording(s) on the card (keeps the newest %u), next is %s.\n",
           (unsigned)kept_count, (unsigned)esp32va_recordings::kMaxKeptRecordings, current_file_name);
  return true;
}

void noteReplyAudioArriving() {
  last_audio_arrival_ms = millis();
  if (state == RecorderState::kRecording) {
    return;  // the usual case - every delta after a reply's first
  }
  if (state != RecorderState::kIdle) {
    // The previous recording is still being written; LISTEN is held off
    // until it's done, so this shouldn't happen - but say so once if it does.
    if (!missed_reply_logged) {
      missed_reply_logged = true;
      LOG_ERROR("Reply audio arrived while %s was still being saved - this reply won't be recorded.\n",
                current_file_name);
    }
    return;
  }
  missed_reply_logged = false;
  esp32va_recordings::formatRecordingName(kReplyFolder, next_reply_number, current_file_name,
                                          sizeof(current_file_name));
  next_reply_number++;
  bytes_written = 0;
  write_failed = false;
  ringStartRecordingTap();
  state = RecorderState::kRecording;
  LOG_DEBUG("Recording %s started.\n", current_file_name);
}

void noteReplyFinished() {
  if (state == RecorderState::kRecording) {
    state = RecorderState::kFinishing;
  }
}

void noteReplyAborted() {
  if (state == RecorderState::kRecording || state == RecorderState::kFinishing) {
    ringStopRecordingTap();
    state = RecorderState::kAborting;
  }
}

bool serviceReplyRecorder(FinishedRecording* finished) {
  if (state == RecorderState::kIdle) {
    return false;
  }
  // Not when aborting - a reply cut off before its file existed shouldn't
  // leave an empty file behind.
  if (file_descriptor < 0 && !write_failed && state != RecorderState::kAborting) {
    openRecordingFile();
  }
  if (state == RecorderState::kRecording || state == RecorderState::kFinishing) {
    writePendingBlocks();
  }
  if (state == RecorderState::kRecording && ringRecordingTapPendingBytes() == 0 &&
      millis() - last_audio_arrival_ms > kStalledReplyTimeoutMs) {
    LOG_INFO("No more reply audio for %lu s and no response.done - closing %s as it is.\n",
             kStalledReplyTimeoutMs / 1000, current_file_name);
    state = RecorderState::kFinishing;
  }
  bool fully_written = state == RecorderState::kFinishing && ringRecordingTapPendingBytes() == 0;
  if (fully_written || state == RecorderState::kAborting) {
    closeRecording(finished);
    if (finished->bytes_written > 0) {  // a new file was kept - make room by dropping the oldest
      esp32va_recordings::deleteOldestRecordings(kReplyFolder, esp32va_recordings::kMaxKeptRecordings,
                                                 deleted_listener);
    }
    return true;
  }
  return false;
}

bool isRecorderIdle() {
  return state == RecorderState::kIdle;
}

}  // namespace esp32va_reply_recorder
