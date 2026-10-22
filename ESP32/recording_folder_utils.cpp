#include "recording_folder_utils.h"

#include <Arduino.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log_utils.h"
#include "sd_card_utils.h"

namespace esp32va_recordings {

const RecordingFolder kReplyFolder = {"/replies", "reply_", ".ulaw", "reply"};
const RecordingFolder kVoiceCommandFolder = {"/voice_commands", "command_", ".pcm", "voice command"};

namespace {

constexpr size_t kMaxPathLen = 64;

char path_buffer[kMaxPathLen];
char oldest_file_name[kMaxRecordingNameSize];

}  // namespace

const char* recordingPath(const RecordingFolder& folder, const char* file_name, char* out, size_t out_size) {
  if (file_name == nullptr) {
    snprintf(out, out_size, "%s%s", esp32va_sd_card::kSdMountPoint, folder.directory);
  } else {
    snprintf(out, out_size, "%s%s/%s", esp32va_sd_card::kSdMountPoint, folder.directory, file_name);
  }
  return out;
}

bool parseRecordingNumber(const RecordingFolder& folder, const char* name, uint32_t* number) {
  size_t prefix_length = strlen(folder.file_prefix);
  if (strncasecmp(name, folder.file_prefix, prefix_length) != 0) {
    return false;
  }
  const char* digits = name + prefix_length;
  if (*digits < '0' || *digits > '9') {
    return false;
  }
  char* parse_end = nullptr;
  unsigned long parsed = strtoul(digits, &parse_end, 10);
  if (strcasecmp(parse_end, folder.file_extension) != 0) {
    return false;
  }
  *number = static_cast<uint32_t>(parsed);
  return true;
}

void formatRecordingName(const RecordingFolder& folder, uint32_t number, char* out, size_t out_size) {
  snprintf(out, out_size, "%s%04u%s", folder.file_prefix, (unsigned)number, folder.file_extension);
}

const RecordingFolder* folderForRecordingName(const char* name) {
  uint32_t unused_number = 0;
  if (parseRecordingNumber(kReplyFolder, name, &unused_number)) return &kReplyFolder;
  if (parseRecordingNumber(kVoiceCommandFolder, name, &unused_number)) return &kVoiceCommandFolder;
  return nullptr;
}

CleanupResult deleteOldestRecordings(const RecordingFolder& folder, uint32_t keep_count,
                                     RecordingDeletedListener on_deleted) {
  CleanupResult result = {0, 0};
  // One folder scan per deletion: simple, and never deletes while a
  // directory listing is open. Only ever a handful of files.
  for (;;) {
    DIR* directory = opendir(recordingPath(folder, nullptr, path_buffer, sizeof(path_buffer)));
    if (directory == nullptr) {
      LOG_ERROR("Couldn't open %s to clean up old recordings (errno %d).\n", folder.directory, errno);
      return result;
    }
    uint32_t recording_count = 0;
    uint32_t oldest_number = UINT32_MAX;
    while (struct dirent* entry = readdir(directory)) {
      uint32_t number = 0;
      if (!parseRecordingNumber(folder, entry->d_name, &number)) {
        continue;
      }
      recording_count++;
      if (number < oldest_number && strlen(entry->d_name) < sizeof(oldest_file_name)) {
        oldest_number = number;
        strcpy(oldest_file_name, entry->d_name);
      }
    }
    closedir(directory);

    result.remaining_count = recording_count;
    if (recording_count <= keep_count || oldest_number == UINT32_MAX) {
      return result;
    }
    if (unlink(recordingPath(folder, oldest_file_name, path_buffer, sizeof(path_buffer))) != 0) {
      LOG_ERROR("Couldn't delete %s (errno %d) - stopping the cleanup.\n", oldest_file_name, errno);
      return result;
    }
    result.deleted_count++;
    result.remaining_count = recording_count - 1;
    if (keep_count > 0) {
      LOG_INFO("Deleted the oldest %s, %s - keeping the newest %u.\n", folder.description, oldest_file_name,
               (unsigned)keep_count);
    } else {
      LOG_INFO("Deleted %s from the SD card.\n", oldest_file_name);
    }
    if (on_deleted != nullptr) {
      on_deleted(oldest_file_name);
    }
  }
}

bool prepareRecordingFolder(const RecordingFolder& folder, RecordingDeletedListener on_deleted,
                            uint32_t* next_number, uint32_t* kept_count) {
  const char* directory_path = recordingPath(folder, nullptr, path_buffer, sizeof(path_buffer));
  if (mkdir(directory_path, 0777) != 0 && errno != EEXIST) {
    LOG_ERROR("Couldn't create %s on the SD card (errno %d).\n", folder.directory, errno);
    return false;
  }
  DIR* directory = opendir(directory_path);
  if (directory == nullptr) {
    LOG_ERROR("Couldn't open %s on the SD card (errno %d).\n", folder.directory, errno);
    return false;
  }
  uint32_t recording_count = 0;
  uint32_t highest_number = 0;
  while (struct dirent* entry = readdir(directory)) {
    uint32_t number = 0;
    if (parseRecordingNumber(folder, entry->d_name, &number)) {
      recording_count++;
      highest_number = max(highest_number, number);
    }
  }
  closedir(directory);

  *next_number = highest_number + 1;
  if (recording_count > kMaxKeptRecordings) {
    recording_count = deleteOldestRecordings(folder, kMaxKeptRecordings, on_deleted).remaining_count;
  }
  *kept_count = recording_count;
  return true;
}

}  // namespace esp32va_recordings
