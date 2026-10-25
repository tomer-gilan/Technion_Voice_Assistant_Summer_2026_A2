// server_events.ino - the OpenAI session: the WebSocket's events and every
// server message (session ready, reply audio and text, tool calls and their
// dispatch, response done, errors).
//
// A tab of the ESP32 sketch: the Arduino IDE appends it after ESP32.ino
// (tabs in alphabetical order) into one translation unit, so it shares
// ESP32.ino's includes, globals and the declarations of every function.

namespace {

/** Parses one server text frame, logs its "type" and any interesting fields, and
    drives session readiness plus the current LISTEN cycle's PASS/FAIL verdict. */
void handleServerMessage(uint8_t* payload, size_t length) {
  // Any server message at all, while a response is due, proves the
  // connection/response is still alive - resets the inactivity clock so a
  // long reply (paced by playback backpressure) doesn't trip
  // checkReplyTimeout() just for taking a while. See Documentation/decisions.md
  // step 16's "reply timeout fired mid-reply" discovery.
  if (awaiting_reply || response_in_flight) {
    last_reply_activity_ms = millis();
  }
  if (awaiting_reply) {
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstServerEvent);
  }

  char type_buf[48];
  bool have_type = esp32va_json::extractEventType(payload, length, type_buf, sizeof(type_buf));

