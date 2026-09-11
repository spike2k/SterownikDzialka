#pragma once

#include <Arduino.h>

class OtaUpdater {
 public:
  bool install(const char* expectedSha256, String& error);
  bool installFrom(const char* firmwareUrl, const char* expectedSha256, String& error);
};
