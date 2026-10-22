#include "command_parse_utils.h"

#include <string.h>
#include <strings.h>

namespace esp32va_cmd {

const char* commandArgument(const char* command, const char* word) {
  size_t word_length = strlen(word);
  if (strncasecmp(command, word, word_length) != 0) {
    return nullptr;
  }
  const char* rest = command + word_length;
  if (*rest != ' ' && *rest != '\0') {
    return nullptr;
  }
  while (*rest == ' ') {
    rest++;
  }
  return rest;
}

bool parseOnOff(const char* text, bool* on_out) {
  if (strcasecmp(text, "on") == 0) {
    *on_out = true;
    return true;
  }
  if (strcasecmp(text, "off") == 0) {
    *on_out = false;
    return true;
  }
  return false;
}

}  // namespace esp32va_cmd