  if (have_type && strcmp(type_buf, "response.output_audio.delta") == 0) {
    const uint8_t* value_start = nullptr;
    size_t value_len = 0;
    if (!esp32va_json::findRawStringField(payload, length, "delta", &value_start, &value_len)) {
      LOG_ERROR("Audio delta missing 'delta' field.\n");
      return;
    }
    if (esp32va_audio::isRingLentToMic()) {
      // Half-duplex says this can't happen: the ring is only lent while no
      // response is coming. Playing it would mean overwriting the mic.
      LOG_ERROR("Reply audio arrived while the mic was listening - dropped it.\n");
      return;
    }
    if (dropping_reply_audio) {
      return;  // a cancelled reply - not played
    }
    esp32va_reply_recorder::noteReplyAudioArriving();  // before the ring write, so the tap catches it
    speech_in_progress = true;
    if (awaiting_reply) {
      esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstAudio);
      esp32va_measure::addReplyAudio(value_len);
    }
    esp32va_audio::decodeAndRingAudioDelta(value_start, value_len);
    return;
  }

  if (have_type && strncmp(type_buf, "response.output_audio.", 22) == 0) {
    // Any other response.output_audio.* event (e.g. .done) carries no large
    // payload, and nothing here needs it - skips the full JSON parse. (22 =
    // the prefix's length with the dot, so the transcript events, which
    // continue with "_transcript", don't match.)
    LOG_DEBUG("Event type: %s (%u bytes).\n", type_buf, (unsigned)length);
    return;
  }

  // session.created / session.updated echo the whole session config back (all
  // the tools and the instructions, ~6KB). Nothing in them is read, so they
  // skip the full JSON parse - its JsonDocument would need several KB more
  // heap, and a NoMemory there left the OpenAI stage stuck on "connecting".
  bool is_session_event = have_type && (strcmp(type_buf, "session.created") == 0 ||
                                        strcmp(type_buf, "session.updated") == 0);
  JsonDocument doc;
  if (!is_session_event) {
    DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(payload), length);
    if (err) {
      LOG_ERROR("JSON parse error: %s (%u bytes, type %s, free heap %u, largest block %u)\n", err.c_str(),
                (unsigned)length, have_type ? type_buf : "?", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
      markReplyFailed("received a message that isn't valid JSON");
      return;
    }
  }

  const char* type = is_session_event ? type_buf : (doc["type"] | "(missing type)");

  if (strcmp(type, "session.created") == 0) {
    LOG_DEBUG("Event type: session.created (server-initiated, before our own update).\n");
    return;
  }

  if (strcmp(type, "session.updated") == 0) {
    session_ready = true;
    if (!session_ever_ready) {
      session_ever_ready = true;
      // The TLS handshake has already peaked and released its temporary
      // buffers by now, so the low-water mark is what it cost at its worst.
      esp32va_heap::recordHeapSnapshot("OpenAI session ready");
      LOG_INFO("Connected with the SD card mounted - lowest free heap since boot: %u bytes.\n",
               (unsigned)esp32va_heap::lowestFreeHeapSinceBootBytes());
      if (esp32va_hands_free::isHandsFreeOn()) {
        Serial.printf("Ready - just start talking once the greeting ends (or type LISTEN).\n");
      } else {
        Serial.printf("Ready - type LISTEN to talk, or VAD on for hands-free.\n");
      }
      esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kOk, "session ready");
      LOG_DEBUG("session.updated received (first connect).\n");
      // Deferred to loop() rather than sent here - same reasoning as
      // should_send_session_update below (sendTXT() from inside this
      // callback can deadlock the library).
      should_send_opening_greeting = true;
      esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kSessionReady);
      esp32va_measure::reportBootTiming();
      // A later disconnect gets its own fresh attempt budget, rather than
      // one shared with however many retries startup itself needed.
      esp32va_openai::resetReconnectBackoff(webSocket);
    } else {
      // Every successful reconnect starts the count again - otherwise a few
      // unrelated disconnects over a long session (e.g. the server's own
      // session timeout) would add up to "offline".
      bool was_offline = esp32va_openai::isOffline();
      esp32va_openai::resetReconnectBackoff(webSocket);
      if (was_offline) {
        Serial.printf("Back online - OpenAI is reachable again.\n");
      }
      LOG_INFO("Reconnected (#%u) - conversation context lost. Resuming at LISTEN #%u.\n",
               connection_count - 1, listen_cycle + 1);
      esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kOk,
                                  "reconnected - the conversation started fresh");
      LOG_DEBUG("session.updated received (reconnect #%u) - OpenAI's Realtime API has "
                "no session-resumption mechanism, so the model has "
                "no memory of anything before this reconnect.\n",
                connection_count - 1);
    }
    return;
  }

  if (strcmp(type, "response.function_call_arguments.delta") == 0) {
    // Arguments arrive complete in .done below - nothing to act on per-delta,
    // and logging each chunk would just be noise.
    return;
  }

  if (strcmp(type, "response.function_call_arguments.done") == 0) {
    const char* function_name = doc["name"] | "(unknown)";
    if (dropping_reply_audio) {
      LOG_INFO("Skipped %s - the LISTEN was cancelled.\n", function_name);
      return;
    }
    LOG_DEBUG("Event type: response.function_call_arguments.done -> %s(%s)\n",
              function_name, (const char*)(doc["arguments"] | "{}"));
    bool handled = false;
    snprintf(active_tool_name, sizeof(active_tool_name), "%s", function_name);
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kToolCall);
    esp32va_measure::setToolName(function_name);
    if (strcmp(function_name, "set_board_state") == 0) {
      handled = esp32va_board::handleSetBoardStateCall(doc, webSocket);
    } else if (strcmp(function_name, "get_time_of_day") == 0) {
      handled = esp32va_light_sensor::handleGetTimeOfDayCall(doc, webSocket);
    } else if (strcmp(function_name, "set_volume") == 0) {
      handled = esp32va_controls::handleSetVolumeCall(doc, webSocket);
    } else if (strcmp(function_name, "get_current_time") == 0) {
      handled = esp32va_clock::handleGetCurrentTimeCall(doc, webSocket);
    } else if (strcmp(function_name, "set_timer") == 0) {
      handled = esp32va_clock::handleSetTimerCall(doc, webSocket);
    } else if (strcmp(function_name, "get_board_status") == 0) {
      handled = esp32va_info::handleGetBoardStatusCall(doc, webSocket);
    } else if (strcmp(function_name, "set_hands_free") == 0) {
      // Only in answer to the user - a LISTEN, or a typed question (ASK) -
      // never during the greeting or a timer announcement, where nobody
      // asked for anything.
      bool user_request_in_progress =
          awaiting_reply || (response_in_flight && strcmp(unprompted_speech_detail, "typed") == 0);
      handled = esp32va_hands_free::handleSetHandsFreeCall(doc, webSocket, user_request_in_progress);
    } else {
      LOG_ERROR("Unhandled function call: %s\n", function_name);
    }
    if (handled) {
      function_call_turn_pending = true;
    }
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.delta") == 0) {
    // Raw streamed content (the model's own spoken-reply transcript) - always
    // printed, never gated behind a log level. See log_utils.h.
    const char* delta = doc["delta"] | "";
    if (dropping_reply_audio) {
      return;  // a cancelled reply's text isn't shown either
    }
    if (awaiting_reply) {
      esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kFirstTranscript);
    }
    if (!transcript_in_progress) {
      transcript_in_progress = true;
      Serial.print("Streaming transcript: ");
    }
    Serial.print(delta);
    // After every piece, not just the first: an @@ line printed in between
    // ends the line, and the next @@ line must find it open again - or it's
    // glued to the text, where the app doesn't see it.
    esp32va_status::setRawLineOpen(true);
    return;
  }

  if (strcmp(type, "response.output_audio_transcript.done") == 0) {
    if (dropping_reply_audio) {
      return;
    }
    if (transcript_in_progress) {
      esp32va_status::endRawLine();
      transcript_in_progress = false;
    }
    // Not logged at LOG_INFO: this is a full re-print of content already
    // shown live via the streamed deltas above (raw, ungated) - repeating it
    // here would just be noise. Kept at LOG_DEBUG for cross-checking the
    // assembled transcript against what streamed in piece by piece.
    const char* transcript = doc["transcript"] | (const char*)nullptr;
    LOG_DEBUG("response.output_audio_transcript.done -> full transcript: \"%s\"\n",
              transcript != nullptr ? transcript : "(none)");
    if (transcript != nullptr) {
      esp32va_status::reportTextLine("SAID", transcript);  // the app's chat bubble
    }
    return;
  }

  if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
    // Best-effort: a separate ASR model, not the realtime model itself,
    // running asynchronously - see header comment. Never affects the
    // LISTEN cycle's verdict, which depends only on the spoken reply.
    const char* transcript = doc["transcript"] | (const char*)nullptr;
    esp32va_status::endRawLine();  // it can arrive while the reply's transcript is streaming
    Serial.printf("You said: \"%s\"\n", transcript != nullptr ? transcript : "(none)");
    esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kHeardTranscript);
    esp32va_measure::setHeardLength(transcript != nullptr ? strlen(transcript) : 0);
    if (transcript != nullptr) {
      esp32va_status::reportTextLine("HEARD", transcript);  // the app's chat bubble
    }
    if (last_turn_was_hands_free) {
      esp32va_hands_free::noteUtteranceTranscript(transcript);  // no words: it was noise after all
    }
    return;
  }

  if (strcmp(type, "conversation.item.input_audio_transcription.failed") == 0) {
    const char* error_message = doc["error"]["message"] | "(no error detail)";
    LOG_ERROR("Input transcription failed: %s\n", error_message);
    LOG_DEBUG("Input transcript unavailable this cycle - the spoken reply itself is unaffected.\n");
    return;
  }

  if (strcmp(type, "input_audio_buffer.cleared") == 0) {
    LOG_DEBUG("input_audio_buffer.cleared - the discarded utterance's audio is gone.\n");
    return;
  }

  if (strcmp(type, "response.created") == 0) {
    // Also after an error that cleared response_in_flight (see "error"
    // below) but didn't stop this response: it is still on its way.
    noteResponseCreated();
    LOG_DEBUG("response.created - a response is running.\n");
    return;
  }

  if (strcmp(type, "response.done") == 0) {
    const char* status = doc["response"]["status"] | "(unknown)";
    LOG_DEBUG("response.done -> status: %s\n", status);
    server_response_running = false;
    // Whatever the status, no more audio is coming for this response. A
    // function-call-only response.done has no recording open - a no-op.
    esp32va_reply_recorder::noteReplyFinished();
    if (dropping_reply_audio) {  // the response CANCEL stopped is over
      noteCancelledResponseOver();
      LOG_INFO("The cancelled response is over (%s).\n", status);
      return;
    }
    if (strcmp(status, "failed") == 0) {
      noteFailedResponseDone();
      const char* error_message = doc["response"]["status_details"]["error"]["message"] | "(no error detail)";
      markReplyFailed(error_message);
    } else if (function_call_turn_pending) {
      // This response.done was for the function-call-only turn (no audio) -
      // every call in it already sent its function_call_output. Only now,
      // with that response over, is the follow-up response.create sent -
      // once, however many calls it held (one per call, sent while the
      // response was still running, got "Conversation already has an
      // active response in progress"). The *next* response.done is the
      // model's real spoken confirmation, which does count as the reply.
      function_call_turn_pending = false;
      webSocket.sendTXT(kResponseCreateMessage);
      esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kToolResponseDone);
      LOG_INFO("Function call done - waiting for spoken confirmation.\n");
    } else {
      // The PASS verdict itself waits for the ring buffer to actually finish
      // draining to the speaker - see checkPlaybackComplete() in loop().
      // Only a LISTEN's own response completes it - not the greeting's, a
      // timer announcement's or a typed question's.
      response_in_flight = false;
      if (awaiting_reply) {
        response_fully_received = true;
        esp32va_measure::markTurnEvent(esp32va_measure::TurnEvent::kResponseDone);
      }
    }
    return;
  }

  if (strcmp(type, "error") == 0) {
    const char* message = doc["error"]["message"] | "(no message)";
    const char* code = doc["error"]["code"] | "";
    if (dropping_reply_audio && strcmp(code, "response_cancel_not_active") == 0) {
      noteCancelledResponseAlreadyOver();
      return;
    }
    LOG_ERROR("Error: %s\n", message);
    if (!server_response_running) {
      // No response is running (e.g. the response.create was rejected), so
      // no response.done will clear it - else hands-free waits for the
      // reply timeout.
      noteResponseRejected();
    }
    markReplyFailed(message);
    return;
  }

  LOG_DEBUG("Event type: %s (routine protocol bookkeeping, no action needed).\n", type);
}

