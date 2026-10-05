#pragma once

// SD card over SPI (pins from config.h): mounting it with as little RAM as
// possible, plus a write/read-back self-test proving the card actually
// works rather than just mounted. Uses the ESP32 core's own SD library, so
// once mounted, paths given to SD.open()/SD.remove() etc. are relative to
// the card's root (e.g. "/replies/reply_0001.ulaw"), while POSIX calls
// (open()/opendir()/stat()) need kSdMountPoint in front.

namespace esp32va_sd_card {

/** Where the card's filesystem is mounted in the ESP32's virtual filesystem. */
constexpr char kSdMountPoint[] = "/sd";

/** Starts the SPI bus on config.h's SD pins and mounts the card's FAT
    filesystem with a single open-file slot (see kMaxOpenFiles in the .cpp
    for why). Logs the card type, its size, and how much heap the mount
    cost. Returns false (logged) if no card responds or it can't be mounted.
    Call once from setup(), before Wi-Fi/TLS have fragmented the heap. */
bool mountSdCard();

/** Writes a short test file, reads it back, compares, and deletes it.
    Returns true only if every step worked and the contents matched; logs
    which step failed otherwise. Call only after mountSdCard() succeeded. */
bool runSdSelfTest();

}  // namespace esp32va_sd_card
