#pragma once

#include <stddef.h>
#include <stdint.h>

// Full-duplex I2S mic+speaker hardware layer, plus a playback ring buffer and
// G.711 mu-law decode - the audio I/O plumbing shared by every test sketch
// that streams mic audio into the OpenAI Realtime API and plays its spoken
// reply back out, over this board's single shared-clock I2S port (BCLK/LRC
// are physically the same GPIOs for both the INMP441 and the MAX98357A - see
// PROJECT_OVERVIEW.md "Shared I2S port, not two independent ones"). Pin
// assignments come straight from config.h; sample rate/DMA sizing and the
// ring buffer's capacity are fixed internally to the values steps 12-14
// confirmed working on real hardware.

/** Configures I2S_NUM_0 as a full-duplex master: RX from the INMP441, TX to
    the MAX98357A, both at 24kHz (the input-rate floor discovered in step 10).
    Halts (loops forever, logging) if the driver install fails - continuing
    past a failed install previously caused a Guru Meditation crash-reboot
    loop on this board (old step 8b). Call once from setup(). */
void setupI2sFullDuplex();

/** Reads up to max_samples of raw I2S mic audio into raw_scratch, converts
    the INMP441's 24-in-32-bit frames to 16-bit PCM in pcm_out with software
    gain applied (clamped to the int16 range), and returns the number of
    samples actually read. level_min/level_max/level_sum_of_squares are
    updated (not reset) with the gained samples from this call, so a caller
    accumulating stats across an entire capture should zero/reset them once
    before the first chunk and read them back after the last one (e.g. for an
    RMS figure). raw_scratch and pcm_out must each hold at least max_samples
    elements. */
uint32_t readMicChunkGained(int32_t* raw_scratch, int16_t* pcm_out, uint32_t max_samples, int32_t gain,
                             int16_t* level_min, int16_t* level_max, double* level_sum_of_squares);

/** Reads exactly sample_count mic samples, blocking until they've all
    arrived, as the mic's own 16-bit values (the I2S word >> 16, no gain) -
    for step 30's mic task (mic_capture_utils.h). raw_scratch must hold
    sample_count words. */
void readMicSamples(int32_t* raw_scratch, int16_t* samples_out, uint32_t sample_count);

/** Startup mic check (step 28): reads ~0.4 s from the mic and checks it
    isn't stuck at one value, as it is with no mic answering. Writes a short
    human-readable result into detail_out either way. Returns true if the mic
    looks alive. Only tells "sending audio" from "not" - a floating data line
    might still pass. Call once from setup(), after setupI2sFullDuplex() -
    and before mic_capture_utils.h's task starts reading the mic. */
bool checkMicrophoneSignal(char* detail_out, size_t detail_size);

/** Sets the speaker volume, 0-100% (anything higher counts as 100%), or
    silences it entirely when muted. Applied when playing audio, so the
    replies recorded on the SD card stay exactly as OpenAI sent them. 100%
    and not muted is how every step before 28 sounded. */
void setPlaybackVolume(uint8_t percent, bool muted);

/** Plays a short 880 Hz beep (0.3 s) at the current volume, blocking until
    it's queued - for checking by ear that the speaker works. Only call while
    nothing else is playing. */
void playTestBeep();

/** How much mic audio the I2S driver can hold, in milliseconds, before it
    starts dropping it. If handling one chunk takes longer than this (or
    keeps taking longer than a chunk lasts), audio is lost between reads. */
uint32_t micBufferCapacityMs();

// ----- Playback ring buffer -----
// Holds raw mu-law bytes, not decoded PCM - decoding and 3x upsampling
// (8kHz mu-law -> the shared port's 24kHz) happen at drain time instead, so
// the buffer holds ~4.1s of audio at its 32KB capacity (64KB until step 24)
// rather than ~0.7s of decoded PCM.

/** Allocates the ring buffer on the heap (not as a static array - growing it
    to 64KB previously overflowed the linker's much smaller static-allocation
    budget; see PROJECT_OVERVIEW.md "Ring buffer is heap-allocated, not
    static"). Call once from setup(), before Wi-Fi/TLS have fragmented the
    heap. Halts if allocation fails. */
void initPlaybackRingBuffer();

/** Resets the ring buffer to empty (and stops the recording tap, below). Call
    at the start of each new reply and on disconnect, so stale/partial audio
    from a previous cycle never gets mixed into the next one. */
void resetRingBuffer();

