#pragma once

#include <Arduino.h>
#include <functional>

class OtaUpdater {
 public:
  using ServiceCallback = std::function<void()>;

  bool install(const char* expectedSha256, String& error, const ServiceCallback& service = {});
  bool installFrom(const char* firmwareUrl, const char* expectedSha256, String& error,
                   const ServiceCallback& service = {});
};
