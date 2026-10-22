// verdicts.ino - each LISTEN's verdict (PASSED / FAILED), this step's own
// hands-free verdict, halting, the capture measurements, and the
// recordings a turn leaves on the SD card.
//
// A tab of the ESP32 sketch: the Arduino IDE appends it after ESP32.ino
// (tabs in alphabetical order) into one translation unit, so it shares
// ESP32.ino's includes, globals and the declarations of every function.

namespace {

/** Reports this step's verdict exactly once: a final heap snapshot, the heap
    summary table, then the PASSED/FAILED banner. Later calls are no-ops, so
    e.g. a reconnect that fails long after a PASS never contradicts it.
    failure_reason is ignored when passed is true. Banners are never gated
    behind a log level - see log_utils.h. */
void reportStepVerdict(bool passed, const char* failure_reason) {
  if (step_verdict_reported) return;
  step_verdict_reported = true;
  esp32va_heap::recordHeapSnapshot("at verdict");
  esp32va_heap::logHeapSummary();
  if (passed) {
    Serial.println("===== HANDS-FREE PASSED =====");
    Serial.println("Keep talking to it - and check by ear that knocks, slams and its own voice never start a LISTEN.");
  } else {
    Serial.printf("===== HANDS-FREE FAILED: %s =====\n", failure_reason);
  }
}

/** Tells the laptop app about a reply recording that just closed, if a file
    was kept (the reply recorder logs the outcome itself). */
void handleFinishedRecording(const esp32va_reply_recorder::FinishedRecording& finished) {
  if (finished.bytes_written > 0) {
    esp32va_sd_transfer::announceSavedRecording(finished.file_name, finished.bytes_written);
  }
}

/** Closes this LISTEN's mic recording and tells the laptop app about it.
    A failed save is logged by the recorder itself (step 27 settled that
    feature; it no longer decides this step's verdict). */
void finishVoiceCommandRecording() {
  esp32va_command_recorder::FinishedVoiceCommand finished;
  if (!esp32va_command_recorder::finishVoiceCommand(&finished)) {
    return;
  }
  esp32va_sd_transfer::announceSavedRecording(finished.file_name, finished.bytes_written);
}

/** The first startup check that didn't end ok, as a short phrase for the
    FAILED banner - or nullptr if they all did. */
const char* firstFailedStartupCheck() {
  using esp32va_status::Stage;
  // The SD card isn't one of them: without it the board runs on, by design.
  if (!esp32va_status::isStageOk(Stage::kMicrophone)) return "the mic check didn't pass - see the ERROR above";
  if (!esp32va_status::isStageOk(Stage::kSpeaker)) return "the speaker setup didn't pass";
  if (!esp32va_status::isStageOk(Stage::kWifi)) return "Wi-Fi wasn't connected";
  if (!esp32va_status::isStageOk(Stage::kTimeSync)) return "the time sync didn't pass";
  if (!esp32va_status::isStageOk(Stage::kOpenAi)) return "the OpenAI session wasn't ready";
  return nullptr;
}

/** Counts a LISTEN that just got its verdict toward this step's: a failed
    startup check fails it at once; otherwise kHandsFreeTurnsToPass
    hands-free LISTENs in a row that passed without losing mic audio pass
    it, if the heap never went below kMinLowestHeapBytes. A manual LISTEN
    neither counts nor breaks the streak, and a LISTEN lost to a dropped
    connection never gets here (see markReplyFailed()). */
void countTowardStepVerdict(bool turn_passed) {
  const char* failed_check = firstFailedStartupCheck();
  if (failed_check != nullptr) {
    reportStepVerdict(false, failed_check);
    return;
  }
  if (!last_turn_was_hands_free) {
    return;
  }
  if (!turn_passed || last_turn_lost_mic_audio) {
    if (hands_free_turns_passed > 0) {
      LOG_INFO("Hands-free streak back to 0 of %u - this LISTEN %s.\n", (unsigned)kHandsFreeTurnsToPass,
               turn_passed ? "lost mic audio" : "failed");
    }
    hands_free_turns_passed = 0;
    return;
  }
  hands_free_turns_passed++;
  LOG_INFO("Hands-free LISTEN %u of %u in a row passed.\n", (unsigned)hands_free_turns_passed,
           (unsigned)kHandsFreeTurnsToPass);
  if (hands_free_turns_passed < kHandsFreeTurnsToPass) {
    return;
  }
  uint32_t lowest_heap_bytes = esp32va_heap::lowestFreeHeapSinceBootBytes();
  if (lowest_heap_bytes < kMinLowestHeapBytes) {
    char reason[96];
    snprintf(reason, sizeof(reason), "free heap fell to %u bytes (needs %u) - see the @@HEAP lines",
             (unsigned)lowest_heap_bytes, (unsigned)kMinLowestHeapBytes);
    reportStepVerdict(false, reason);
    return;
  }
  reportStepVerdict(true, nullptr);
}

/** Tells whoever is at the keyboard how to start the next LISTEN. */
void printNextTurnHint() {
  if (esp32va_hands_free::isHandsFreeOn()) {
    Serial.println("Just talk to continue the conversation (or type LISTEN).");
  } else {
    Serial.println("Type LISTEN to continue the conversation.");
  }
}

/** Marks this LISTEN cycle as having gotten a reply, logging the verdict exactly once per cycle.
    Verdicts are never gated behind a log level - see log_utils.h. */
void markReplyReceived() {
  if (reply_received_this_cycle) return;
  noteVerdictReached();
  Serial.printf("===== LISTEN #%u PASSED =====\n", listen_cycle);
  printNextTurnHint();
  esp32va_status::printProtocolLine("@@RESULT pass %u", listen_cycle);
  esp32va_measure::reportTurn(true);
  countTowardStepVerdict(true);
}

/** Marks this LISTEN cycle as failed, logging the verdict exactly once per cycle.
    Reserved for a genuine problem (protocol error, failed response, timeout) - not
    logged at all if a reply already arrived for this cycle. Verdicts are never
    gated behind a log level - see log_utils.h. */
void markReplyFailed(const char* reason) {
  if (reply_received_this_cycle || !awaiting_reply) return;
  noteVerdictReached();
  function_call_turn_pending = false;  // else @@STATE stays "tool" after the failure
  Serial.printf("===== LISTEN #%u FAILED: %s =====\n", listen_cycle, reason);
  esp32va_status::printProtocolLine("@@RESULT fail %u %s", listen_cycle, reason);
  esp32va_controls::requestStatusSound(esp32va_controls::StatusSound::kFailed);
  esp32va_measure::reportTurn(false);
  if (session_ready) {
    Serial.println(esp32va_hands_free::isHandsFreeOn() ? "Just talk to try again (or type LISTEN)." : "Type LISTEN to try again.");
    countTowardStepVerdict(false);
  } else {
    // Cut off by a dropped connection (Wi-Fi or OpenAI) - a robustness case
    // with its own recovery, not a hands-free failure: it neither counts nor
    // breaks the streak (and the stages it checks aren't ok right now).
    LOG_DEBUG("LISTEN #%u was lost to the connection drop - not counted toward the hands-free verdict.\n",
              listen_cycle);
  }
}

/** Halts the sketch entirely - reserved for the one problem it can't run
    around: the mic task not starting (out of memory at boot). A missing SD
    card, no Wi-Fi or an unreachable OpenAI don't halt it - it runs on
    without recordings, or offline, and keeps retrying. Fails this step's
    verdict too, unless it already
    passed. Never gated behind a log level - see log_utils.h. */
void haltSketch(const char* reason) {
  should_keep_running = false;
  esp32va_mic_capture::stopCapture();
  reportStepVerdict(false, reason);
  Serial.printf("===== HALTED: %s =====\n", reason);
  esp32va_status::reportPipelineState(esp32va_status::PipelineState::kHalted, reason);
}

/** Hands this LISTEN's capture numbers to @@TIMING (measurement_utils.h),
    logs how well the mic task and loop() kept up, and notes whether any mic
    audio was lost. speech_ms and end_of_speech_to_commit_ms are -1 when not
    known (a manual or aborted LISTEN). */
void recordCaptureMeasurements(const esp32va_mic_capture::CaptureStats& stats, long speech_ms,
                               long end_of_speech_to_commit_ms) {
  esp32va_measure::setListenStats(capture.mode, samplesToMs(stats.pre_roll_samples), speech_ms,
                                  end_of_speech_to_commit_ms, samplesToMs(stats.max_backlog_samples),
                                  samplesToMs(stats.dropped_samples), stats.max_read_gap_ms);
  double level_rms = capture.samples_sent > 0 ? sqrt(capture.level_sum_of_squares / capture.samples_sent) : 0.0;
  int32_t negative_peak = -static_cast<int32_t>(capture.level_min);
  int32_t positive_peak = static_cast<int32_t>(capture.level_max);
  int32_t peak_abs_sample = capture.samples_sent > 0 ? max(negative_peak, positive_peak) : 0;
  unsigned long average_chunk_ms = capture.chunks_handled > 0 ? capture.total_chunk_ms / capture.chunks_handled : 0;
  esp32va_measure::setCaptureStats(capture.sent_json_bytes, capture.total_send_ms, peak_abs_sample, level_rms,
                                   capture.clipped_samples, capture.slowest_chunk_ms, average_chunk_ms);
  LOG_DEBUG("Captured %u chunks. Mic level: min=%d max=%d rms=%.1f\n", (unsigned)capture.chunks_sent,
            capture.level_min, capture.level_max, level_rms);

  // The mic task's reads keep up unless one waited longer than the I2S
  // driver's buffer; loop() keeps up unless it fell further behind than
  // the borrowed ring holds.
  uint32_t mic_buffer_ms = esp32va_audio::micBufferCapacityMs();
  LOG_INFO("Mic timing: longest gap between reads %u ms (the driver holds %u ms); sending fell %u ms behind at "
           "most, %u ms dropped; a %u ms chunk took %lu ms at most to handle, %lu ms on average.\n",
           (unsigned)stats.max_read_gap_ms, (unsigned)mic_buffer_ms, (unsigned)samplesToMs(stats.max_backlog_samples),
           (unsigned)samplesToMs(stats.dropped_samples), (unsigned)kChunkMs, capture.slowest_chunk_ms,
           average_chunk_ms);
  LOG_DEBUG("Chunk time split: sending %lu ms, SD %lu ms, everything else %lu ms (%u chunks).\n",
            capture.total_send_ms, capture.total_sd_ms,
            capture.total_chunk_ms - capture.total_send_ms - capture.total_sd_ms, (unsigned)capture.chunks_handled);
  last_turn_lost_mic_audio = stats.dropped_samples > 0 || stats.max_read_gap_ms >= mic_buffer_ms;
  if (last_turn_lost_mic_audio) {
    LOG_ERROR("Mic audio was lost this LISTEN - %u ms dropped from the ring, longest read gap %u ms.\n",
              (unsigned)samplesToMs(stats.dropped_samples), (unsigned)stats.max_read_gap_ms);
  }
  if (stats.unexpected_events > 0) {
    LOG_ERROR("The mic task saw %u unexpected detector events this LISTEN.\n", (unsigned)stats.unexpected_events);
  }
}

/** Lets the recorder do its SD work for this loop() pass, and settles this
    step's verdict once a recording closes. */
void serviceReplyRecording() {
  esp32va_reply_recorder::FinishedRecording finished_recording;
  if (esp32va_reply_recorder::serviceReplyRecorder(&finished_recording)) {
    handleFinishedRecording(finished_recording);
  }
}

}  // namespace
