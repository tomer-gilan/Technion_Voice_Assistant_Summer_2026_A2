// serial_commands.ino - the typed commands (from the control center or a
// serial terminal) and the BOOT button: Talk, the refused tone and the
// technician beep check.
//
// A tab of the ESP32 sketch: the Arduino IDE appends it after ESP32.ino
// (tabs in alphabetical order) into one translation unit, so it shares
// ESP32.ino's includes, globals and the declarations of every function.

namespace {

/** True for the commands before which hands-free listening stops: BEEP
    makes a sound the mic would hear, and a GET holds loop() for its whole
    transfer. */
bool commandPausesHandsFree(const char* command) {
  const char* const kPausingWords[] = {"BEEP", "GET"};
  for (const char* word : kPausingWords) {
    size_t word_length = strlen(word);
    if (strncasecmp(command, word, word_length) == 0 && (command[word_length] == '\0' || command[word_length] == ' ')) {
      return true;
    }
  }
  return false;
}

/** Prints everything the laptop app shows about the board right now: every
    startup check, the assistant's state, Wi-Fi signal, volume, LED, the
    light sensor, hands-free, the timer, the clock and the board's fixed
    limits (@@LIMITS) - the answer to STATUS. */
void reprintFullStatus() {
  esp32va_status::reprintStatusSnapshot();
  esp32va_wifi::reportWifiSignal();
  esp32va_wifi::announceWifiNetwork();
  esp32va_controls::announceControlStates();
  esp32va_button::announceButtonState();
  esp32va_light_sensor::announceLightState();
  esp32va_hands_free::announceHandsFreeState();
  esp32va_clock::announceTimerState();
  esp32va_clock::announceClock();
  esp32va_info::announceLimits();
}

/** The short low tone for a BOOT press that couldn't start a LISTEN.
    Skipped while the mic is recording (a hands-free utterance loop() hasn't
    taken yet), where the recording would pick it up. While a reply or the
    greeting plays it is spliced into the speech: both write the I2S port
    from loop(), one after the other. loop() only (i2s_write). */
void playRefusedTone() {
  if (esp32va_mic_capture::isCaptureInProgress()) {
    LOG_DEBUG("Refused tone skipped - the mic is recording.\n");
    return;
  }
  esp32va_hands_free::holdOff();
  esp32va_audio::playTone(esp32va_params::kRefusedToneHz, esp32va_params::kRefusedToneMs);
  esp32va_hands_free::holdOff();
  LOG_DEBUG("Refused tone played.\n");
}

/** The BOOT button (when switched on - button_utils.h): starts a LISTEN like
    the Talk button, or cancels the one being recorded or answered. A press
    that can't start one plays the refused tone. */
void handleTalkButtonPress() {
  if (!should_keep_running) {
    return;
  }
  if (is_capturing || awaiting_reply) {
    listen_started_by_last_press = 0;
    Serial.printf("Talk button: cancelling LISTEN #%u.\n", listen_cycle);
    cancelListenTurn();
    return;
  }
  Serial.printf("Talk button pressed.\n");
  bool started = startManualListen();
  listen_started_by_last_press = started ? listen_cycle : 0;
  if (!started) {
    playRefusedTone();
  }
}

/** The technician beep check, for BOOT held kTechnicianHoldMs: one beep per
    startup check in a fixed order - Wi-Fi, OpenAI, SD card, mic - high = ok,
    low = failed, as things stand right now (the mic: its startup check, as
    the mic task is running if this runs at all). Works with no PC and no
    network. The press already started a LISTEN when it went down - that one
    is cancelled first. Hands-free is held off around it, as for BEEP. Blocks
    ~2 s; loop() only (i2s_write). */
void runTechnicianCheck() {
  if (listen_started_by_last_press != 0 && listen_started_by_last_press == listen_cycle &&
      (is_capturing || awaiting_reply)) {
    Serial.printf("Talk button held - cancelling LISTEN #%u for the technician check.\n", listen_cycle);
    cancelListenTurn();
  }
  listen_started_by_last_press = 0;
  const bool wifi_ok = esp32va_wifi::isWifiConnected();
  const bool openai_ok = session_ready;
  const bool sd_ok = esp32va_sd_card::isSdCardUsable();
  const bool mic_ok = esp32va_status::isStageOk(esp32va_status::Stage::kMicrophone);
  LOG_INFO("Technician check: Wi-Fi %s, OpenAI %s, SD %s, mic %s.\n", wifi_ok ? "OK" : "FAILED",
           openai_ok ? "OK" : "FAILED", sd_ok ? "OK" : "FAILED", mic_ok ? "OK" : "FAILED");
  const bool checks_in_beep_order[] = {wifi_ok, openai_ok, sd_ok, mic_ok};
  esp32va_hands_free::holdOff();
  for (bool check_ok : checks_in_beep_order) {
    esp32va_audio::playTone(check_ok ? esp32va_params::kTechnicianOkToneHz : esp32va_params::kTechnicianFailToneHz,
                            esp32va_params::kTechnicianToneMs);
    esp32va_audio::waitUntilPlayedOut();
    delay(esp32va_params::kTechnicianGapMs);
  }
  esp32va_hands_free::holdOff();
}

/** Checks Serial for a typed command. LIST / GET / DELETE <name> / DELETE_ALL answer the laptop
    app (see sd_file_transfer_utils.h) - with @@BUSY unless idle. STATUS
    reprints the board's whole status (status_report_utils.h), CLOCK just
    the board's local time (clock_timer_utils.h). VOLUME / MUTE
    / BEEP / LED / LED_BRIGHTNESS control the hardware
    (device_controls_utils.h), LIGHT [<value>] / LIGHT_BANDS set/show the
    simulated light sensor (light_sensor_utils.h), VAD / VAD_SENSITIVITY /
    VAD_HANGOVER set hands-free listening (hands_free_utils.h), WIFI /
    WIFI_SSID / WIFI_PASSWORD / WIFI_FORGET the network (wifi_utils.h),
    BUTTON the BOOT button as a Talk button (button_utils.h) - all at any
    time; BEEP and GET stop hands-free listening first. LISTEN starts a
    manual capture if the board can take one (startManualListen()). ASK
    <text> sends a typed question (askTypedQuestion()).
    Anything else is logged and ignored. */
void checkForSerialCommand() {
  if (!Serial.available()) {
    return;
  }
  String command = Serial.readStringUntil('\n');
  if (command.endsWith("\r")) {
    command.remove(command.length() - 1);
  }
  const String raw_line = command;  // untrimmed - a Wi-Fi password keeps its spaces (wifi_utils.h)
  command.trim();
  if (command.length() == 0) {
    return;
  }
  bool pauses_hands_free = commandPausesHandsFree(command.c_str());
  if (pauses_hands_free) {
    esp32va_hands_free::holdOff();
  }
  bool handled = esp32va_sd_transfer::handleFileCommand(command.c_str(), isIdleForFileTransfer());
  if (!handled && command.equalsIgnoreCase("STATUS")) {
    reprintFullStatus();
    handled = true;
  }
  if (!handled && command.equalsIgnoreCase("CLOCK")) {
    esp32va_clock::announceClock();
    handled = true;
  }
  if (!handled) {
    handled = esp32va_controls::handleControlCommand(command.c_str(), isIdleForFileTransfer()) ||
              esp32va_light_sensor::handleLightCommand(command.c_str()) ||
              esp32va_hands_free::handleHandsFreeCommand(command.c_str()) ||
              esp32va_wifi::handleWifiCommand(command.c_str(), raw_line.c_str()) ||
              esp32va_button::handleButtonCommand(command.c_str());
  }
  if (pauses_hands_free) {
    esp32va_hands_free::holdOff();  // again: the cooldown counts from after the beep or the transfer
  }
  if (handled) {
    return;
  }
  if (command.length() > 4 && command.substring(0, 4).equalsIgnoreCase("ASK ")) {
    askTypedQuestion(command.substring(4));
    return;
  }
  if (command.equalsIgnoreCase("CANCEL")) {
    cancelListenTurn();
    return;
  }
  if (!command.equalsIgnoreCase("LISTEN")) {
    Serial.printf("Unknown command \"%s\" - expected LISTEN, CANCEL, ASK <text>, STATUS, CLOCK, VAD on|off, VAD_SENSITIVITY <1-5>, VAD_HANGOVER <600-1500>, LIGHT <0-%d>, LIGHT_BANDS <evening> <day>, VOLUME <0-100>, MUTE on|off, BEEP, SOUNDS on|off, BUTTON on|off, LED on|off, LED_BRIGHTNESS <0-100>, WIFI, WIFI_SSID <name>, WIFI_PASSWORD [<password>], WIFI_FORGET, LIST, GET <name>, DELETE <name> or DELETE_ALL [replies|commands].\n",
             command.c_str(), esp32va_light_sensor::kMaxLightSensorValue);
    return;
  }
  startManualListen();
}

}  // namespace
