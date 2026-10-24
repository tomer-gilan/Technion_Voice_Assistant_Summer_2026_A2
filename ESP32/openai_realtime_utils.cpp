#include "openai_realtime_utils.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "parameters.h"
#include "secrets_include.h"
#include "command_parse_utils.h"
#include "log_utils.h"
#include "settings_store_utils.h"
#include "status_report_utils.h"

namespace {
const char kOpenAiHost[] = "api.openai.com";
constexpr uint16_t kOpenAiPort = 443;
const char kOpenAiPath[] = "/v1/realtime?model=" OPENAI_MODEL;
const char kAuthHeaderPrefix[] = "Authorization: Bearer ";
const char kApiKeyPrefix[] = "sk-";
const char kApiKeySettingKey[] = "openai_key";
constexpr size_t kKeyHintBufferSize = 16;  // "sk-...AbCd"
constexpr uint32_t kKeyRefusalsBeforeFallback = esp32va_params::kOpenAiKeyRefusalsBeforeFallback;
static_assert(sizeof(OPENAI_API_KEY) - 1 <= esp32va_openai::kMaxApiKeyLength,
              "SECRETS.h's OPENAI_API_KEY is longer than kMaxApiKeyLength (openai_realtime_utils.h) - raise it");

char active_api_key[esp32va_openai::kMaxApiKeyLength + 1] = "";
bool using_saved_key = false;
// The Authorization header the next handshake sends (the library copies it).
char auth_header[sizeof(kAuthHeaderPrefix) + esp32va_openai::kMaxApiKeyLength] = "";
uint32_t key_refusals_in_a_row = 0;  // on the active key, since it last got through or was chosen
int key_refused_http_code = 0;       // see lastKeyRefusalHttpCode()

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
// trying, every kOfflineRetryIntervalMs, and recovers by itself - a new key
// from the control center is tried at once (the sketch's
// reconnectWithNewApiKey()).
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

/** True if key looks like an OpenAI API key: "sk-", then only letters,
    digits, '-' and '_', kMinApiKeyLength to kMaxApiKeyLength in all - so a
    paste with a space, a quote or a line break in it is caught here, and
    never reaches the HTTP header. */
bool isPlausibleApiKey(const char* key) {
  size_t length = strlen(key);
  if (length < esp32va_openai::kMinApiKeyLength || length > esp32va_openai::kMaxApiKeyLength ||
      strncmp(key, kApiKeyPrefix, sizeof(kApiKeyPrefix) - 1) != 0) {
    return false;
  }
  for (const char* character = key; *character != '\0'; character++) {
    if (!isalnum(static_cast<unsigned char>(*character)) && *character != '-' && *character != '_') {
      return false;
    }
  }
  return true;
}

/** Makes SECRETS.h's key the active one. */
void useDefaultKey() {
  using_saved_key = false;
  snprintf(active_api_key, sizeof(active_api_key), "%s", OPENAI_API_KEY);
}

/** Makes the key saved in flash the active one. Returns false, changing
    nothing, if no key was saved (or what was saved isn't one). */
bool useSavedKey() {
  char saved_key[esp32va_openai::kMaxApiKeyLength + 1];
  if (!esp32va_settings::loadTextSetting(kApiKeySettingKey, saved_key, sizeof(saved_key))) {
    return false;
  }
  if (!isPlausibleApiKey(saved_key)) {
    LOG_ERROR("The API key saved in flash doesn't look like one - using SECRETS.h's.\n");
    return false;
  }
  using_saved_key = true;
  snprintf(active_api_key, sizeof(active_api_key), "%s", saved_key);
  return true;
}

/** Writes the active key's hint - its first 3 and last 4 characters - into out. */
void describeActiveKey(char* out, size_t out_size) {
  size_t length = strlen(active_api_key);
  if (length < 8) {
    snprintf(out, out_size, "(not-set)");  // e.g. SECRETS.example.h's "sk-..."
  } else {
    snprintf(out, out_size, "%.3s...%s", active_api_key, active_api_key + length - 4);
  }
}

/** Puts the active key into the Authorization header the next handshake
    sends, tells the app (@@OPENAI_KEY), and starts the refusal count over. */
void applyActiveKey(WebSocketsClient& webSocket) {
  snprintf(auth_header, sizeof(auth_header), "%s%s", kAuthHeaderPrefix, active_api_key);
  webSocket.setExtraHeaders(auth_header);
  key_refusals_in_a_row = 0;
  esp32va_openai::announceApiKey();
}

/** The HTTP status in a WStype_DISCONNECTED reason ("... HTTP 401"), 0 if
    it holds none. reason isn't '\0'-terminated - length bytes. */
int httpCodeInReason(const uint8_t* reason, size_t length) {
  const char kMarker[] = "HTTP ";
  const size_t marker_length = sizeof(kMarker) - 1;
  if (reason == nullptr) {
    return 0;
  }
  for (size_t start = 0; start + marker_length < length; start++) {
    if (memcmp(reason + start, kMarker, marker_length) != 0) {
      continue;
    }
    int code = 0;
    for (size_t at = start + marker_length; at < length && at < start + marker_length + 3 && isdigit(reason[at]); at++) {
      code = code * 10 + (reason[at] - '0');
    }
    return code;
  }
  return 0;
}
}  // namespace

