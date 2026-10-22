#include "sd_file_transfer_utils.h"

#include <Arduino.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log_utils.h"
#include "recording_folder_utils.h"

namespace esp32va_sd_transfer {

namespace {

using esp32va_recordings::RecordingFolder;

constexpr char kListCommand[] = "LIST";
constexpr char kGetCommand[] = "GET";
constexpr char kDeleteCommand[] = "DELETE";
constexpr char kDeleteAllCommand[] = "DELETE_ALL";
constexpr char kRepliesArgument[] = "replies";
constexpr char kCommandsArgument[] = "commands";
constexpr size_t kMaxPathLen = 64;
constexpr uint32_t kTransferChunkBytes = 512;
constexpr uint32_t kCrc32Polynomial = 0xEDB88320;  // IEEE 802.3, bit-reversed - the zlib.crc32 variant

const RecordingFolder* const kAllFolders[] = {&esp32va_recordings::kReplyFolder,
                                              &esp32va_recordings::kVoiceCommandFolder};

char path_buffer[kMaxPathLen];
uint8_t transfer_chunk[kTransferChunkBytes];

/** Standard CRC-32 (the zlib variant - the laptop app checks against it), chainable the
    same way: crc32Update(crc32Update(0, a), b) is the CRC of a followed by
    b. Bitwise rather than table-driven - slower, but no 1KB table, and a
    whole reply still takes only milliseconds. */
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  crc = ~crc;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (kCrc32Polynomial & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

/** Answers LIST: one @@FILE line per recording, replies first, then voice
    commands. A folder that can't be read is logged and skipped. */
void listRecordings() {
  Serial.println("@@LIST_BEGIN");
  uint32_t file_count = 0;
  for (const RecordingFolder* folder : kAllFolders) {
    DIR* directory = opendir(esp32va_recordings::recordingPath(*folder, nullptr, path_buffer, sizeof(path_buffer)));
    if (directory == nullptr) {
      LOG_ERROR("Couldn't open %s (errno %d) - left out of LIST.\n", folder->directory, errno);
      continue;
    }
    while (struct dirent* entry = readdir(directory)) {
      uint32_t unused_number = 0;
      if (!esp32va_recordings::parseRecordingNumber(*folder, entry->d_name, &unused_number)) {
        continue;
      }
      struct stat file_info;
      if (stat(esp32va_recordings::recordingPath(*folder, entry->d_name, path_buffer, sizeof(path_buffer)),
               &file_info) != 0) {
        LOG_ERROR("Couldn't read the size of %s (errno %d) - left out of LIST.\n", entry->d_name, errno);
        continue;
      }
      Serial.printf("@@FILE %s %ld\n", entry->d_name, (long)file_info.st_size);
      file_count++;
    }
    closedir(directory);
  }
  Serial.printf("@@LIST_END %u\n", (unsigned)file_count);
  LOG_DEBUG("LIST answered: %u recording(s).\n", (unsigned)file_count);
}

/** Answers GET: header line, the file's raw bytes, then a CRC trailer. If
    the card stops returning data partway, pads with zeros up to the
    announced size (so the app stays in step with the stream) and sends
    @@GET_ERROR instead of @@GET_END. */
void sendRecording(const char* file_name) {
  const RecordingFolder* folder = esp32va_recordings::folderForRecordingName(file_name);
  if (folder == nullptr) {
    Serial.printf("@@GET_ERROR %s not a recording name\n", file_name);
    return;
  }
  int file_descriptor =
      open(esp32va_recordings::recordingPath(*folder, file_name, path_buffer, sizeof(path_buffer)), O_RDONLY);
  if (file_descriptor < 0) {
    Serial.printf("@@GET_ERROR %s couldn't open it (errno %d)\n", file_name, errno);
    return;
  }
  struct stat file_info;
  if (fstat(file_descriptor, &file_info) != 0) {
    Serial.printf("@@GET_ERROR %s couldn't read its size (errno %d)\n", file_name, errno);
    close(file_descriptor);
    return;
  }

  uint32_t file_size = static_cast<uint32_t>(file_info.st_size);
  Serial.printf("@@GET_BEGIN %s %u\n", file_name, (unsigned)file_size);
  uint32_t bytes_sent = 0;
  uint32_t crc = 0;
  while (bytes_sent < file_size) {
    ssize_t bytes_read = read(file_descriptor, transfer_chunk, min(kTransferChunkBytes, file_size - bytes_sent));
    if (bytes_read <= 0) {
      break;
    }
    crc = crc32Update(crc, transfer_chunk, bytes_read);
    Serial.write(transfer_chunk, bytes_read);
    bytes_sent += bytes_read;
  }
  close(file_descriptor);

  if (bytes_sent < file_size) {
    uint32_t real_bytes = bytes_sent;
    memset(transfer_chunk, 0, sizeof(transfer_chunk));
    while (bytes_sent < file_size) {
      uint32_t padding_bytes = min(kTransferChunkBytes, file_size - bytes_sent);
      Serial.write(transfer_chunk, padding_bytes);
      bytes_sent += padding_bytes;
    }
    Serial.printf("@@GET_ERROR %s read failed after %u of %u bytes\n", file_name, (unsigned)real_bytes,
                  (unsigned)file_size);
    LOG_ERROR("Reading %s failed after %u of %u bytes.\n", file_name, (unsigned)real_bytes, (unsigned)file_size);
    return;
  }
  Serial.printf("@@GET_END %s %08x\n", file_name, (unsigned)crc);
  LOG_INFO("Sent %s to the laptop (%u bytes).\n", file_name, (unsigned)file_size);
}

/** Answers DELETE: removes one recording from its folder. */
void deleteRecording(const char* file_name) {
  const RecordingFolder* folder = esp32va_recordings::folderForRecordingName(file_name);
  if (folder == nullptr) {
    Serial.printf("@@DELETE_ERROR %s not a recording name\n", file_name);
    return;
  }
  if (unlink(esp32va_recordings::recordingPath(*folder, file_name, path_buffer, sizeof(path_buffer))) != 0) {
    Serial.printf("@@DELETE_ERROR %s couldn't delete it (errno %d)\n", file_name, errno);
    return;
  }
  announceDeletedRecording(file_name);
  LOG_INFO("Deleted %s from the SD card.\n", file_name);
}

/** Answers DELETE_ALL [replies|commands]: deletes every recording in that
    folder (replies when no folder is given), announcing each, then says how
    many went and how many are left (non-zero only if one failed). */
void deleteAllRecordings(const char* folder_argument) {
  const RecordingFolder* folder = nullptr;
  if (*folder_argument == '\0' || strcasecmp(folder_argument, kRepliesArgument) == 0) {
    folder = &esp32va_recordings::kReplyFolder;
  } else if (strcasecmp(folder_argument, kCommandsArgument) == 0) {
    folder = &esp32va_recordings::kVoiceCommandFolder;
  } else {
    Serial.printf("@@DELETE_ALL_ERROR unknown folder \"%s\" (use %s or %s)\n", folder_argument, kRepliesArgument,
                  kCommandsArgument);
    return;
  }
  esp32va_recordings::CleanupResult result =
      esp32va_recordings::deleteOldestRecordings(*folder, 0, announceDeletedRecording);
  Serial.printf("@@DELETE_ALL_DONE %u %u\n", (unsigned)result.deleted_count, (unsigned)result.remaining_count);
}

/** If command is `word` alone or `word <argument>` (case-insensitive),
    returns the argument with leading spaces skipped ("" if there's none);
    otherwise nullptr. So "LISTEN" is not a LIST command. */
const char* commandArgument(const char* command, const char* word) {
  size_t word_length = strlen(word);
  if (strncasecmp(command, word, word_length) != 0) {
    return nullptr;
  }
  const char* rest = command + word_length;
  if (*rest != ' ' && *rest != '\0') {
    return nullptr;
  }
  while (*rest == ' ') {
    rest++;
  }
  return rest;
}

}  // namespace

bool handleFileCommand(const char* command, bool transfers_allowed) {
  const char* list_argument = commandArgument(command, kListCommand);
  const char* get_argument = commandArgument(command, kGetCommand);
  const char* delete_argument = commandArgument(command, kDeleteCommand);
  const char* delete_all_argument = commandArgument(command, kDeleteAllCommand);
  const char* command_word = nullptr;
  if (list_argument != nullptr) {
    command_word = kListCommand;
  } else if (get_argument != nullptr) {
    command_word = kGetCommand;
  } else if (delete_argument != nullptr) {
    command_word = kDeleteCommand;
  } else if (delete_all_argument != nullptr) {
    command_word = kDeleteAllCommand;
  } else {
    return false;
  }

  if (!transfers_allowed) {
    Serial.printf("@@BUSY %s\n", command_word);
    LOG_DEBUG("%s refused - a reply is in progress, still playing, or still being saved.\n", command);
    return true;
  }
  if (list_argument != nullptr) {
    listRecordings();
    return true;
  }
  if (delete_all_argument != nullptr) {
    deleteAllRecordings(delete_all_argument);
    return true;
  }
  const char* file_name = get_argument != nullptr ? get_argument : delete_argument;
  if (*file_name == '\0') {
    Serial.printf("@@%s_ERROR - missing file name (usage: %s <name>)\n", command_word, command_word);
    return true;
  }
  if (get_argument != nullptr) {
    sendRecording(file_name);
  } else {
    deleteRecording(file_name);
  }
  return true;
}

void announceRecordingList() {
  listRecordings();
}

void announceSavedRecording(const char* file_name, uint32_t size_bytes) {
  Serial.printf("@@SAVED %s %u\n", file_name, (unsigned)size_bytes);
}

void announceDeletedRecording(const char* file_name) {
  Serial.printf("@@DELETED %s\n", file_name);
}

}  // namespace esp32va_sd_transfer
