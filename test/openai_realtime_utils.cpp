#include "openai_realtime_utils.h"

#include <Arduino.h>
#include "config.h"
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
// https://pki.goog/repo/certs/gtsr4.pem. Different root than the GTS Root
// R1 (RSA) used by the historical Gemini-era test sketches.
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

// A closed/refused connection could be one transient blip - only declare
// failure once it's happened enough times in a row that it looks like a
// genuine, non-transient problem (bad API key, wrong host/path, TLS
// rejection), per Tomer's Guidelines #4.
constexpr uint32_t kMaxConnectAttempts = 5;
constexpr unsigned long kInitialBackoffMs = 1000;
constexpr unsigned long kMaxBackoffMs = 30000;

uint32_t reconnect_attempts = 0;
unsigned long reconnect_backoff_ms = kInitialBackoffMs;
}  // namespace

void beginOpenAiRealtimeConnection(WebSocketsClient& webSocket) {
  webSocket.setExtraHeaders(kOpenAiAuthHeader);
  webSocket.beginSslWithCA(kOpenAiHost, kOpenAiPort, kOpenAiPath, kOpenAiRootCaCert);
}

unsigned long addJitter(unsigned long base_ms) {
  long jitter_range = static_cast<long>(base_ms) / 5;
  long jitter = random(-jitter_range, jitter_range + 1);
  long result = static_cast<long>(base_ms) + jitter;
  return result < 0 ? 0 : static_cast<unsigned long>(result);
}

bool registerDisconnectAndMaybeGiveUp(WebSocketsClient& webSocket) {
  reconnect_attempts++;
  if (reconnect_attempts >= kMaxConnectAttempts) {
    LOG_ERROR("Giving up - no verdict after %u attempts.\n", reconnect_attempts);
    return true;
  }

  unsigned long wait_ms = addJitter(reconnect_backoff_ms);
  LOG_INFO("Reconnecting (attempt %u/%u)...\n", reconnect_attempts, kMaxConnectAttempts);
  LOG_DEBUG("Disconnected before a verdict was reached (attempt %u/%u). Backing off ~%lu ms before retrying.\n",
            reconnect_attempts, kMaxConnectAttempts, wait_ms);
  webSocket.setReconnectInterval(wait_ms);
  reconnect_backoff_ms = min(reconnect_backoff_ms * 2, kMaxBackoffMs);
  return false;
}

void resetReconnectBackoff() {
  reconnect_attempts = 0;
  reconnect_backoff_ms = kInitialBackoffMs;
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
