// listen_turn.ino - a LISTEN turn: starting one (manual or by voice),
// streaming the mic's audio to OpenAI chunk by chunk, finishing, aborting
// or cancelling it, and ASK's typed question.
//
// A tab of the ESP32 sketch: the Arduino IDE appends it after ESP32.ino
// (tabs in alphabetical order) into one translation unit, so it shares
// ESP32.ino's includes, globals and the declarations of every function.

namespace {

/** Milliseconds of audio in a number of 24 kHz samples. */
uint32_t samplesToMs(uint32_t samples) {
  return static_cast<uint32_t>(static_cast<uint64_t>(samples) * 1000 / kSampleRateHz);
}

/** Ends a capture that can't become a turn any more (the connection
    dropped): stops the mic, keeps what was recorded on the SD card, and
    fails the LISTEN. */
void abortCapture(const char* reason) {
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  esp32va_mic_capture::stopCapture();
  recordCaptureMeasurements(stats, -1, -1);
  finishVoiceCommandRecording();
  noteCaptureAborted();
  markReplyFailed(reason);
}

/** Starts a LISTEN turn whose audio the mic task is already capturing:
    manual (LISTEN / Talk, a fixed kManualRecordDurationSec from now) or by
    voice (hands-free, from its pre-roll on). Its audio is sent chunk by
    chunk by serviceCapture(). */
void beginListenTurn(esp32va_measure::ListenMode mode) {
  listen_cycle++;
  esp32va_measure::beginTurn(listen_cycle);
  enterListening();
  capture = {};
  capture.mode = mode;
  capture.level_min = INT16_MAX;
  capture.level_max = INT16_MIN;
  last_turn_was_hands_free = mode == esp32va_measure::ListenMode::kVoice;
  last_turn_lost_mic_audio = false;
  esp32va_command_recorder::beginVoiceCommand();
  if (mode == esp32va_measure::ListenMode::kVoice) {
    esp32va_status::reportPipelineState(esp32va_status::PipelineState::kListening, "vad");
    Serial.printf("LISTEN #%u: heard speech - recording until you stop talking, streaming to OpenAI...\n",
                  listen_cycle);
  } else {
    char listen_detail[kStateDetailBufferSize];
    snprintf(listen_detail, sizeof(listen_detail), "%u", (unsigned)kManualRecordDurationSec);
    esp32va_status::reportPipelineState(esp32va_status::PipelineState::kListening, listen_detail);
    Serial.printf("LISTEN #%u: recording %us and streaming to OpenAI...\n", listen_cycle,
                  (unsigned)kManualRecordDurationSec);
  }
}

/** Handles the mic task confirming an utterance: it becomes a LISTEN turn,
    unless the board stopped being able to take one meanwhile. */
void handleVoiceStarted() {
  if (is_capturing) {
    LOG_ERROR("Speech was detected during another recording - ignored.\n");
    return;
  }
  if (!session_ready || awaiting_reply || response_in_flight) {
    // The mic stops listening on the next loop() pass anyway (hands_free_utils.h).
    LOG_INFO("Heard speech just as the board stopped listening - ignored it.\n");
    esp32va_mic_capture::stopCapture();
    return;
  }
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  beginListenTurn(esp32va_measure::ListenMode::kVoice);
  esp32va_hands_free::noteUtteranceStarted(samplesToMs(stats.pre_roll_samples));
}

/** Handles the mic task deciding the utterance being recorded wasn't
    speech: OpenAI forgets the audio already sent, the recording is deleted,
    and the LISTEN never happened (its number is reused). The mic is already
    listening for speech again. */
void handleVoiceDiscarded() {
  if (!is_capturing || capture.mode != esp32va_measure::ListenMode::kVoice) {
    LOG_ERROR("A discarded utterance arrived without a hands-free recording in progress - ignored.\n");
    return;
  }
  if (capture.chunks_sent > 0) {
    webSocket.sendTXT(kClearMessage);
  }
  esp32va_command_recorder::cancelVoiceCommand();
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  Serial.printf("LISTEN #%u dropped - that didn't sound like speech. Still listening.\n", listen_cycle);
  noteListenDropped();
  esp32va_hands_free::noteUtteranceDiscarded(stats.discard_reason, samplesToMs(stats.captured_samples));
}

/** CANCEL (the app's Cancel button): drops the LISTEN in progress, so no
    reply is heard. While recording: the mic stops, OpenAI forgets the audio
    sent so far and the recording is deleted. Once sent: the response is
    cancelled if it's still coming (its tool calls are skipped) and nothing
    more of it is played. */
void cancelListenTurn() {
  if (is_capturing) {
    esp32va_mic_capture::stopCapture();
    if (capture.chunks_sent > 0) {
      webSocket.sendTXT(kClearMessage);
    }
    esp32va_command_recorder::cancelVoiceCommand();
    is_capturing = false;
  } else if (awaiting_reply) {
    if (response_in_flight) {
      webSocket.sendTXT(kCancelResponseMessage);
      dropping_reply_audio = true;
    }
    esp32va_reply_recorder::noteReplyAborted();  // keeps what was saved; must come before the ring reset
    esp32va_audio::resetRingBuffer();
  } else {
    Serial.printf("CANCEL ignored - no LISTEN to cancel.\n");
    return;
  }
  noteListenCancelled();
  esp32va_hands_free::holdOff();
  Serial.printf("===== LISTEN #%u CANCELLED =====\n", listen_cycle);
  esp32va_status::printProtocolLine("@@RESULT cancel %u", listen_cycle);
  printNextTurnHint();
}

/** While the laptop plays a recording out loud (LAPTOP_PLAYBACK,
    hands_free_utils.h), every loop() pass: a hands-free utterance still
    being recorded is the recording, or mixed with it - it's cancelled like
    CANCEL, before it becomes a question. Also one whose kVoiceStarted
    loop() hasn't taken yet (the mic task confirmed speech just as the
    playback began). A manual LISTEN (Talk, BOOT, LISTEN) is left alone: the
    user asked for that one, and the control center pauses its player for
    it. No new hands-free utterance starts meanwhile. */
void dropHandsFreeRecordingDuringLaptopPlayback() {
  if (is_capturing && capture.mode == esp32va_measure::ListenMode::kVoice) {
    LOG_INFO("The laptop is playing a recording - cancelling the hands-free recording, it would hear it.\n");
    cancelListenTurn();
  } else if (!is_capturing && esp32va_mic_capture::isCaptureInProgress()) {
    // A manual capture sets is_capturing at once (startManualListen()), so
    // this can only be speech the mic task confirmed and loop() hasn't taken.
    LOG_INFO("The laptop is playing a recording - ignored the speech the mic just heard.\n");
    esp32va_mic_capture::stopCapture();
  }
}

/** Takes up to one chunk of captured audio, saves the mic's own samples to
    the SD card, and streams them with the software gain applied as one
    input_audio_buffer.append - its base64 written straight into the JSON
    message - printing the chunk's level as @@MIC_LEVEL. */
void sendCaptureChunk() {
  unsigned long handling_started_ms = millis();
  uint32_t sample_count = esp32va_mic_capture::takeCaptureSamples(mic_chunk, kChunkSamples);
  if (sample_count == 0) {
    return;  // the mic task dropped it meanwhile - counted in its stats
  }

  size_t message_length = sizeof(kAppendMessagePrefix) - 1;
  memcpy(json_message, kAppendMessagePrefix, message_length);
  double chunk_sum_of_squares = 0.0;
  bool encoded = true;
  for (uint32_t block_start = 0; block_start < sample_count; block_start += kGainBlockSamples) {
    uint32_t block_count = min(kGainBlockSamples, sample_count - block_start);
    for (uint32_t i = 0; i < block_count; i++) {
      int32_t scaled = static_cast<int32_t>(mic_chunk[block_start + i]) * kSoftwareGain;
      if (scaled >= INT16_MAX) {
        scaled = INT16_MAX;
        capture.clipped_samples++;
      } else if (scaled <= INT16_MIN) {
        scaled = INT16_MIN;
        capture.clipped_samples++;
      }
      gain_block[i] = static_cast<int16_t>(scaled);
      capture.level_min = min(capture.level_min, gain_block[i]);
      capture.level_max = max(capture.level_max, gain_block[i]);
      chunk_sum_of_squares += static_cast<double>(scaled) * scaled;
    }
    size_t encoded_chars = 0;
    int b64_result = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(json_message + message_length),
                                           sizeof(json_message) - message_length, &encoded_chars,
                                           reinterpret_cast<const unsigned char*>(gain_block),
                                           block_count * sizeof(int16_t));
    if (b64_result != 0) {
      LOG_ERROR("base64 encode failed (code %d) - skipping this chunk.\n", b64_result);
      encoded = false;
      break;
    }
    message_length += encoded_chars;
  }
  capture.level_sum_of_squares += chunk_sum_of_squares;
  double chunk_rms = sqrt(chunk_sum_of_squares / sample_count);
  int chunk_level_dbfs = chunk_rms >= 1.0 ? static_cast<int>(lround(20.0 * log10(chunk_rms / 32768.0))) : -96;
  esp32va_status::printProtocolLine("@@MIC_LEVEL %d", chunk_level_dbfs);

