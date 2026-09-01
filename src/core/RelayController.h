#pragma once

#include <Arduino.h>
#include <array>
#include "core/Settings.h"

enum class ControlMode { Auto, Manual };

class RelayController {
 public:
  void begin(Settings& settings);
  void reconfigure();
  void tick(bool telemetryHealthy, float pvPowerW, float loadPowerW);
  bool setRelay(size_t index, bool enabled);
  void setMode(ControlMode mode);
  ControlMode mode() const;
  bool state(size_t index) const;
  int pin(size_t index) const;
  bool active(size_t index) const;
  void allOff();

 private:
  void apply(size_t index, bool enabled);
  void releasePin(int pin);

  Settings* settings_ = nullptr;
  std::array<int8_t, Config::loadCount> armedPins_{};
  std::array<bool, Config::loadCount> states_{};
  std::array<uint32_t, Config::loadCount> lastToggleMs_{};
  ControlMode mode_ = ControlMode::Auto;
};
