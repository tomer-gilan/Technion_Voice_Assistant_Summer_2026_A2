#include "heap_diagnostics_utils.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "log_utils.h"

namespace esp32va_heap {

namespace {

constexpr uint32_t kMallocCaps = MALLOC_CAP_8BIT;  // see header comment
constexpr size_t kMaxSnapshots = 12;

struct HeapSnapshot {
  const char* label;
  uint32_t free_bytes;
  uint32_t largest_free_block_bytes;
  uint32_t lowest_free_since_boot_bytes;
};

HeapSnapshot snapshots[kMaxSnapshots];
size_t snapshot_count = 0;

// Updated from inside whichever task's allocation failed - hence volatile,
// and nothing but plain counter updates in the hook itself.
volatile uint32_t failed_allocation_count = 0;
volatile uint32_t largest_failed_allocation_bytes = 0;

/** Heap-failure hook: counts the failure and tracks the largest failed
    request. Runs inside the failing allocation call, so it must not log or
    allocate. IRAM_ATTR so it stays callable even if an allocation fails
    while the flash cache is disabled. */
void IRAM_ATTR onAllocationFailed(size_t requested_bytes, uint32_t caps, const char* function_name) {
  (void)caps;
  (void)function_name;
  failed_allocation_count++;
  if (requested_bytes > largest_failed_allocation_bytes) {
    largest_failed_allocation_bytes = requested_bytes;
  }
}

}  // namespace

void startCountingFailedAllocations() {
  esp_err_t result = heap_caps_register_failed_alloc_callback(onAllocationFailed);
  if (result != ESP_OK) {
    LOG_ERROR("Couldn't register the failed-allocation hook (code %d) - failures won't be counted.\n",
              (int)result);
  }
}

uint32_t freeHeapBytes() {
  return heap_caps_get_free_size(kMallocCaps);
}

uint32_t lowestFreeHeapSinceBootBytes() {
  return heap_caps_get_minimum_free_size(kMallocCaps);
}

uint32_t recordHeapSnapshot(const char* label) {
  HeapSnapshot snapshot = {label, freeHeapBytes(),
                           (uint32_t)heap_caps_get_largest_free_block(kMallocCaps),
                           lowestFreeHeapSinceBootBytes()};
  LOG_DEBUG("Heap at \"%s\": free %u, largest free block %u, lowest free since boot %u, "
            "failed allocations so far %u.\n",
            label, (unsigned)snapshot.free_bytes, (unsigned)snapshot.largest_free_block_bytes,
            (unsigned)snapshot.lowest_free_since_boot_bytes, (unsigned)failed_allocation_count);
  if (snapshot_count < kMaxSnapshots) {
    snapshots[snapshot_count++] = snapshot;
  } else {
    LOG_ERROR("Heap snapshot table full - \"%s\" left out of the summary.\n", label);
  }
  return snapshot.free_bytes;
}

void logHeapSummary() {
  LOG_INFO("Heap summary (bytes malloc() can use):\n");
  LOG_INFO("  %-28s %8s %8s %14s %13s\n", "stage", "free", "change", "largest block", "lowest so far");
  for (size_t i = 0; i < snapshot_count; i++) {
    const HeapSnapshot& snapshot = snapshots[i];
    if (i == 0) {
      LOG_INFO("  %-28s %8u %8s %14u %13u\n", snapshot.label, (unsigned)snapshot.free_bytes, "-",
               (unsigned)snapshot.largest_free_block_bytes, (unsigned)snapshot.lowest_free_since_boot_bytes);
    } else {
      long change = (long)snapshot.free_bytes - (long)snapshots[i - 1].free_bytes;
      LOG_INFO("  %-28s %8u %+8ld %14u %13u\n", snapshot.label, (unsigned)snapshot.free_bytes, change,
               (unsigned)snapshot.largest_free_block_bytes, (unsigned)snapshot.lowest_free_since_boot_bytes);
    }
  }
  LOG_INFO("Failed allocations since boot: %u (largest request %u bytes).\n",
           (unsigned)failed_allocation_count, (unsigned)largest_failed_allocation_bytes);
}

}  // namespace esp32va_heap