  unsigned long send_ms = 0;
  if (encoded) {
    memcpy(json_message + message_length, kAppendMessageSuffix, sizeof(kAppendMessageSuffix));  // with its '\0'
    message_length += sizeof(kAppendMessageSuffix) - 1;
    unsigned long send_started_ms = millis();
    webSocket.sendTXT(json_message, message_length);
    send_ms = millis() - send_started_ms;
    capture.total_send_ms += send_ms;
    capture.sent_json_bytes += message_length;
    capture.chunks_sent++;
  }
  unsigned long sd_started_ms = millis();
  esp32va_command_recorder::appendMicSamples(mic_chunk, sample_count);
  unsigned long sd_ms = millis() - sd_started_ms;
  capture.total_sd_ms += sd_ms;
  if (send_ms + sd_ms > 2 * kChunkMs) {
    LOG_DEBUG("Slow chunk #%u: send %lu ms, SD %lu ms, %u bytes, RSSI %d, free heap %u.\n",
              (unsigned)capture.chunks_handled + 1, send_ms, sd_ms, (unsigned)message_length, (int)WiFi.RSSI(),
              (unsigned)ESP.getFreeHeap());
  }
  capture.samples_sent += sample_count;
  capture.chunks_handled++;
  unsigned long handling_ms = millis() - handling_started_ms;
  capture.slowest_chunk_ms = max(capture.slowest_chunk_ms, handling_ms);
  capture.total_chunk_ms += handling_ms;
}