namespace esp32va_openai {

void loadApiKey() {
  if (!useSavedKey()) {
    useDefaultKey();
  }
  snprintf(auth_header, sizeof(auth_header), "%s%s", kAuthHeaderPrefix, active_api_key);
}

void announceApiKey() {
  char hint[kKeyHintBufferSize];
  describeActiveKey(hint, sizeof(hint));
  esp32va_status::printProtocolLine("@@OPENAI_KEY %s %s", using_saved_key ? "saved" : "default", hint);
}

bool handleApiKeyCommand(const char* command, WebSocketsClient& webSocket, bool* key_changed) {
  using esp32va_cmd::commandArgument;  // command_parse_utils.h
  *key_changed = false;
  const char* argument = nullptr;
  if ((argument = commandArgument(command, "OPENAI_KEY_FORGET")) != nullptr) {
    esp32va_settings::removeSetting(kApiKeySettingKey);
    useDefaultKey();
    key_refused_http_code = 0;
    LOG_INFO("Forgot the saved OpenAI API key - back to the default one.\n");
    applyActiveKey(webSocket);
    *key_changed = true;
    return true;
  }
  if ((argument = commandArgument(command, "OPENAI_KEY")) != nullptr) {
    if (*argument == '\0') {
      announceApiKey();
      return true;
    }
    if (!isPlausibleApiKey(argument)) {
      // The argument isn't echoed: it may well be a real key with one typo.
      Serial.printf("OPENAI_KEY ignored - expected an OpenAI API key: sk-..., %u to %u characters, only letters, "
                    "digits, - and _.\n",
                    (unsigned)kMinApiKeyLength, (unsigned)kMaxApiKeyLength);
      return true;
    }
    snprintf(active_api_key, sizeof(active_api_key), "%s", argument);
    esp32va_settings::saveTextSetting(kApiKeySettingKey, active_api_key);
    using_saved_key = true;
    key_refused_http_code = 0;
    LOG_INFO("Saved a new OpenAI API key (%u characters).\n", (unsigned)strlen(active_api_key));
    applyActiveKey(webSocket);
    *key_changed = true;
    return true;
  }
  return false;
}

void noteHandshakeAccepted() {
  key_refusals_in_a_row = 0;
  key_refused_http_code = 0;
}

int noteDisconnectReason(const uint8_t* reason, size_t length, WebSocketsClient& webSocket) {
  int http_code = httpCodeInReason(reason, length);
  if (http_code != 401 && http_code != 403) {
    return 0;
  }
  key_refused_http_code = http_code;
  key_refusals_in_a_row++;
  LOG_ERROR("OpenAI refused the %s API key (HTTP %d) - check it, or set another in the control center.\n",
            using_saved_key ? "saved" : "default", http_code);
  if (key_refusals_in_a_row < kKeyRefusalsBeforeFallback) {
    return http_code;
  }
  if (using_saved_key) {
    LOG_ERROR("Saved API key refused %u times in a row - trying the default key instead (the saved one is kept).\n",
              (unsigned)key_refusals_in_a_row);
    useDefaultKey();
  } else if (useSavedKey()) {
    LOG_ERROR("Default API key refused %u times in a row - trying the saved key again.\n",
              (unsigned)key_refusals_in_a_row);
  } else {
    return http_code;  // only the default key exists - keep trying it
  }
  applyActiveKey(webSocket);  // the next attempt (on the usual backoff) uses it
  return http_code;
}

int lastKeyRefusalHttpCode() {
  return key_refused_http_code;
}

void beginOpenAiRealtimeConnection(WebSocketsClient& webSocket) {
  if (auth_header[0] == '\0') {
    loadApiKey();  // setup() should have - but never connect with no key at all
  }
  webSocket.setExtraHeaders(auth_header);
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
