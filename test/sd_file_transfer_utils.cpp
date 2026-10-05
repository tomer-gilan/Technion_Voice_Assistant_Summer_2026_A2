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
#include "reply_recorder_utils.h"
#include "sd_card_utils.h"

namespace esp32va_sd_transfer {

namespace {

constexpr char kListCommand[] = "LIST";
constexpr char kGetCommand[] = "GET";
constexpr char kDeleteCommand[] = "DELETE";
constexpr char kDeleteAllCommand[] = "DELETE_ALL";
constexpr size_t kMaxFileNameLen = 31;
constexpr size_t kMaxPathLen = 64;
constexpr uint32_t kTransferChunkBytes = 512;
constexpr uint32_t kCrc32Polynomial = 0xEDB88320;  // IEEE 802.3, bit-reversed - the zlib.crc32 variant

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

/** Full POSIX path of the recordings folder, or of file_name inside it. */
const char* recordingPath(const char* file_name) {
  if (file_name == nullptr) {
    snprintf(path_buffer, sizeof(path_buffer), "%s%s", esp32va_sd_card::kSdMountPoint,
             esp32va_reply_recorder::kRepliesDirectory);
  } else {
    snprintf(path_buffer, sizeof(path_buffer), "%s%s/%s", esp32va_sd_card::kSdMountPoint,
             esp32va_reply_recorder::kRepliesDirectory, file_name);
  }
  return path_buffer;
}

/** True if name is a plain file name - 1 to kMaxFileNameLen letters,
    digits, '_', '-' or '.', not starting with '.' - so a GET can never
    reach outside the recordings folder. */
bool isPlainFileName(const char* name) {
  size_t length = strlen(name);
  if (length == 0 || length > kMaxFileNameLen || name[0] == '.') {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
      return false;
    }
  }
  return true;
}

/** Answers LIST: one @@FILE line per file in the recordings folder. */
void listRecordings() {
  DIR* directory = opendir(recordingPath(nullptr));
  if (directory == nullptr) {
    Serial.printf("@@LIST_ERROR couldn't open %s (errno %d)\n", esp32va_reply_recorder::kRepliesDirectory, errno);
    return;
  }
  Serial.println("@@LIST_BEGIN");
  uint32_t file_count = 0;
  while (struct dirent* entry = readdir(directory)) {
    if (entry->d_type == DT_DIR) {
      continue;
    }
    struct stat file_info;
    if (stat(recordingPath(entry->d_name), &file_info) != 0) {
      LOG_ERROR("Couldn't read the size of %s (errno %d) - left out of LIST.\n", entry->d_name, errno);
      continue;
    }
    Serial.printf("@@FILE %s %ld\n", entry->d_name, (long)file_info.st_size);
    file_count++;
  }
  closedir(directory);
  Serial.printf("@@LIST_END %u\n", (unsigned)file_count);
  LOG_DEBUG("LIST answered: %u recording(s).\n", (unsigned)file_count);
}

/** Answers GET: header line, the file's raw bytes, then a CRC trailer. If
    the card stops returning data partway, pads with zeros up to the
    announced size (so the app stays in step with the stream) and sends
    @@GET_ERROR instead of @@GET_END. */
void sendRecording(const char* file_name) {
  if (!isPlainFileName(file_name)) {
    Serial.printf("@@GET_ERROR %s not a plain file name in %s\n", file_name,
                  esp32va_reply_recorder::kRepliesDirectory);
    return;
  }
  int file_descriptor = open(recordingPath(file_name), O_RDONLY);
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

/** Answers DELETE: removes one file from the recordings folder. */
void deleteRecording(const char* file_name) {
  if (!isPlainFileName(file_name)) {
    Serial.printf("@@DELETE_ERROR %s not a plain file name in %s\n", file_name,
                  esp32va_reply_recorder::kRepliesDirectory);
    return;
  }
  if (unlink(recordingPath(file_name)) != 0) {
    Serial.printf("@@DELETE_ERROR %s couldn't delete it (errno %d)\n", file_name, errno);
    return;
  }
  announceDeletedRecording(file_name);
  LOG_INFO("Deleted %s from the SD card.\n", file_name);
}

/** Answers DELETE_ALL: deletes every recording, announcing each, then says
    how many went and how many are left (non-zero only if one failed). */
void deleteAllRecordings() {
  esp32va_reply_recorder::CleanupResult result =
      esp32va_reply_recorder::deleteOldestRecordings(0, announceDeletedRecording);
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
    deleteAllRecordings();
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