/** Ends a capture whose audio has all been sent: gives the ring back to
    playback (the reply needs it), commits the audio and asks for a response,
    records the LISTEN's numbers, closes the SD recording and starts the
    reply timeout. Also says whether any mic audio was lost on the way. */
void finishCapture() {
  esp32va_mic_capture::CaptureStats stats;
  esp32va_mic_capture::getCaptureStats(&stats);
  esp32va_mic_capture::stopCapture();
  webSocket.sendTXT(kCommitMessage);
  webSocket.sendTXT(kResponseCreateMessage);
  unsigned long committed_ms = millis();
  LOG_DEBUG("Commit + response.create sent (%u chunks). Free heap %u, largest block %u.\n",
            (unsigned)capture.chunks_sent, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  response_in_flight = true;
  esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kRecordingDone);

  bool is_hands_free = capture.mode == esp32va_measure::ListenMode::kVoice;
  long speech_ms = -1;
  long end_of_speech_to_commit_ms = -1;
  if (is_hands_free) {
    speech_ms = static_cast<long>(samplesToMs(stats.speech_samples));
    if (stats.speech_end_ms != 0) {
      end_of_speech_to_commit_ms = static_cast<long>(committed_ms - stats.speech_end_ms);
    }
    esp32va_hands_free::noteUtteranceSent(static_cast<uint32_t>(speech_ms),
                                          stats.end_event == esp32va_vad::DetectorEvent::kMaxLengthReached);
  }
  Serial.printf("Done streaming %.1f s of audio - waiting for reply...\n", samplesToMs(capture.samples_sent) / 1000.0f);
  recordCaptureMeasurements(stats, speech_ms, end_of_speech_to_commit_ms);
  finishVoiceCommandRecording();

  enterAwaitingReply();
}

