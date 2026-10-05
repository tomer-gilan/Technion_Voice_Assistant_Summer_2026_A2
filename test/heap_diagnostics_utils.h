#pragma once

#include <stdint.h>

// Heap measurements for judging what a feature costs in RAM and how much
// headroom is left for the TLS handshake to OpenAI. Every figure here is for
// byte-addressable memory (MALLOC_CAP_8BIT) - the pool plain malloc(), and so
// TLS, JSON parsing, the ring buffer and the SD mount, all draw from.
// ESP.getFreeHeap() may also count regions malloc() can't use (unconfirmed
// for this core version), which would overstate headroom - so these numbers
// won't line up 1:1 with older logs that printed ESP.getFreeHeap().

namespace esp32va_heap {

/** Starts counting failed heap allocations (any task, any size) so
    logHeapSummary() can report them. Call once, first thing in setup().
    Worth having because an allocation failure deep inside the TLS handshake
    can surface as an unrelated-looking error (suspected, not confirmed: the
    "X509 - Certificate verification failed" in PROJECT_OVERVIEW.md step 24). */
void startCountingFailedAllocations();

/** Bytes malloc() can use right now. */
uint32_t freeHeapBytes();

/** Lowest freeHeapBytes() has been at any point since boot (the low-water
    mark) - shows the peak cost of something that allocates and frees again,
    like a TLS handshake. */
uint32_t lowestFreeHeapSinceBootBytes();

/** Records a labelled snapshot (free, largest free block, low-water mark)
    for logHeapSummary(), logs it at DEBUG, and returns its free-heap figure.
    `label` must be a string literal - it's stored by pointer, not copied.
    Once the snapshot table is full, further snapshots are logged at ERROR
    and left out of the summary. */
uint32_t recordHeapSnapshot(const char* label);

/** Logs every recorded snapshot as a table at INFO - each row with its
    change from the row before it, so a stage's cost reads straight off the
    "change" column - plus the failed-allocation count. */
void logHeapSummary();

}  // namespace esp32va_heap
