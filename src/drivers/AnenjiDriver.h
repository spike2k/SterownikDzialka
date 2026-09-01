#pragma once

#include "core/Settings.h"
#include "core/Telemetry.h"

class AnenjiDriver {
 public:
  void begin(const Settings& settings);
  bool poll(Telemetry& telemetry);
  HardwareSerial& serial() { return serial_; }

 private:
  HardwareSerial serial_{2};
};
