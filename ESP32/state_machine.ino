// state_machine.ino - the assistant's state machine: the transitions (named
// helpers for the flags they change), the one place the state is derived
// and reported (updatePipelineState(), @@STATE), and the checks that move it
// on: reply timeout, playback done, offline, Wi-Fi loss, the OpenAI
// connection, the @@ALIVE heartbeat.
//
// A tab of the ESP32 sketch: the Arduino IDE appends it after ESP32.ino
// (tabs in alphabetical order) into one translation unit, so it shares
// ESP32.ino's includes, globals and the declarations of every function.

namespace {

// ===== State machine: the transitions =====
// The assistant's state is derived in one place, updatePipelineState()
// below, from the flags in ESP32.ino. These are the transitions that change
// several of them at once - each one the same statements, in the same order,
// its call sites used to write inline. Flag writes only: the calls, sends
// and logs around them stay with their callers.

/** Idle -> listening: a LISTEN's recording starts (beginListenTurn()). */
void enterListening() {
  is_capturing = true;
  awaiting_reply = false;
  reply_received_this_cycle = false;
  response_fully_received = false;
  function_call_turn_pending = false;
  dropping_reply_audio = false;
  speech_in_progress = false;
}

/** Listening -> processing: the recording is all sent and committed, the
    reply timeout starts (finishCapture()). */
void enterAwaitingReply() {
  is_capturing = false;
  awaiting_reply = true;
  last_reply_activity_ms = millis();
}

/** Listening -> failed: the connection dropped while recording
    (abortCapture()), just before markReplyFailed(). */
void noteCaptureAborted() {
  is_capturing = false;
  awaiting_reply = true;  // so markReplyFailed() reports this LISTEN's verdict
}

/** Listening (hands-free) -> idle: the utterance wasn't speech, so the
    LISTEN never happened and its number is reused (handleVoiceDiscarded()). */
void noteListenDropped() {
  is_capturing = false;
  listen_cycle--;
}

/** Listening / processing / speaking -> idle: CANCEL dropped the LISTEN
    (cancelListenTurn()). */
void noteListenCancelled() {
  awaiting_reply = false;
  reply_received_this_cycle = true;
  function_call_turn_pending = false;
  speech_in_progress = false;
}

/** Processing / speaking -> idle: this LISTEN got its verdict, PASSED or
    FAILED (markReplyReceived(), markReplyFailed()). */
void noteVerdictReached() {
  reply_received_this_cycle = true;
  awaiting_reply = false;
}

/** A response the board asked for on its own (the opening greeting) is on
    its way: hands-free waits for it, the reply timeout watches it. */
void noteResponseRequested() {
  response_in_flight = true;
  last_reply_activity_ms = millis();
}

/** Like noteResponseRequested(), for speech no LISTEN asked for that
    @@STATE names: "timer" (a timer announcement) or "typed" (ASK). */
void noteUnpromptedResponseRequested(const char* speech_detail) {
  unprompted_speech_detail = speech_detail;
  noteResponseRequested();
}

/** The server's response.created: a response is running. */
void noteResponseCreated() {
  server_response_running = true;
  response_in_flight = true;
  last_reply_activity_ms = millis();
}

/** response.done for a response CANCEL stopped: it is over, nothing more
    of it is dropped. */
void noteCancelledResponseOver() {
  dropping_reply_audio = false;
  response_in_flight = false;
  function_call_turn_pending = false;
}

/** response.done with status "failed", just before markReplyFailed(). */
void noteFailedResponseDone() {
  response_in_flight = false;
  function_call_turn_pending = false;
}

/** The "error" response_cancel_not_active after a CANCEL: the response had
    already ended. */
void noteCancelledResponseAlreadyOver() {
  dropping_reply_audio = false;  // the response had already ended - nothing left to drop
  response_in_flight = false;
}

/** An "error" while no response is running (e.g. a rejected
    response.create): no response.done will come to clear these. */
void noteResponseRejected() {
  response_in_flight = false;
  dropping_reply_audio = false;
}

/** No response is coming any more (the socket closed, or the server went
    silent - checkReplyTimeout()). */
void clearResponseFlags() {
  response_in_flight = false;
  server_response_running = false;
  function_call_turn_pending = false;
  dropping_reply_audio = false;
}

/** Ready -> reconnecting: the WebSocket disconnected (onWebSocketEvent()). */
void noteSessionLost() {
  session_ready = false;
  clearResponseFlags();
}

// ===== State machine: the state, derived and checked =====

/** True when a LIST/GET may run now: no turn in progress, nothing left to
    play, nothing left to save. A GET holds loop() for its whole transfer, so
    it must never overlap playback or recording. */
bool isIdleForFileTransfer() {
  return !is_capturing && !esp32va_mic_capture::isCaptureInProgress() && !awaiting_reply && esp32va_audio::ringUsedBytes() == 0 &&
         esp32va_reply_recorder::isRecorderIdle();
}

/** True when hands-free listening may run: the session is ready, the
    greeting has been asked for, and nothing is being recorded, sent, played
    or saved, with no response still on its way. */
bool boardCanListen() {
  return should_keep_running && session_ready && !should_send_opening_greeting && !is_capturing &&
         !awaiting_reply && !response_in_flight && !speech_in_progress && esp32va_audio::ringUsedBytes() == 0 &&
         esp32va_reply_recorder::isRecorderIdle();
}

/** Checks whether the current LISTEN cycle has gone quiet for too long - an
    INACTIVITY timeout (no server message at all for kReplyTimeoutMs), not a
    cap on the reply's total duration. A tool-using cycle takes two full
    response round trips and a long spoken reply is paced by playback
    backpressure, both of which can legitimately take well past
    kReplyTimeoutMs from the start of the cycle - what should never happen is
    total silence from the server for that long. (A total-duration cap fired
    while transcript/audio was still actively arriving in a real run - see
    Documentation/decisions.md step 16's "reply timeout fired mid-reply" discovery.)
    The same silence ends a response nobody waits on (the greeting), so
    hands-free listening isn't held off forever by one that never finished.
    Either way the connection is closed too: a server that silent may be
    behind a half-open socket (Wi-Fi up, internet gone), and the usual
    disconnect path reconnects - or shows offline - instead of failing every
    LISTEN on a dead session. */
void checkReplyTimeout() {
  if (!awaiting_reply && !response_in_flight) {
    return;
  }
  if (millis() - last_reply_activity_ms < kReplyTimeoutMs) {
    return;
  }
  clearResponseFlags();  // a response that silent isn't coming any more
  if (!awaiting_reply) {
    LOG_ERROR("No word from the server for %lu ms about the last response - not waiting for it any more.\n",
              kReplyTimeoutMs);
  } else {
    LOG_ERROR("Reply timeout: connected %d, session ready %d, free heap %u, largest block %u.\n",
              (int)webSocket.isConnected(), (int)session_ready, (unsigned)ESP.getFreeHeap(),
              (unsigned)ESP.getMaxAllocHeap());
    char reason[64];
    snprintf(reason, sizeof(reason), "no server activity for %lu ms", kReplyTimeoutMs);
    markReplyFailed(reason);
  }
  if (webSocket.isConnected()) {
    LOG_INFO("Closing the silent OpenAI connection - a fresh session will be set up.\n");
    webSocket.disconnect();
  }
}

/** Once the server has said the (post-tool-call, if any) response is fully done
    AND the ring buffer has finished draining to the speaker, the reply has
    genuinely been heard in full - that's this step's actual PASS criterion,
    not just "bytes arrived". */
void checkPlaybackComplete() {
  if (!response_fully_received || !awaiting_reply) {
    return;
  }
  if (esp32va_audio::ringUsedBytes() > 0) {
    return;  // still playing
  }
  esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kPlaybackDone);
  markReplyReceived();
}

