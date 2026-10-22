#include "openai_realtime_utils.h"

#include <Arduino.h>
#include "parameters.h"
#include "secrets_include.h"
#include "log_utils.h"

namespace {
const char kOpenAiHost[] = "api.openai.com";
constexpr uint16_t kOpenAiPort = 443;
const char kOpenAiPath[] = "/v1/realtime?model=" OPENAI_MODEL;
const char kOpenAiAuthHeader[] = "Authorization: Bearer " OPENAI_API_KEY;

// GTS Root R4 - the root api.openai.com's cert chain verifies up to (leaf
// -> Google Trust Services "WE1" intermediate -> this root). Verified
// directly against the live chain via `openssl s_client -connect
// api.openai.com:443 -showcerts` and fetched from
// https://pki.goog/repo/certs/gtsr4.pem.
const char kOpenAiRootCaCert[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)EOF";

// A closed/refused connection could be one transient blip - only call the
// board offline once it has happened kOpenAiAttemptsBeforeOffline times in a
// row (a bad API key, no internet, a server rejection). Even then it keeps
// trying, every kOfflineRetryIntervalMs, and recovers by itself.
constexpr uint32_t kAttemptsBeforeOffline = esp32va_params::kOpenAiAttemptsBeforeOffline;
constexpr unsigned long kInitialBackoffMs = esp32va_params::kInitialReconnectBackoffMs;
constexpr unsigned long kOfflineRetryIntervalMs = esp32va_params::kOfflineRetryIntervalMs;

uint32_t reconnect_attempts = 0;
unsigned long reconnect_backoff_ms = kInitialBackoffMs;

// One function_call_output message: the JSON around it, a ~40-char call_id
// and the longest tool output (~140 chars, get_current_time / set_timer),
// with room to spare. Static: no heap per tool call.
constexpr size_t kToolResponseBufferSize = 384;
char tool_response_message[kToolResponseBufferSize];
}  // namespace

namespace esp32va_openai {

void beginOpenAiRealtimeConnection(WebSocketsClient& webSocket) {
  webSocket.setExtraHeaders(kOpenAiAuthHeader);
  webSocket.beginSslWithCA(kOpenAiHost, kOpenAiPort, kOpenAiPath, kOpenAiRootCaCert);
  if (esp32va_params::kHeartbeatIntervalMs > 0) {
    webSocket.enableHeartbeat(esp32va_params::kHeartbeatIntervalMs, esp32va_params::kHeartbeatPongTimeoutMs,
                              esp32va_params::kHeartbeatMissedPongs);
  }
}

unsigned long addJitter(unsigned long base_ms) {
  long jitter_range = static_cast<long>(base_ms) / 5;
  long jitter = random(-jitter_range, jitter_range + 1);
  long result = static_cast<long>(base_ms) + jitter;
  return result < 0 ? 0 : static_cast<unsigned long>(result);
}

bool registerDisconnect(WebSocketsClient& webSocket) {
  reconnect_attempts++;
  if (reconnect_attempts >= kAttemptsBeforeOffline) {
    webSocket.setReconnectInterval(addJitter(kOfflineRetryIntervalMs));
    if (reconnect_attempts == kAttemptsBeforeOffline) {
      LOG_ERROR("OpenAI unreachable %u times in a row - offline, retrying every %lu s.\n",
                (unsigned)reconnect_attempts, kOfflineRetryIntervalMs / 1000);
      return true;
    }
    LOG_DEBUG("Still offline (attempt %u) - next try in ~%lu s.\n", (unsigned)reconnect_attempts,
              kOfflineRetryIntervalMs / 1000);
    return false;
  }

  unsigned long wait_ms = addJitter(reconnect_backoff_ms);
  LOG_INFO("Reconnecting (attempt %u/%u)...\n", (unsigned)reconnect_attempts, (unsigned)kAttemptsBeforeOffline);
  LOG_DEBUG("Disconnected (attempt %u/%u). Backing off ~%lu ms before retrying.\n", (unsigned)reconnect_attempts,
            (unsigned)kAttemptsBeforeOffline, wait_ms);
  webSocket.setReconnectInterval(wait_ms);
  reconnect_backoff_ms = min(reconnect_backoff_ms * 2, kOfflineRetryIntervalMs);
  return false;
}

void resetReconnectBackoff(WebSocketsClient& webSocket) {
  reconnect_attempts = 0;
  reconnect_backoff_ms = kInitialBackoffMs;
  webSocket.setReconnectInterval(kInitialBackoffMs);
}

uint32_t reconnectAttemptCount() {
  return reconnect_attempts;
}

uint32_t attemptsBeforeOffline() {
  return kAttemptsBeforeOffline;
}

bool isOffline() {
  return reconnect_attempts >= kAttemptsBeforeOffline;
}

unsigned long offlineRetryIntervalMs() {
  return kOfflineRetryIntervalMs;
}

void sendFunctionCallOutput(WebSocketsClient& webSocket, const char* call_id, const char* output) {
  snprintf(tool_response_message, kToolResponseBufferSize,
           R"JSON({"type":"conversation.item.create","item":{"type":"function_call_output","call_id":"%s","output":"%s"}})JSON",
           call_id, output);
  webSocket.sendTXT(tool_response_message);
}

void logGenericWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_BIN:
      LOG_DEBUG("Received binary frame: %u bytes.\n", (unsigned)length);
      break;
    case WStype_ERROR:
      LOG_ERROR("WebSocket error: %.*s\n", (int)length, payload);
      break;
    case WStype_FRAGMENT_TEXT_START:
      LOG_DEBUG("Fragmented TEXT started.\n");
      break;
    case WStype_FRAGMENT_BIN_START:
      LOG_DEBUG("Fragmented BIN started.\n");
      break;
    case WStype_FRAGMENT:
      LOG_DEBUG("Fragment, %u bytes.\n", (unsigned)length);
      break;
    case WStype_FRAGMENT_FIN:
      LOG_DEBUG("Final fragment, %u bytes.\n", (unsigned)length);
      break;
    case WStype_PING:
      LOG_DEBUG("Received PING.\n");
      break;
    case WStype_PONG:
      LOG_DEBUG("Received PONG.\n");
      break;
    default:
      LOG_ERROR("Unhandled event type %d.\n", (int)type);
      break;
  }
}

}  // namespace esp32va_openai