/** Shows the OpenAI startup check as failed (red in the laptop app):
    waiting for Wi-Fi, or offline, retrying every offlineRetryIntervalMs() -
    and that OpenAI refused the API key, if that's why. */
void reportOpenAiStageOffline() {
  if (!esp32va_wifi::isWifiConnected()) {
    esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kFail,
                                "waiting for Wi-Fi");
    return;
  }
  char stage_detail[kStateDetailBufferSize];
  int refused_http_code = esp32va_openai::lastKeyRefusalHttpCode();
  if (refused_http_code != 0) {
    snprintf(stage_detail, sizeof(stage_detail), "API key refused (HTTP %d) - retrying every %lu s", refused_http_code,
             esp32va_openai::offlineRetryIntervalMs() / 1000);
  } else {
    snprintf(stage_detail, sizeof(stage_detail), "offline - retrying every %lu s",
             esp32va_openai::offlineRetryIntervalMs() / 1000);
  }
  esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kFail, stage_detail);
}

/** Handles WebSocket lifecycle events. CONNECTED/DISCONNECTED/TEXT get
    explicit handling here; everything else is delegated to
    esp32va_openai::logGenericWsEvent() so nothing goes unnoticed. */
void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connection_count++;
      esp32va_measure::markBootMilestone(esp32va_measure::BootMilestone::kSocketConnected);  // first connect only
      LOG_INFO("Connected (#%u).\n", connection_count);
      esp32va_openai::noteHandshakeAccepted();  // OpenAI took the API key
      session_ready = false;
      esp32va_status::reportStage(esp32va_status::Stage::kOpenAi, esp32va_status::StageState::kActive,
                                  "setting up the session");
      // Deferred to loop() rather than sent here: calling sendTXT() directly
      // inside this callback can deadlock the library (discovered in old
      // step 7 - see Documentation/decisions.md "Discoveries").
      should_send_session_update = true;
      break;
    case WStype_DISCONNECTED: {
      // Distinguishing why matters: mid-turn is a real, visible failure
      // (that LISTEN cycle's reply is lost); idle is expected/benign, most
      // likely the server's own idle timeout.
      bool wifi_up = esp32va_wifi::isWifiConnected();
      if (is_capturing || awaiting_reply) {
        LOG_ERROR("Disconnected mid-turn (LISTEN #%u lost).\n", listen_cycle);
      } else if (!wifi_up) {
        LOG_INFO("Disconnected - reconnecting once Wi-Fi is back.\n");
      } else {
        LOG_INFO("Disconnected (idle) - reconnecting.\n");
        LOG_DEBUG("Disconnected while idle between LISTEN cycles - likely a server idle timeout.\n");
      }
      noteSessionLost();
      if (is_capturing) {
        abortCapture("disconnected while recording");  // stops the mic (no send happens here)
      }
      esp32va_reply_recorder::noteReplyAborted();  // keeps what was saved; must come before the ring reset
      esp32va_audio::resetRingBuffer();  // any partial reply audio is now stale - don't play it
      markReplyFailed("disconnected before a reply arrived");
      session_not_ready_since_ms = millis();
      if (!wifi_up) {
        // Not OpenAI's doing, so not counted as a failed attempt - and no
        // attempt is made until Wi-Fi is back (loop() stops polling the
        // library; dropDeadSocketOnWifiLoss() reconnects at once then).
        reportOpenAiStageOffline();
        break;
      }
      // A handshake OpenAI refused (HTTP 401/403): the API key - counted,
      // and the saved key may give way to SECRETS.h's (or back).
      int refused_http_code = esp32va_openai::noteDisconnectReason(payload, length, webSocket);
      if (esp32va_openai::registerDisconnect(webSocket)) {
        // Offline - but never halted: it keeps retrying, and the timer and
        // the controls keep working meanwhile.
        reportOpenAiStageOffline();
        Serial.printf("OpenAI unreachable - offline. The timer and the controls still work; retrying every %lu s.\n",
                      esp32va_openai::offlineRetryIntervalMs() / 1000);
      } else if (!esp32va_openai::isOffline()) {
        char stage_detail[kStateDetailBufferSize];
        if (refused_http_code != 0) {
          snprintf(stage_detail, sizeof(stage_detail), "API key refused (HTTP %d) - retrying (attempt %u of %u)",
                   refused_http_code, (unsigned)esp32va_openai::reconnectAttemptCount(),
                   (unsigned)esp32va_openai::attemptsBeforeOffline());
        } else {
          snprintf(stage_detail, sizeof(stage_detail), "disconnected - reconnecting (attempt %u of %u)",
                   (unsigned)esp32va_openai::reconnectAttemptCount(),
                   (unsigned)esp32va_openai::attemptsBeforeOffline());
        }
        esp32va_status::reportStage(esp32va_status::Stage::kOpenAi,
                                    refused_http_code != 0 ? esp32va_status::StageState::kFail
                                                           : esp32va_status::StageState::kActive,
                                    stage_detail);
      }
      break;
    }
    case WStype_TEXT:
      handleServerMessage(payload, length);
      break;
    default:
      esp32va_openai::logGenericWsEvent(type, payload, length);
      break;
  }
}

}  // namespace
