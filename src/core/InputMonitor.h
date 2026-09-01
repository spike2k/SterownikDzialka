#pragma once

#include "core/Settings.h"

class InputMonitor {
 public:
  void begin(Settings& settings);
  void reconfigure();
  bool configured(size_t index) const;
  bool active(size_t index) const;
  int pin(size_t index) const;

 private:
  Settings* settings_ = nullptr;
};
