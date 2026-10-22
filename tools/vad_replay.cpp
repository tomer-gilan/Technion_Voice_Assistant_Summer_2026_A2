// Replays recordings through the board's voice activity detector
// (ESP32/vad_utils.cpp, the exact same code) to see where hands-free
// listening would start and stop recording - and to tune it - without
// flashing anything. Simulates the board's armed loop: waiting for speech,
// recording from kPreRollMs before the start was confirmed, ending after the
// hangover (or discarding an utterance that didn't sound like speech), then
// waiting again.
//
// Build (any C++11 compiler; on Windows e.g. MSYS2/MinGW g++):
//   g++ -std=c++11 -O2 -I ESP32 tools/vad_replay.cpp ESP32/vad_utils.cpp -o vad_replay
// Run:
//   vad_replay [--sensitivity 1-5] [--hangover <ms>] [--frames] [--captures <folder>] <file>...
//
// Files: a voice command from the SD card (command_NNNN.pcm - raw 24 kHz
// 16-bit mono, the mic's own samples) or any 16-bit PCM .wav (mono or
// stereo, any sample rate - mixed to mono and resampled to 24 kHz; a higher
// rate such as 44.1 or 48 kHz is low-pass filtered at ~11 kHz first, so
// sound above the 24 kHz output's 12 kHz limit doesn't fold down into the
// band the detector analyses, the way the mic's own filter keeps it out on
// the board). The
// laptop app's "Download WAV" of a voice command is 24 kHz already; pick
// its plain variant (not gained) so the levels match what the board's
// detector sees. (Step 30's recordings went through mu-law in the mic
// buffer on their way to the SD card: speech replays as the detector heard
// it, but a very quiet room's noise floor reads a few dB differently.)
//   --frames      also prints every 20 ms frame's analysis as CSV
//   --captures    writes each recording that would be sent (or discarded)
//                 as a WAV file into that folder, to listen to the result
//
// What this harness does NOT model - its results are the detector's
// decisions on each clip, not exactly what the board would do while
// listening to the same sound for real:
//   - The pause: after kMaxDiscardsInARow (3) discards in a row the board
//     stops listening for kDiscardPauseMs (5 s, hands_free_utils.h); here
//     the next utterance in the file is judged right away.
//   - The re-arm delay: after a sent utterance the board listens again only
//     once the reply has played and kCooldownMs has passed; here the
//     detector restarts on the very next frame.
//   - The room-adapted noise floor: each file starts a fresh detector at
//     its initial floor, while the board's detector keeps the floor it has
//     learned in the room from one arming to the next.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "vad_utils.h"

