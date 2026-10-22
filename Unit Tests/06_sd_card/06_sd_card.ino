// Unit test 06 - SD card module (SPI)
// Mounts the card, prints its type and size, then writes a test file, reads
// it back, compares and deletes it - the same check the firmware runs at
// boot, on its own, so a wiring or card problem shows up here first.
//
// PASS: "===== SD CARD TEST PASSED =====". A FAILED banner names the step
// that failed (no card / not FAT32 / write / read / compare / delete).

#include <SD.h>
#include <SPI.h>

// Pins - the same as ESP32/parameters.h (each unit test stands alone, so it
// carries its own copy).
#define SD_CS   5
#define SD_SCLK 18
#define SD_MISO 19
#define SD_MOSI 23

namespace {
constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSpiFrequencyHz = 4000000;  // the firmware's speed (parameters.h)
constexpr uint32_t kBytesPerMegabyte = 1024 * 1024;
const char kTestPath[] = "/unit_test_06.txt";
const char kTestContent[] = "ESP32 voice assistant - SD card unit test";

/** Card type as a short name. */
const char* cardTypeName(uint8_t card_type) {
  switch (card_type) {
    case CARD_MMC:
      return "MMC";
    case CARD_SD:
      return "SDSC";
    case CARD_SDHC:
      return "SDHC/SDXC";
    case CARD_NONE:
      return "none";
    default:
      Serial.printf("Unexpected card type code %u.\n", (unsigned)card_type);
      return "unknown";
  }
}

/** Prints the verdict banner once. */
void reportVerdict(bool passed, const char* failed_step) {
  if (passed) {
    Serial.println("===== SD CARD TEST PASSED =====");
  } else {
    Serial.printf("===== SD CARD TEST FAILED: %s =====\n", failed_step);
  }
}

/** Writes, reads back, compares and deletes the test file. Returns the step
    that failed, or nullptr. */
const char* runReadWriteCheck() {
  File file = SD.open(kTestPath, FILE_WRITE);
  if (!file) {
    return "couldn't create the test file";
  }
  size_t written = file.print(kTestContent);
  file.close();
  if (written != strlen(kTestContent)) {
    return "writing the test file";
  }
  Serial.printf("Wrote %u bytes to %s.\n", (unsigned)written, kTestPath);

  file = SD.open(kTestPath, FILE_READ);
  if (!file) {
    return "couldn't open the test file again";
  }
  char read_back[sizeof(kTestContent)] = {};
  size_t read = file.readBytes(read_back, sizeof(read_back) - 1);
  file.close();
  if (read != strlen(kTestContent) || strcmp(read_back, kTestContent) != 0) {
    return "the read-back content didn't match";
  }
  Serial.println("Read it back - same content.");

  if (!SD.remove(kTestPath)) {
    return "deleting the test file";
  }
  Serial.println("Deleted it.");
  return nullptr;
}
}  // namespace

/** Runs the whole test once. */
void setup() {
  Serial.begin(kSerialBaudRate);
  delay(500);
  Serial.println("Unit test 06 - SD card. Mounting...");
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, kSpiFrequencyHz)) {
    Serial.println("Mount failed - no card, loose wiring (CS 5, SCK 18, MISO 19, MOSI 23, 3.3V, GND), or not FAT32.");
    reportVerdict(false, "no card, or it couldn't be mounted");
    return;
  }
  Serial.printf("Card: %s, %u MB.\n", cardTypeName(SD.cardType()), (unsigned)(SD.cardSize() / kBytesPerMegabyte));
  const char* failed_step = runReadWriteCheck();
  reportVerdict(failed_step == nullptr, failed_step);
  SD.end();
}

/** Nothing to repeat - press Reset to run it again. */
void loop() {
}
