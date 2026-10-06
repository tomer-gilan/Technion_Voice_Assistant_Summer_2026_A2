#include "voice_command_recorder_utils.h"

#include <Arduino.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "log_utils.h"

namespace esp32va_command_recorder {

namespace {

using esp32va_recordings::kVoiceCommandFolder;

constexpr uint32_t kPcmBytesPerSecond = 24000 * sizeof(int16_t);  // 24kHz, 16-bit mono
constexpr size_t kMaxPathLen = 64;

int file_descriptor = -1;  // POSIX file descriptor of this LISTEN's file, -1 if none
bool write_failed = false;
uint32_t bytes_written = 0;
uint32_t next_command_number = 1;
char current_file_name[esp32va_recordings::kMaxRecordingNameSize];
char path_buffer[kMaxPathLen];
esp32va_recordings::RecordingDeletedListener deleted_listener = nullptr;  // for the trim after each save

/** Full POSIX path of a voice command file, built in path_buffer. */
const char* commandPath(const char* file_name) {
  return esp32va_recordings::recordingPath(kVoiceCommandFolder, file_name, path_buffer, sizeof(path_buffer));
}

}  // namespace

bool setupVoiceCommandRecorder(esp32va_recordings::RecordingDeletedListener on_recording_deleted) {
  deleted_listener = on_recording_deleted;
  uint32_t kept_count = 0;
  if (!esp32va_recordings::prepareRecordingFolder(kVoiceCommandFolder, deleted_listener, &next_command_number,
                                                  &kept_count)) {
    return false;
  }
  esp32va_recordings::formatRecordingName(kVoiceCommandFolder, next_command_number, current_file_name,
                                          sizeof(current_file_name));
  LOG_INFO("Voice command recorder ready - %u earlier recording(s) (keeps the newest %u), next is %s.\n",
           (unsigned)kept_count, (unsigned)esp32va_recordings::kMaxKeptRecordings, current_file_name);
  return true;
}

void beginVoiceCommand() {
  esp32va_recordings::formatRecordingName(kVoiceCommandFolder, next_command_number, current_file_name,
                                          sizeof(current_file_name));
  next_command_number++;
  bytes_written = 0;
  write_failed = false;
  file_descriptor = open(commandPath(current_file_name), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (file_descriptor < 0) {
    LOG_ERROR("Couldn't create %s (errno %d) - this LISTEN's mic audio won't be saved.\n", current_file_name,
              errno);
    return;
  }
  LOG_DEBUG("Recording the mic to %s.\n", current_file_name);
}

void appendMicChunk(int32_t* raw_i2s_words, uint32_t sample_count) {
  if (file_descriptor < 0) {
    return;
  }
  // Pack in place: sample i lands in bytes [2i, 2i+2), which only overlap
  // words already read (word i is read before it's written over).
  uint8_t* packed_bytes = reinterpret_cast<uint8_t*>(raw_i2s_words);
  for (uint32_t i = 0; i < sample_count; i++) {
    int16_t mic_sample = static_cast<int16_t>(raw_i2s_words[i] >> 16);  // as readMicChunkGained(), without the gain
    memcpy(packed_bytes + i * sizeof(int16_t), &mic_sample, sizeof(mic_sample));
  }
  size_t chunk_bytes = sample_count * sizeof(int16_t);
  ssize_t result = write(file_descriptor, packed_bytes, chunk_bytes);
  if (result != static_cast<ssize_t>(chunk_bytes)) {
    LOG_ERROR("Writing %s failed (errno %d) after %u bytes - the rest of this LISTEN isn't saved.\n",
              current_file_name, errno, (unsigned)bytes_written);
    write_failed = true;
    close(file_descriptor);
    file_descriptor = -1;
    return;
  }
  bytes_written += chunk_bytes;
}

bool finishVoiceCommand(FinishedVoiceCommand* finished) {
  if (file_descriptor >= 0 && close(file_descriptor) != 0) {
    LOG_ERROR("Closing %s failed (errno %d) - the file may be incomplete.\n", current_file_name, errno);
    write_failed = true;
  }
  bool had_file = file_descriptor >= 0 || bytes_written > 0;
  file_descriptor = -1;
  if (!had_file) {
    return false;
  }
  if (bytes_written == 0) {
    unlink(commandPath(current_file_name));  // nothing in it - don't leave an empty file
    return false;
  }

  strncpy(finished->file_name, current_file_name, sizeof(finished->file_name));
  finished->bytes_written = bytes_written;
  finished->write_failed = write_failed;
  float seconds = static_cast<float>(bytes_written) / kPcmBytesPerSecond;
  if (write_failed) {
    LOG_ERROR("Saved only part of %s (%u bytes = %.1f s of mic audio).\n", current_file_name,
              (unsigned)bytes_written, seconds);
  } else {
    LOG_INFO("Saved %s (%u bytes = %.1f s of mic audio).\n", current_file_name, (unsigned)bytes_written, seconds);
  }
  esp32va_recordings::deleteOldestRecordings(kVoiceCommandFolder, esp32va_recordings::kMaxKeptRecordings,
                                             deleted_listener);
  return true;
}

}  // namespace esp32va_command_recorder