namespace esp32va_vad_replay {

using esp32va_vad::DetectorEvent;
using esp32va_vad::DiscardReason;
using esp32va_vad::kFrameSamples;
using esp32va_vad::kSampleRateHz;

struct Options {
  uint8_t sensitivity = esp32va_vad::kDefaultSensitivity;
  uint32_t hangover_ms = esp32va_vad::kDefaultHangoverMs;
  bool print_frames = false;
  std::string captures_folder;
  std::vector<std::string> files;
};

/** Reads a whole file into memory. False if it can't be read. */
bool readFile(const std::string& path, std::vector<uint8_t>* bytes) {
  FILE* file = fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  bytes->resize(size > 0 ? static_cast<size_t>(size) : 0);
  size_t read = bytes->empty() ? 0 : fread(bytes->data(), 1, bytes->size(), file);
  fclose(file);
  return read == bytes->size();
}

/** Little-endian 16- and 32-bit reads from a byte buffer. */
uint16_t readU16(const uint8_t* at) { return static_cast<uint16_t>(at[0] | (at[1] << 8)); }
uint32_t readU32(const uint8_t* at) {
  return static_cast<uint32_t>(at[0] | (at[1] << 8) | (at[2] << 16) | (static_cast<uint32_t>(at[3]) << 24));
}

// The anti-aliasing low-pass before downsampling: a windowed-sinc (Blackman)
// FIR. At 48 kHz it passes up to 10 kHz flat, is -6 dB at 11 kHz and
// -67 dB at 12 kHz, the output's Nyquist frequency (-77 dB at 44.1 kHz).
constexpr int kAntiAliasTaps = 127;  // odd, so the filter has a center tap and adds no delay
constexpr double kAntiAliasCutoffHz = 11000.0;
constexpr double kPi = 3.14159265358979323846;

/** Low-pass filters mono samples at source_rate below kSampleRateHz / 2,
    so resampling them to kSampleRateHz doesn't alias. */
std::vector<float> lowPassBeforeDownsampling(const std::vector<float>& mono, uint32_t source_rate) {
  const double cutoff = kAntiAliasCutoffHz / source_rate;  // in cycles per sample
  const int half_taps = kAntiAliasTaps / 2;
  std::vector<double> taps(kAntiAliasTaps);
  double taps_sum = 0.0;
  for (int tap = 0; tap < kAntiAliasTaps; tap++) {
    int offset = tap - half_taps;
    double sinc = offset == 0 ? 2.0 * cutoff : sin(2.0 * kPi * cutoff * offset) / (kPi * offset);
    double window = 0.42 - 0.5 * cos(2.0 * kPi * tap / (kAntiAliasTaps - 1)) +
                    0.08 * cos(4.0 * kPi * tap / (kAntiAliasTaps - 1));
    taps[tap] = sinc * window;
    taps_sum += taps[tap];
  }
  for (double& value : taps) value /= taps_sum;  // unity gain at 0 Hz, so levels don't change
  const long sample_count = static_cast<long>(mono.size());
  std::vector<float> filtered(mono.size());
  for (long index = 0; index < sample_count; index++) {
    double sum = 0.0;
    for (int tap = 0; tap < kAntiAliasTaps; tap++) {
      long source = index + tap - half_taps;
      if (source >= 0 && source < sample_count) sum += taps[tap] * mono[source];  // silence past either end
    }
    filtered[index] = static_cast<float>(sum);
  }
  return filtered;
}

/** Linear-interpolation resample of mono samples to kSampleRateHz. Expects
    audio already band-limited below kSampleRateHz / 2 when source_rate is
    higher (lowPassBeforeDownsampling). */
std::vector<int16_t> resampleTo24k(const std::vector<float>& mono, uint32_t source_rate) {
  std::vector<int16_t> out;
  if (mono.empty()) return out;
  double step = static_cast<double>(source_rate) / kSampleRateHz;
  size_t out_count = static_cast<size_t>(mono.size() / step);
  out.reserve(out_count);
  for (size_t index = 0; index < out_count; index++) {
    double position = index * step;
    size_t left = static_cast<size_t>(position);
    double fraction = position - left;
    float right_value = left + 1 < mono.size() ? mono[left + 1] : mono[left];
    double value = mono[left] * (1.0 - fraction) + right_value * fraction;
    long rounded = lround(value);
    out.push_back(static_cast<int16_t>(rounded > 32767 ? 32767 : (rounded < -32768 ? -32768 : rounded)));
  }
  return out;
}

/** Loads a .wav (16-bit PCM) or raw .pcm (24 kHz 16-bit mono) file as 24 kHz
    mono samples. Prints why and returns false if it can't. */
bool loadAudio(const std::string& path, std::vector<int16_t>* samples) {
  std::vector<uint8_t> bytes;
  if (!readFile(path, &bytes)) {
    fprintf(stderr, "%s: can't read it\n", path.c_str());
    return false;
  }
  bool is_wav = bytes.size() >= 12 && memcmp(bytes.data(), "RIFF", 4) == 0 && memcmp(bytes.data() + 8, "WAVE", 4) == 0;
  if (!is_wav) {
    samples->resize(bytes.size() / 2);
    for (size_t index = 0; index < samples->size(); index++) {
      (*samples)[index] = static_cast<int16_t>(readU16(bytes.data() + 2 * index));
    }
    return true;
  }
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  const uint8_t* data = nullptr;
  uint32_t data_size = 0;
  size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    const uint8_t* chunk = bytes.data() + offset;
    uint32_t chunk_size = readU32(chunk + 4);
    if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
      format = readU16(chunk + 8);
      channels = readU16(chunk + 10);
      rate = readU32(chunk + 12);
      bits = readU16(chunk + 22);
    } else if (memcmp(chunk, "data", 4) == 0) {
      data = chunk + 8;
      data_size = static_cast<uint32_t>(
          chunk_size <= bytes.size() - offset - 8 ? chunk_size : bytes.size() - offset - 8);
    }
    offset += 8 + chunk_size + (chunk_size & 1);
  }
  if (format != 1 || bits != 16 || channels == 0 || rate == 0 || data == nullptr) {
    fprintf(stderr, "%s: only 16-bit PCM WAV is supported (format %u, %u bits)\n", path.c_str(), format, bits);
    return false;
  }
  size_t frames = data_size / (2u * channels);
  std::vector<float> mono(frames);
  for (size_t frame = 0; frame < frames; frame++) {
    float sum = 0.0f;
    for (uint16_t channel = 0; channel < channels; channel++) {
      sum += static_cast<int16_t>(readU16(data + 2 * (frame * channels + channel)));
    }
    mono[frame] = sum / channels;
  }
  if (rate == kSampleRateHz) {
    samples->resize(frames);
    for (size_t frame = 0; frame < frames; frame++) (*samples)[frame] = static_cast<int16_t>(mono[frame]);
  } else if (rate > kSampleRateHz) {
    *samples = resampleTo24k(lowPassBeforeDownsampling(mono, rate), rate);
  } else {
    *samples = resampleTo24k(mono, rate);  // upsampling adds nothing above the source's own limit
  }
  return true;
}