/** Bytes currently queued in the ring buffer, awaiting playback. */
uint32_t ringUsedBytes();

/** Bytes free in the ring buffer right now - space still held for the
    recording tap (below) counts as used, even if playback is past it. */
uint32_t ringFreeBytes();

// ----- Recording tap -----
// A second, independent read cursor over the same ring, so a reply can be
// saved (reply_recorder_utils) without a buffer of its own: every byte
// written while the tap is active stays in the ring until BOTH playback and
// the tap have read it. If the tap falls behind (e.g. a slow SD write), the
// ring fills up and the existing backpressure pauses WebSocket reads - no
// audio is dropped from either.

/** Starts the tap at the ring's current write position: everything written
    from now on is kept for ringReadRecordingTap(). Call before writing the
    first byte that should be recorded. */
void ringStartRecordingTap();

/** Stops the tap and forgets whatever it hadn't read yet. */
void ringStopRecordingTap();

/** Bytes written since the tap started that it hasn't read yet. */
uint32_t ringRecordingTapPendingBytes();

/** Copies up to max_len unread bytes from the tap into out. Returns how
    many were copied. */
uint32_t ringReadRecordingTap(uint8_t* out, uint32_t max_len);

/** Base64-decodes a (possibly large) span of mu-law audio in small
    fixed-size aligned slices, writing the raw mu-law bytes straight into the
    ring buffer - decode-to-PCM and upsampling happen later, at drain time.
    Drops (and logs) a slice that doesn't fit in the ring rather than
    blocking or overwriting unread data, and the whole span while the ring
    is lent to the mic (below). Never allocates proportionally to b64_len
    (see PROJECT_OVERVIEW.md "Audio blew the heap, again"). */
void decodeAndRingAudioDelta(const uint8_t* b64_data, size_t b64_len);

/** Drains a small, fixed amount of raw mu-law audio from the ring buffer (if
    any is queued), decodes + upsamples it, and writes it to the speaker via
    i2s_write(). A no-op when the ring is empty. Safe to call every loop()
    pass - only ever blocks as long as i2s_write() naturally does for one
    small chunk, so this must not be called from inside the WebSocket
    library's own callback (same category of risk as the sendTXT()-from-
    callback deadlock found in old step 7 - see header note in the caller). */
void drainRingBufferToSpeaker();

/** Hysteresis check for backpressure: returns whether the caller should be
    paused (not reading more off the WebSocket) right now, given whether it
    was already paused going into this call. Pause kicks in once the ring is
    nearly full, resume once enough of it has drained back down - two
    different thresholds so this doesn't flip on/off right at one boundary.
    Not reading from the socket makes TCP's own flow control tell the server
    to pause, which is what actually paces an arbitrarily long reply to real
    playback speed (see PROJECT_OVERVIEW.md "Backpressure, not a bigger
    buffer"). */
bool ringShouldPauseReads(bool currently_paused);

// ----- Lending the ring to the mic (step 30) -----
// While nothing plays, the ring's memory sits unused - and hands-free
// listening needs a pre-roll and a FIFO for the mic (mic_capture_utils.h),
// which the heap has no room for besides. So the ring is lent out, holding
// the mic's 24 kHz samples as mu-law bytes like the replies (one byte each,
// not two): ~1.36 s of them. Half-duplex keeps that safe - nothing may play
// while the mic has it, and a reply only starts arriving after the capture
// ended and the ring came back.

/** Lends the ring to the mic: returns its memory, and how many mu-law
    samples it holds in *capacity_samples, when it's empty and the recording
    tap is off - nothing playing or being saved. Otherwise nullptr (the ring
    stays as it is). While lent, playback has nothing to play and incoming
    reply audio is dropped (logged). */
uint8_t* lendRingToMic(uint32_t* capacity_samples);

/** Takes the ring back from the mic, empty, for playback. The caller must
    be sure the mic no longer writes into it. A no-op if it isn't lent. */
void returnRingFromMic();

/** Whether the ring is lent to the mic right now. */
bool isRingLentToMic();

// ----- G.711 mu-law -----

/** One 16-bit PCM sample as its 8-bit mu-law code (the classic G.711
    encode, matching ulawToLinearPcm16()). */
uint8_t linearPcm16ToUlaw(int16_t sample);

/** One 8-bit mu-law code as a 16-bit PCM sample (the classic public-domain
    G.711 decode). */
int16_t ulawToLinearPcm16(uint8_t ulaw_sample);
