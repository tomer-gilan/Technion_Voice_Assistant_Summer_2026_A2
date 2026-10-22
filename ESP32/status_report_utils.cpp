#include "status_report_utils.h"

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include "log_utils.h"

namespace esp32va_status {

namespace {

constexpr size_t kStageCount = static_cast<size_t>(Stage::kCount);
constexpr size_t kDetailBufferSize = 72;  // e.g. "SDHC 7580 MB, self-test passed" - room to spare
constexpr size_t kProtocolLineBufferSize = 1024;  // the longest is the @@TIMING line (~850 chars at worst since step 30)

struct StageRecord {
  StageState state;
  char detail[kDetailBufferSize];
};

// Static, not per-call: these are replayed by reprintStatusSnapshot().
StageRecord stage_records[kStageCount] = {};
PipelineState current_pipeline_state = PipelineState::kBooting;
char current_pipeline_detail[kDetailBufferSize] = "";
bool pipeline_state_reported = false;
bool raw_line_open = false;

/** Protocol word for a startup check. */
const char* stageName(Stage stage) {
  switch (stage) {
    case Stage::kSdCard:
      return "sd";
    case Stage::kMicrophone:
      return "mic";
    case Stage::kSpeaker:
      return "speaker";
    case Stage::kWifi:
      return "wifi";
    case Stage::kTimeSync:
      return "time";
    case Stage::kOpenAi:
      return "openai";
    default:
      LOG_ERROR("Unexpected stage %d.\n", (int)stage);
      return "unknown";
  }
}

/** Protocol word for a startup check's state. */
const char* stageStateName(StageState state) {
  switch (state) {
    case StageState::kPending:
      return "pending";
    case StageState::kActive:
      return "active";
    case StageState::kOk:
      return "ok";
    case StageState::kFail:
      return "fail";
    default:
      LOG_ERROR("Unexpected stage state %d.\n", (int)state);
      return "unknown";
  }
}

/** Protocol word for an assistant state. */
const char* pipelineStateName(PipelineState state) {
  switch (state) {
    case PipelineState::kBooting:
      return "booting";
    case PipelineState::kReady:
      return "ready";
    case PipelineState::kArmed:
      return "armed";
    case PipelineState::kListening:
      return "listening";
    case PipelineState::kProcessing:
      return "processing";
    case PipelineState::kTool:
      return "tool";
    case PipelineState::kSpeaking:
      return "speaking";
    case PipelineState::kSaving:
      return "saving";
    case PipelineState::kReconnecting:
      return "reconnecting";
    case PipelineState::kOffline:
      return "offline";
    case PipelineState::kHalted:
      return "halted";
    default:
      LOG_ERROR("Unexpected pipeline state %d.\n", (int)state);
      return "unknown";
  }
}

/** Copies detail (nullptr counts as empty) into a fixed buffer, cut to fit. */
void copyDetail(char* destination, const char* detail) {
  snprintf(destination, kDetailBufferSize, "%s", detail != nullptr ? detail : "");
}

/** Ends a raw line still being printed (see setRawLineOpen()), so the next
    output starts at the beginning of a line. */
void startProtocolLine() {
  if (raw_line_open) {
    Serial.println();
    raw_line_open = false;
  }
}

/** Prints one startup check's @@STAGE line from its stored record. */
void printStageLine(Stage stage) {
  startProtocolLine();
  const StageRecord& record = stage_records[static_cast<size_t>(stage)];
  if (record.detail[0] == '\0') {
    Serial.printf("@@STAGE %s %s\n", stageName(stage), stageStateName(record.state));
  } else {
    Serial.printf("@@STAGE %s %s %s\n", stageName(stage), stageStateName(record.state), record.detail);
  }
}

/** Prints the current @@STATE line. */
void printPipelineLine() {
  startProtocolLine();
  if (current_pipeline_detail[0] == '\0') {
    Serial.printf("@@STATE %s\n", pipelineStateName(current_pipeline_state));
  } else {
    Serial.printf("@@STATE %s %s\n", pipelineStateName(current_pipeline_state), current_pipeline_detail);
  }
}

}  // namespace

void reportStage(Stage stage, StageState state, const char* detail) {
  if (stage >= Stage::kCount) {
    LOG_ERROR("Stage %d is out of range - not reported.\n", (int)stage);
    return;
  }
  StageRecord& record = stage_records[static_cast<size_t>(stage)];
  record.state = state;
  copyDetail(record.detail, detail);
  printStageLine(stage);
}

bool isStageOk(Stage stage) {
  return stage < Stage::kCount && stage_records[static_cast<size_t>(stage)].state == StageState::kOk;
}

void reportPipelineState(PipelineState state, const char* detail) {
  char new_detail[kDetailBufferSize];
  copyDetail(new_detail, detail);
  if (pipeline_state_reported && state == current_pipeline_state &&
      strcmp(new_detail, current_pipeline_detail) == 0) {
    return;
  }
  pipeline_state_reported = true;
  current_pipeline_state = state;
  memcpy(current_pipeline_detail, new_detail, kDetailBufferSize);
  printPipelineLine();
  LOG_DEBUG("Assistant state -> %s %s\n", pipelineStateName(state), current_pipeline_detail);
}

void printProtocolLine(const char* format, ...) {
  static char line[kProtocolLineBufferSize];  // static: avoids a stack array on every call
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  startProtocolLine();
  Serial.println(line);
}

void reportTextLine(const char* tag, const char* text) {
  startProtocolLine();
  Serial.printf("@@%s ", tag);
  for (const char* cursor = text != nullptr ? text : ""; *cursor != '\0'; cursor++) {
    Serial.write((*cursor == '\n' || *cursor == '\r') ? ' ' : *cursor);
  }
  Serial.println();
}

void setRawLineOpen(bool open) {
  raw_line_open = open;
}

void endRawLine() {
  startProtocolLine();
}

void reprintStatusSnapshot() {
  for (size_t index = 0; index < kStageCount; index++) {
    printStageLine(static_cast<Stage>(index));
  }
  printPipelineLine();
}

}  // namespace esp32va_status
