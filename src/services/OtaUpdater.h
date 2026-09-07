#pragma once

#include <Arduino.h>

class OtaUpdater {
 public:
  bool install(const char* expectedSha256, String& error);
};