/** Writes samples[first, last) as a 24 kHz mono 16-bit WAV file. */
void writeWav(const std::string& path, const std::vector<int16_t>& samples, size_t first, size_t last) {
  FILE* file = fopen(path.c_str(), "wb");
  if (file == nullptr) {
    fprintf(stderr, "can't write %s\n", path.c_str());
    return;
  }
  uint32_t data_bytes = static_cast<uint32_t>((last - first) * 2);
  uint8_t header[44];
  memcpy(header, "RIFF", 4);
  uint32_t riff_size = 36 + data_bytes;
  memcpy(header + 4, &riff_size, 4);
  memcpy(header + 8, "WAVEfmt ", 8);
  uint32_t fmt_size = 16, rate = kSampleRateHz, byte_rate = kSampleRateHz * 2;
  uint16_t audio_format = 1, channels = 1, block_align = 2, bits = 16;
  memcpy(header + 16, &fmt_size, 4);
  memcpy(header + 20, &audio_format, 2);
  memcpy(header + 22, &channels, 2);
  memcpy(header + 24, &rate, 4);
  memcpy(header + 28, &byte_rate, 4);
  memcpy(header + 32, &block_align, 2);
  memcpy(header + 34, &bits, 2);
  memcpy(header + 36, "data", 4);
  memcpy(header + 40, &data_bytes, 4);
  fwrite(header, 1, sizeof(header), file);
  fwrite(samples.data() + first, 2, last - first, file);
  fclose(file);
}

/** Why an utterance was dropped, in words. */
const char* discardReasonText(DiscardReason reason) {
  switch (reason) {
    case DiscardReason::kNone:
      return "not discarded";
    case DiscardReason::kTooShort:
      return "too little voiced sound";
    case DiscardReason::kTooUnvoiced:
      return "mostly unvoiced - not speech-like";
    case DiscardReason::kTooSteady:
      return "steady, no syllable rhythm";
    default:
      return "unknown reason";
  }
}

/** Seconds from a sample index. */
double secondsAt(size_t sample_index) { return static_cast<double>(sample_index) / kSampleRateHz; }

/** The file's base name without folders or extension, for capture file names. */
std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

/** Runs one file through the detector and prints what the board would do.
    Adds to *sent / *discarded. */