/** True when the assistant can't answer for now and isn't about to: OpenAI
    failed attempts-before-offline times in a row, or the session has been
    down longer than the offline retry interval (e.g. Wi-Fi up but no
    internet, where connection attempts fail before any disconnect event).
    Never while setup() is still running. */
bool isAssistantOffline() {
  if (session_ready || !setup_finished) {
    return false;
  }
  bool down_too_long = millis() - session_not_ready_since_ms > esp32va_openai::offlineRetryIntervalMs();
  if (!openai_connection_started) {
    return down_too_long && !esp32va_wifi::isTimeSynced();  // Wi-Fi is up but the clock never came
  }
  return esp32va_openai::isOffline() || down_too_long;
}

/** When Wi-Fi goes down (or the network is switched), the open socket to
    OpenAI is dead, but the library only notices on its next failed send.
    Closing it right away runs the usual disconnect path: a LISTEN in
    progress fails, the state shows reconnecting / offline, and the
    connection is retried once Wi-Fi is back. When Wi-Fi comes back (or a
    new network connects), the reconnect backoff starts over, so OpenAI is
    retried at once instead of after the offline pace's ~30 s. */
void dropDeadSocketOnWifiLoss() {
  bool wifi_up = esp32va_wifi::isWifiConnected();
  if (wifi_was_up && !wifi_up && webSocket.isConnected()) {
    LOG_INFO("Wi-Fi is down - closing the OpenAI connection, it can't be alive.\n");
    webSocket.disconnect();
  }
  if (!wifi_was_up && wifi_up && openai_connection_started && !webSocket.isConnected()) {
    LOG_INFO("Wi-Fi is back - reconnecting to OpenAI now.\n");
    esp32va_openai::resetReconnectBackoff(webSocket);
    esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive,
                                "Wi-Fi is back - reconnecting");
    wifi_return_fast_retry = true;
    wifi_returned_ms = millis();
  }
  wifi_was_up = wifi_up;
}

