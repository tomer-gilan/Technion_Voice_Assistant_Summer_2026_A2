#include "log_utils.h"

namespace esp32va_log {
// DEBUG for now, to find why a reply sometimes stalls after the recording
// ends - back to LOG_LEVEL_ERROR once that's fixed.
LogLevel g_log_level = LogLevel::LOG_LEVEL_DEBUG;
}  // namespace esp32va_log
