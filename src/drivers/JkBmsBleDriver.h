#pragma once

#include <Arduino.h>

#include "core/BatteryData.h"

class JkBmsBleDriver {
 public:
  struct Config {
    const char* mac = nullptr;       // pusty = automatyczne wyszukanie JK-BMS
    bool verbose = false;
    bool autoConnect = true;
  };

  JkBmsBleDriver();
  ~JkBmsBleDriver();
  void begin();
  void begin(const Config& config);
  void tick(BatteryData& battery);
  void setVerbose(bool enabled);
  void setProtocolHint(BatteryProtocol protocol);
  bool connected() const;
  const char* mac() const;
  const char* deviceModel() const;
  const char* hardwareVersion() const;
  const char* softwareVersion() const;
  uint32_t validFrames() const;
  uint32_t invalidFrames() const;
  static bool looksLikeJk(const char* name, bool advertisesFfe0);

 private:
  class Impl;
  Impl* impl_ = nullptr;
};