/** A new API key was saved from the control center (or the saved one
    forgotten - OPENAI_KEY, openai_realtime_utils.h). The open connection,
    or an attempt still in its handshake, uses the old key, so it's closed:
    the usual disconnect path - a LISTEN in progress fails, the state shows
    reconnecting - as for a new Wi-Fi network. The next attempt, with the
    new key, is made at once rather than at the offline pace, with the same
    short fast-retry window as when Wi-Fi comes back. Before the first
    connection, or with Wi-Fi down, there's nothing to close: the next
    connection (dropDeadSocketOnWifiLoss() retries at once when Wi-Fi is
    back) starts with the new key. */
void reconnectWithNewApiKey() {
  if (!openai_connection_started || !esp32va_wifi::isWifiConnected()) {
    Serial.printf("The new API key is used once Wi-Fi and the clock are up.\n");
    return;
  }
  webSocket.disconnect();  // a no-op when nothing is open (waiting to retry)
  esp32va_openai::resetReconnectBackoff(webSocket);  // after the disconnect, which counted an attempt
  Serial.printf("Connecting to OpenAI with the new API key...\n");
  esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive,
                              "new API key - connecting");
  wifi_return_fast_retry = true;
  wifi_returned_ms = millis();
}

/** Works out what the assistant is doing from this sketch's flags and
    reports it as @@STATE when it changes (status_report_utils.h). Called
    every loop() pass. */
