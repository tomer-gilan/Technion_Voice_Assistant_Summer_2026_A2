#pragma once

#include <stdint.h>

// Serial file access for the laptop app (tools/reply_audio_app.html): lists the
// saved recordings - replies (reply_NNNN.ulaw) and voice commands
// (command_NNNN.pcm), see recording_folder_utils.h - and sends one back
// over the USB serial line. A name's prefix says which folder it's in.
// Responses are machine-readable lines starting with "@@" - printed raw,
// never through the LOG_* macros, the same way verdict banners are - so the
// app can pick them out of the normal log stream:
//
//   LIST         -> @@LIST_BEGIN
//                   @@FILE <name> <size in bytes>     (one per recording, both folders)
//                   @@LIST_END <count>
//   GET <name>   -> @@GET_BEGIN <name> <size>\n, then exactly <size> raw
//                   bytes, then @@GET_END <name> <crc32 as 8 hex digits>\n
//                or @@GET_ERROR <name> <reason>
//   DELETE <name> -> @@DELETED <name>
//                or @@DELETE_ERROR <name> <reason>
//   DELETE_ALL [replies|commands]   (replies if no folder is given)
//                -> @@DELETED <name>                  (one per recording)
//                   @@DELETE_ALL_DONE <deleted count> <count left>
//                or @@DELETE_ALL_ERROR <reason>
//   any of them, while a reply is in progress -> @@BUSY <LIST|GET|DELETE|DELETE_ALL>
//   (so a file is never deleted while it's still being written)
//
// Also sent unprompted, so the app's list is current without asking:
//   at boot                          -> the same @@LIST_BEGIN ... @@LIST_END block
//   whenever a recording is saved    -> @@SAVED <name> <size>
//   whenever a recorder drops its oldest file to keep only the newest
//   kMaxKeptRecordings               -> @@DELETED <name>
//
// The CRC is standard CRC-32 (the zlib variant, as the app computes it), so the app
// can tell a clean transfer from one corrupted by stray output. A GET runs
// start to finish inside one call (nothing else in loop() can print in the
// middle of it). At 115200 baud an 8kHz mu-law reply takes ~0.7 s of
// transfer per second of audio, and a 24kHz 16-bit voice command ~4.2 s.

namespace esp32va_sd_transfer {

/** Handles a typed Serial command if it's LIST, GET <name>, DELETE <name>
    or DELETE_ALL [replies|commands] (case-insensitive). When
    transfers_allowed is false - a turn is in progress, or a reply is still
    playing or being saved - answers @@BUSY instead, so the app can retry.
    Only recording names are accepted (no paths, nothing outside the two
    folders). Returns true if `command` was a file command (answered,
    refused as busy, or rejected), false if it's something else for the
    caller to handle. */
bool handleFileCommand(const char* command, bool transfers_allowed);

/** Prints the LIST block unprompted, so a laptop app that's already
    connected shows the recordings right away - before Wi-Fi and the
    OpenAI session hold up setup() and loop() can answer a LIST itself.
    Call once from setup(), after the reply recorder is set up. */
void announceRecordingList();

/** Prints "@@SAVED <name> <size>" so a connected laptop app adds a new
    recording to its list at once. One short line - cheap enough to send
    while the reply is still playing. */
void announceSavedRecording(const char* file_name, uint32_t size_bytes);

/** Prints "@@DELETED <name>" so a connected laptop app drops it from its
    list. Matches recording_folder_utils.h's RecordingDeletedListener, so it
    can be handed to the recorders for the files they delete on their own. */
void announceDeletedRecording(const char* file_name);

}  // namespace esp32va_sd_transfer
