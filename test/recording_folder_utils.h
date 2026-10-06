#pragma once

#include <stddef.h>
#include <stdint.h>

// A folder of numbered recordings on the SD card - replies (step 25) and
// voice commands (step 27) - and what both share: where the folder is, how
// files are named (<prefix><4-digit number><extension>, numbers only ever
// counting up, so the lowest is the oldest), finding the next number, and
// keeping only the newest kMaxKeptRecordings. POSIX calls on the card's
// mount point (sd_card_utils.h). SD work: call from setup() or loop() only,
// and never on a folder whose file is still being written.

namespace esp32va_recordings {

struct RecordingFolder {
  const char* directory;       // relative to the card's root, e.g. "/replies"
  const char* file_prefix;     // e.g. "reply_"
  const char* file_extension;  // e.g. ".ulaw"
  const char* description;     // for log lines, e.g. "reply"
};

/** OpenAI's spoken replies: raw 8kHz G.711 mu-law, reply_NNNN.ulaw. */
extern const RecordingFolder kReplyFolder;
/** The mic audio of each LISTEN: raw 16-bit little-endian PCM at 24kHz,
    command_NNNN.pcm. */
extern const RecordingFolder kVoiceCommandFolder;

/** How many recordings each folder keeps (the user's call, 2026-10-05). */
constexpr uint32_t kMaxKeptRecordings = 5;

/** Longest recording name, terminator included ("command_0001.pcm" is 17). */
constexpr size_t kMaxRecordingNameSize = 24;

/** Told the name of each recording deleted on its own (by a trim), so the
    caller can pass it on - e.g. to the laptop app - without this module
    knowing the serial protocol. */
using RecordingDeletedListener = void (*)(const char* file_name);

struct CleanupResult {
  uint32_t deleted_count;
  uint32_t remaining_count;  // recordings still in the folder afterwards
};

/** POSIX path of the folder (file_name == nullptr) or of a file in it,
    written into out. Returns out. */
const char* recordingPath(const RecordingFolder& folder, const char* file_name, char* out, size_t out_size);

/** If name is <prefix><number><extension> (case-insensitive) for this
    folder, stores the number and returns true; otherwise returns false. A
    name that passes can't contain a path, so it's safe to use in one. */
bool parseRecordingNumber(const RecordingFolder& folder, const char* name, uint32_t* number);

/** Writes <prefix><number, at least 4 digits><extension> into out. */
void formatRecordingName(const RecordingFolder& folder, uint32_t number, char* out, size_t out_size);

/** The folder a recording name belongs to, or nullptr if it isn't a valid
    recording name for either. */
const RecordingFolder* folderForRecordingName(const char* name);

/** Creates the folder if needed, sets *next_number to one past the highest
    number in it, and trims it to kMaxKeptRecordings (telling on_deleted, if
    not null, about each deletion). Sets *kept_count to how many remain.
    Returns false (logged) if the folder can't be created or read. */
bool prepareRecordingFolder(const RecordingFolder& folder, RecordingDeletedListener on_deleted,
                            uint32_t* next_number, uint32_t* kept_count);

/** Deletes the oldest recordings until at most keep_count remain - 0 deletes
    them all - telling on_deleted (if not null) about each one. Only files
    named like this folder's recordings count; anything else is left alone.
    Stops at the first file that can't be deleted (logged). */
CleanupResult deleteOldestRecordings(const RecordingFolder& folder, uint32_t keep_count,
                                     RecordingDeletedListener on_deleted);

}  // namespace esp32va_recordings