void updatePipelineState() {
  using esp32va_status::PipelineState;
  if (!should_keep_running) {
    return;  // haltSketch() already reported halted
  }
  bool offline_now = !session_ready && setup_finished &&
                     (!esp32va_wifi::isWifiConnected() || isAssistantOffline());
  if (offline_now && !offline_announced) {
    offline_announced = true;
    esp32va_controls::requestStatusSound(esp32va_controls::StatusSound::kOffline);
    if (openai_connection_started) {
      // A failed connect raises no disconnect event, so the backoff in
      // registerDisconnect() may never have reached the offline pace - set
      // it here, so a dead network isn't retried (and loop() held up by a
      // connect attempt) every second.
      webSocket.setReconnectInterval(esp32va_openai::addJitter(esp32va_openai::offlineRetryIntervalMs()));
      // For the same reason the OpenAI check may still say "connecting" -
      // with Wi-Fi up but no internet it must turn red, like any offline.
      reportOpenAiStageOffline();
    }
  } else if (offline_now && wifi_return_fast_retry && esp32va_wifi::isWifiConnected() &&
             millis() - wifi_returned_ms >= kWifiReturnFastRetryMs) {
    // Wi-Fi is back but OpenAI still isn't (e.g. no internet): back to the
    // offline pace, for the same reason as above.
    wifi_return_fast_retry = false;
    LOG_DEBUG("OpenAI still unreachable %lu s after Wi-Fi came back - retrying every %lu s again.\n",
              kWifiReturnFastRetryMs / 1000, esp32va_openai::offlineRetryIntervalMs() / 1000);
    webSocket.setReconnectInterval(esp32va_openai::addJitter(esp32va_openai::offlineRetryIntervalMs()));
    reportOpenAiStageOffline();
  } else if (session_ready && offline_announced) {
    offline_announced = false;
    esp32va_controls::requestStatusSound(esp32va_controls::StatusSound::kOnline);
  }
  if (session_ready) {
    wifi_return_fast_retry = false;
  }
  if (!session_ready) {
    if (setup_finished && !esp32va_wifi::isWifiConnected()) {
      esp32va_status::reportPipelineState(PipelineState::kOffline, "no Wi-Fi");
      return;
    }
    if (isAssistantOffline()) {
      const char* offline_detail = !esp32va_wifi::isTimeSynced()                    ? "no clock - the network may block NTP"
                                   : esp32va_openai::lastKeyRefusalHttpCode() != 0 ? "OpenAI refused the API key"
                                                                                   : "OpenAI unreachable";
      esp32va_status::reportPipelineState(PipelineState::kOffline, offline_detail);
      return;
    }
    if (!session_ever_ready) {
      esp32va_status::reportPipelineState(PipelineState::kBooting, nullptr);
      return;
    }
    char reconnect_detail[kStateDetailBufferSize];
    snprintf(reconnect_detail, sizeof(reconnect_detail), "attempt %u of %u",
             (unsigned)esp32va_openai::reconnectAttemptCount(), (unsigned)esp32va_openai::attemptsBeforeOffline());
    esp32va_status::reportPipelineState(PipelineState::kReconnecting, reconnect_detail);
    return;
  }
  // Speech is over once nothing is left to play and either the server said
  // the response is done or no LISTEN is waiting on it (the opening greeting,
  // or a cycle that already failed).
  if (speech_in_progress && esp32va_audio::ringUsedBytes() == 0 && (response_fully_received || !awaiting_reply)) {
    speech_in_progress = false;
  }
  if (function_call_turn_pending) {
    esp32va_status::reportPipelineState(PipelineState::kTool, active_tool_name);
  } else if (speech_in_progress) {
    esp32va_status::reportPipelineState(PipelineState::kSpeaking, awaiting_reply ? nullptr : unprompted_speech_detail);
  } else if (is_capturing) {
    if (capture.mode == esp32va_measure::ListenMode::kVoice) {
      esp32va_status::reportPipelineState(PipelineState::kListening, "vad");
    } else {
      char listen_detail[kStateDetailBufferSize];
      snprintf(listen_detail, sizeof(listen_detail), "%u", (unsigned)kManualRecordDurationSec);
      esp32va_status::reportPipelineState(PipelineState::kListening, listen_detail);
    }
  } else if (awaiting_reply || (response_in_flight && dropping_reply_audio)) {
    esp32va_status::reportPipelineState(PipelineState::kProcessing, nullptr);
  } else if (response_in_flight) {
    // The greeting, a timer announcement or a typed question's answer was
    // asked for and its audio hasn't started yet - not Ready (a LISTEN would
    // be refused, see startManualListen()).
    esp32va_status::reportPipelineState(PipelineState::kSpeaking, unprompted_speech_detail);
  } else if (!esp32va_reply_recorder::isRecorderIdle()) {
    esp32va_status::reportPipelineState(PipelineState::kSaving, nullptr);
  } else if (esp32va_hands_free::isWaitingForSpeech()) {
    esp32va_status::reportPipelineState(PipelineState::kArmed, nullptr);
  } else {
    esp32va_status::reportPipelineState(PipelineState::kReady, nullptr);
  }
}

/** Prints "@@ALIVE <seconds since boot>" every kAliveIntervalMs (the first
    on loop()'s first pass), so the laptop app can tell a running board from
    a hung one. Only from loop(): setup()'s waits and a GET's transfer hold
    loop(), and the app allows for those. */
void reportAliveHeartbeat() {
  static uint32_t last_alive_ms = 0;
  static bool alive_reported = false;
  uint32_t now_ms = millis();
  if (alive_reported && now_ms - last_alive_ms < esp32va_params::kAliveIntervalMs) {
    return;
  }
  alive_reported = true;
  last_alive_ms = now_ms;
  esp32va_status::printProtocolLine("@@ALIVE %lu", (unsigned long)(now_ms / 1000));
}

/** Opens the OpenAI connection the first time Wi-Fi is connected and the
    clock is set (TLS needs both) - from setup() when they came up in time,
    otherwise from loop() whenever they do. The WebSocket library reconnects
    by itself after that. */
void startOpenAiConnectionWhenNetworkReady() {
  if (openai_connection_started || !esp32va_wifi::isWifiConnected() || !esp32va_wifi::isTimeSynced()) {
    return;
  }
  openai_connection_started = true;
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kWifiConnected);  // no-ops when setup() marked them
  esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kTimeSynced);
  Serial.printf("Connecting to OpenAI Realtime API...\n");
  esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive, "connecting");
  esp32va_openai::beginOpenAiRealtimeConnection(webSocket);
}

}  // namespace