void replayFile(const Options& options, const std::string& path, uint32_t* sent, uint32_t* discarded) {
  std::vector<int16_t> samples;
  if (!loadAudio(path, &samples)) return;
  printf("%s: %.2f s\n", path.c_str(), secondsAt(samples.size()));

  esp32va_vad::VoiceActivityDetector detector;
  detector.setSensitivity(options.sensitivity);
  detector.setHangoverMs(options.hangover_ms);
  detector.restart();
  const size_t pre_roll_samples = esp32va_vad::kPreRollMs * kSampleRateHz / 1000;
  size_t capture_start = 0;
  size_t speech_start = 0;
  size_t armed_at = 0;  // as on the board, the pre-roll never reaches back before the detector (re)started
  uint32_t capture_number = 0;
  if (options.print_frames) {
    printf("time_s,level_dbfs,floor_dbfs,threshold_dbfs,band_share,zcr,voicing,active,voiced,event\n");
  }

  size_t frame_count = samples.size() / kFrameSamples;
  for (size_t frame_index = 0; frame_index < frame_count; frame_index++) {
    size_t frame_start = frame_index * kFrameSamples;
    size_t frame_end = frame_start + kFrameSamples;
    DetectorEvent event = detector.processFrame(samples.data() + frame_start);
    const esp32va_vad::FrameAnalysis& frame = detector.lastFrame();
    if (options.print_frames) {
      printf("%.2f,%.1f,%.1f,%.1f,%.2f,%.3f,%.2f,%d,%d,%d\n", secondsAt(frame_start), frame.speech_band_dbfs,
             frame.noise_floor_dbfs, frame.threshold_dbfs, frame.speech_band_share, frame.zero_crossing_rate,
             frame.voicing, frame.active ? 1 : 0, frame.voiced ? 1 : 0, static_cast<int>(event));
    }
    switch (event) {
      case DetectorEvent::kNone:
        break;
      case DetectorEvent::kSpeechStarted: {
        capture_start = frame_end - armed_at > pre_roll_samples ? frame_end - pre_roll_samples : armed_at;
        speech_start = frame_start - detector.speechStartFramesAgo() * kFrameSamples;
        printf("  %6.2f s  speech started (began %.2f s) - recording from %.2f s\n", secondsAt(frame_end),
               secondsAt(speech_start), secondsAt(capture_start));
        break;
      }
      case DetectorEvent::kSpeechEnded:
      case DetectorEvent::kSpeechDiscarded:
      case DetectorEvent::kMaxLengthReached: {
        // As on the board: everything up to the frame that ended it - the
        // hangover's silence included, since it was already sent while
        // the detector was still waiting to see whether the speech goes on.
        size_t capture_end = frame_end;
        uint32_t speech_frames = detector.utteranceFrames() - detector.framesSinceActivity();
        bool is_sent = event != DetectorEvent::kSpeechDiscarded;
        printf("  %6.2f s  %s: %.2f-%.2f s = %.2f s, %.2f s voiced\n", secondsAt(frame_end),
               is_sent ? "SENT" : "dropped", secondsAt(capture_start), secondsAt(capture_end),
               secondsAt(capture_end - capture_start),
               detector.utteranceVoicedFrames() * esp32va_vad::kFrameMs / 1000.0);
        printf("            %s%s | speech %.2f s, %u%% voiced, %u%% syllable dips\n",
               event == DetectorEvent::kMaxLengthReached ? "hit the length cap" : "",
               event == DetectorEvent::kSpeechEnded ? "ended on silence" : "",
               speech_frames * esp32va_vad::kFrameMs / 1000.0,
               speech_frames > 0 ? 100u * detector.utteranceVoicedFrames() / speech_frames : 0u,
               speech_frames > 0 ? 100u * detector.utteranceDipFrames() / speech_frames : 0u);
        if (!is_sent) {
          printf("            DISCARDED: %s\n", discardReasonText(detector.discardReason()));
        }
        if (!options.captures_folder.empty()) {
          char name[64];
          snprintf(name, sizeof(name), "_%02u_%s.wav", ++capture_number, is_sent ? "sent" : "dropped");
          writeWav(options.captures_folder + "/" + baseName(path) + name, samples, capture_start, capture_end);
        }
        if (is_sent) {
          (*sent)++;
          detector.restart();  // the board stops listening while it answers, then re-arms
          armed_at = frame_end;
        } else {
          (*discarded)++;
        }
        break;
      }
      default:
        printf("  unexpected detector event %d\n", static_cast<int>(event));
        break;
    }
  }
  if (detector.inUtterance()) {
    printf("  (file ended mid-utterance, %.2f s in)\n", detector.utteranceFrames() * esp32va_vad::kFrameMs / 1000.0);
  }
}

}  // namespace esp32va_vad_replay

/** Parses the options, replays every file, prints the totals. */
int main(int argc, char** argv) {
  esp32va_vad_replay::Options options;
  for (int index = 1; index < argc; index++) {
    std::string argument = argv[index];
    if (argument == "--sensitivity" && index + 1 < argc) {
      options.sensitivity = static_cast<uint8_t>(atoi(argv[++index]));
    } else if (argument == "--hangover" && index + 1 < argc) {
      options.hangover_ms = static_cast<uint32_t>(atoi(argv[++index]));
    } else if (argument == "--frames") {
      options.print_frames = true;
    } else if (argument == "--captures" && index + 1 < argc) {
      options.captures_folder = argv[++index];
    } else if (!argument.empty() && argument[0] == '-') {
      fprintf(stderr, "unknown option %s\n", argument.c_str());
      return 2;
    } else {
      options.files.push_back(argument);
    }
  }
  if (options.files.empty()) {
    fprintf(stderr,
            "usage: vad_replay [--sensitivity 1-5] [--hangover <ms>] [--frames] [--captures <folder>] <file>...\n");
    return 2;
  }
  uint32_t sent = 0;
  uint32_t discarded = 0;
  for (const std::string& file : options.files) {
    esp32va_vad_replay::replayFile(options, file, &sent, &discarded);
  }
  printf("sensitivity %u, hangover %u ms: %u sent, %u discarded\n", options.sensitivity, options.hangover_ms, sent,
         discarded);
  return 0;
}
