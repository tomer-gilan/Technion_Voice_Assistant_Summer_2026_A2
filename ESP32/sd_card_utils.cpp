#include "sd_card_utils.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include "parameters.h"
#include "heap_diagnostics_utils.h"
#include "log_utils.h"
#include "status_report_utils.h"

namespace esp32va_sd_card {

namespace {

// The SD library's own default - ~500 KB/s raw, far more than the 8 KB/s
// that recording 8kHz mu-law replies (step 25) will need.
constexpr uint32_t kSpiFrequencyHz = esp32va_params::kSdSpiFrequencyHz;
// Every open-file slot is allocated at mount time and held for as long as
// the card stays mounted, used or not. The library default is 5; one is
// enough while only one file is ever open at a time. A future feature that
// needs two files open at once (e.g. playing a track while recording a
// reply) must raise this and re-measure the mount cost.
constexpr uint8_t kMaxOpenFiles = 1;

const char kSelfTestPath[] = "/esp32va_selftest.txt";
const char kSelfTestContent[] = "ESP32 voice assistant SD self-test";
constexpr size_t kSelfTestContentLen = sizeof(kSelfTestContent) - 1;
char self_test_read_buffer[kSelfTestContentLen + 1];

constexpr uint32_t kBytesPerMegabyte = 1024 * 1024;

/** Human-readable name for an SD.cardType() result. */
const char* cardTypeName(sdcard_type_t card_type) {
  switch (card_type) {
    case CARD_NONE:
      return "no card";
    case CARD_MMC:
      return "MMC";
    case CARD_SD:
      return "SDSC";
    case CARD_SDHC:
      return "SDHC/SDXC";
    case CARD_UNKNOWN:
      return "unknown type";
    default:
      LOG_ERROR("Unexpected SD card type code %d.\n", (int)card_type);
      return "unrecognized type";
  }
}

bool sd_card_usable = false;  // see setSdCardUsable()
bool card_check_pending = false;  // see noteCardError()

/** True when the card's root folder can still be opened. Not SD.cardType():
    that only returns what was found at mount time. Opening a folder runs
    FatFs's own status check (an SD SEND_STATUS command) and needs none of
    the kMaxOpenFiles file slots. */
bool cardAnswers() {
  char root_path[sizeof(kSdMountPoint) + 1];
  snprintf(root_path, sizeof(root_path), "%s/", kSdMountPoint);
  DIR* root = opendir(root_path);
  if (root == nullptr) {
    LOG_DEBUG("SD re-check: opening %s failed (errno %d).\n", root_path, errno);
    return false;
  }
  closedir(root);
  return true;
}

}  // namespace

bool mountSdCard() {
  uint32_t free_before_mount = esp32va_heap::freeHeapBytes();
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, kSpiFrequencyHz, kSdMountPoint, kMaxOpenFiles)) {
    LOG_ERROR("SD card mount failed - no card, loose wiring, or not FAT32-formatted.\n");
    return false;
  }
  uint32_t free_after_mount = esp32va_heap::freeHeapBytes();

  LOG_INFO("SD card mounted (%s, %u MB) - mount cost %d bytes of heap.\n",
           cardTypeName(SD.cardType()), (unsigned)(SD.cardSize() / kBytesPerMegabyte),
           (int)(free_before_mount - free_after_mount));
  LOG_DEBUG("SD mount details: SPI at %u Hz, %u open-file slot(s), filesystem %u MB total / %u MB used, "
            "heap free %u -> %u.\n",
            (unsigned)kSpiFrequencyHz, (unsigned)kMaxOpenFiles, (unsigned)(SD.totalBytes() / kBytesPerMegabyte),
            (unsigned)(SD.usedBytes() / kBytesPerMegabyte), (unsigned)free_before_mount,
            (unsigned)free_after_mount);
  return true;
}

void describeSdCard(char* out, size_t out_size) {
  snprintf(out, out_size, "%s, %u MB", cardTypeName(SD.cardType()), (unsigned)(SD.cardSize() / kBytesPerMegabyte));
}

bool runSdSelfTest() {
  File test_file = SD.open(kSelfTestPath, FILE_WRITE);
  if (!test_file) {
    LOG_ERROR("SD self-test: couldn't create %s.\n", kSelfTestPath);
    return false;
  }
  size_t bytes_written = test_file.write(reinterpret_cast<const uint8_t*>(kSelfTestContent), kSelfTestContentLen);
  test_file.close();
  if (bytes_written != kSelfTestContentLen) {
    LOG_ERROR("SD self-test: wrote only %u of %u bytes.\n", (unsigned)bytes_written,
              (unsigned)kSelfTestContentLen);
    SD.remove(kSelfTestPath);
    return false;
  }

  test_file = SD.open(kSelfTestPath, FILE_READ);
  if (!test_file) {
    LOG_ERROR("SD self-test: couldn't reopen %s for reading.\n", kSelfTestPath);
    SD.remove(kSelfTestPath);
    return false;
  }
  size_t bytes_read = test_file.read(reinterpret_cast<uint8_t*>(self_test_read_buffer), kSelfTestContentLen);
  test_file.close();
  self_test_read_buffer[bytes_read] = '\0';

  bool contents_match = bytes_read == kSelfTestContentLen &&
                        memcmp(self_test_read_buffer, kSelfTestContent, kSelfTestContentLen) == 0;
  bool removed = SD.remove(kSelfTestPath);
  if (!contents_match) {
    LOG_ERROR("SD self-test: read back \"%s\", expected \"%s\".\n", self_test_read_buffer, kSelfTestContent);
    return false;
  }
  if (!removed) {
    LOG_ERROR("SD self-test: couldn't delete %s.\n", kSelfTestPath);
    return false;
  }

  LOG_INFO("SD card self-test passed (write, read back, delete).\n");
  return true;
}

void setSdCardUsable(bool usable) {
  sd_card_usable = usable;
}

bool isSdCardUsable() {
  return sd_card_usable;
}

void noteCardError() {
  card_check_pending = true;
}

void serviceCardCheck(bool board_idle) {
  if (!card_check_pending || !board_idle) {
    return;
  }
  card_check_pending = false;
  if (!sd_card_usable) {
    return;  // already marked unusable (or never was usable)
  }
  if (cardAnswers()) {
    LOG_INFO("SD card re-checked after a file error - it still answers, so it stays in use.\n");
    return;
  }
  sd_card_usable = false;
  LOG_ERROR("SD card stopped answering (removed?) - running without recordings until Reset.\n");
  esp32va_status::reportStage(esp32va_status::Stage::kSdCard, esp32va_status::StageState::kFail,
                              "card removed or failed - insert it and press Reset");
}

}  // namespace esp32va_sd_card