/** Moves the capture along, once per loop() pass: acts on the mic task's
    events (an utterance confirmed, a capture complete or discarded), sends
    at most one ~100 ms chunk of what's been captured - a partial one only
    once the capture is complete - and finishes the capture once it's all
    sent. */
void serviceCapture() {
  using esp32va_mic_capture::CaptureEvent;
  CaptureEvent event = esp32va_mic_capture::takeCaptureEvent();
  switch (event) {
    case CaptureEvent::kNone:
      break;
    case CaptureEvent::kVoiceStarted:
      handleVoiceStarted();
      break;
    case CaptureEvent::kCaptureEnded:
      LOG_DEBUG("The mic task says the recording is complete - sending the rest.\n");
      break;
    case CaptureEvent::kCaptureDiscarded:
      handleVoiceDiscarded();
      break;
    default:
      LOG_ERROR("Unexpected capture event %d - ignored.\n", (int)event);
      break;
  }
  // The mic task drops a non-speech utterance by going back to kArmed and
  // queueing kCaptureDiscarded. When that lands after the takeCaptureEvent()
  // above but before serviceHandsFree() in the same loop() pass (the mic task
  // runs at a higher priority on this core), serviceHandsFree() sees kArmed
  // while is_capturing still holds and stops the mic - and stopCapture()
  // clears the queued event, so the recording never ended (found on the
  // board: clapping during hands-free left it on "Listening" for good).
  if (is_capturing && capture.mode == esp32va_measure::ListenMode::kVoice) {
    esp32va_mic_capture::CaptureMode mic_mode = esp32va_mic_capture::captureMode();
    if (mic_mode == esp32va_mic_capture::CaptureMode::kArmed || mic_mode == esp32va_mic_capture::CaptureMode::kOff) {
      LOG_INFO("The mic stopped this hands-free recording without saying why - treating it as discarded.\n");
      handleVoiceDiscarded();
      return;
    }
  }
  if (!is_capturing) {
    return;
  }
  uint32_t samples_ready = esp32va_mic_capture::captureSamplesReady();
  bool capture_complete = esp32va_mic_capture::captureMode() == esp32va_mic_capture::CaptureMode::kDraining;
  static unsigned long last_progress_log_ms = 0;  // diagnostic: where a long capture stands, every 2 s
  if (millis() - last_progress_log_ms >= 2000) {
    last_progress_log_ms = millis();
    LOG_DEBUG("Capture in progress: mic mode %d, %u ms waiting to be sent, %u chunks sent so far.\n",
              (int)esp32va_mic_capture::captureMode(), (unsigned)samplesToMs(samples_ready),
              (unsigned)capture.chunks_sent);
  }
  if (samples_ready >= kChunkSamples || (capture_complete && samples_ready > 0)) {
    sendCaptureChunk();
  }
  if (esp32va_mic_capture::isCaptureDrained()) {
    finishCapture();
  }
}

/** The short high tick right before a manual recording starts. Blocks
    ~0.25 s: the tone, then until it has left the speaker, so the recording
    that starts next doesn't hear it. Hands-free is held off around it, as
    for BEEP, so the mic doesn't take it for speech. loop() only (i2s_write). */
void playStartTick() {
  esp32va_hands_free::holdOff();
  esp32va_audio::playTone(esp32va_params::kStartToneHz, esp32va_params::kStartToneMs);
  esp32va_audio::waitUntilPlayedOut();
  esp32va_hands_free::holdOff();
}

/** Starts a manual LISTEN (the command, the app's Talk button or the BOOT
    button) if the board can take one now, with the start tick just before
    the recording; otherwise says why not. Works with hands-free on too - a
    fixed kManualRecordDurationSec, as before. Returns true if it started,
    false if it was refused. */
bool startManualListen() {
  if (!should_keep_running) {
    Serial.printf("LISTEN ignored - the sketch has halted (see the HALTED line above).\n");
    return false;
  }
  if (!session_ready && isAssistantOffline()) {
    Serial.printf("LISTEN ignored - offline (no Wi-Fi or no OpenAI). The timer and the controls still work.\n");
    return false;
  }
  if (!session_ready) {
    Serial.printf("LISTEN ignored - not ready yet (still connecting/setting up).\n");
    return false;
  }
  if (is_capturing) {
    Serial.printf("LISTEN ignored - already recording.\n");
    return false;
  }
  if (awaiting_reply) {
    Serial.printf("LISTEN ignored - still waiting for or playing back a reply.\n");
    return false;
  }
  if (response_in_flight || speech_in_progress) {
    // The greeting, a timer announcement or a typed question's answer is on
    // its way: its audio would arrive while the mic holds the ring (and be
    // dropped), and a second response.create would be rejected.
    LOG_INFO("LISTEN ignored - the assistant is still answering, try again when it is Ready.\n");
    return false;
  }
  if (!esp32va_reply_recorder::isRecorderIdle()) {
    Serial.printf("LISTEN ignored - still saving the last reply to the SD card, try again in a moment.\n");
    return false;
  }
  // What startManualCapture() would refuse, checked before the tick too, so
  // a LISTEN is never ticked and then refused.
  if (esp32va_mic_capture::isCaptureInProgress() || esp32va_audio::ringUsedBytes() > 0) {
    Serial.printf("LISTEN ignored - the mic is busy (speech is being recorded, or audio still playing).\n");
    return false;
  }
  playStartTick();
  if (!esp32va_mic_capture::startManualCapture(kSampleRateHz * kManualRecordDurationSec)) {
    Serial.printf("LISTEN ignored - the mic is busy (speech is being recorded, or audio still playing).\n");
    return false;
  }
  beginListenTurn(esp32va_measure::ListenMode::kManual);
  return true;
}

/** ASK <text>: a question typed in the laptop app instead of spoken - for a
    noisy room, or a mic that won't cooperate. It goes into the same
    conversation as a text message, and the reply is spoken like the
    greeting: no LISTEN, so no recording and no @@TIMING. The text comes
    back as @@HEARD for the app's chat bubble. A question longer than
    kMaxTypedQuestionBytes bytes (UTF-8) is cut there, never mid-character. */
void askTypedQuestion(const String& text) {
  if (!session_ready) {
    Serial.printf("ASK ignored - not connected to OpenAI yet.\n");
    return;
  }
  if (!isIdleForFileTransfer() || response_in_flight || speech_in_progress) {
    Serial.printf("ASK ignored - the assistant is busy, try again when it is Ready.\n");
    return;
  }
  unsigned int cut = min(text.length(), kMaxTypedQuestionBytes);
  while (cut > 0 && cut < text.length() && (static_cast<uint8_t>(text[cut]) & 0xC0) == 0x80) {
    cut--;  // don't split a multi-byte (e.g. Hebrew) character - the server rejects broken UTF-8
  }
  if (cut < text.length()) {
    LOG_INFO("Typed question is %u bytes - only the first %u bytes are sent (the limit is %u bytes).\n",
             text.length(), cut, kMaxTypedQuestionBytes);
  }
  String question = text.substring(0, cut);
  JsonDocument doc;
  doc["type"] = "conversation.item.create";
  JsonObject item = doc["item"].to<JsonObject>();
  item["type"] = "message";
  item["role"] = "user";
  JsonObject part = item["content"].to<JsonArray>().add<JsonObject>();
  part["type"] = "input_text";
  part["text"] = question;  // ArduinoJson escapes it
  String message;
  serializeJson(doc, message);
  webSocket.sendTXT(message);
  webSocket.sendTXT(kResponseCreateMessage);
  esp32va_status::reportTextLine("HEARD", question.c_str());
  noteUnpromptedResponseRequested("typed");  // like the greeting: hands-free waits for it to be heard
  LOG_INFO("Typed question sent: \"%s\"\n", question.c_str());
}

}  // namespace
