#pragma once

#include <Arduino.h>
#include <array>
#include "core/BatteryData.h"

constexpr size_t MaxCells = BatteryMaxCells;

struct Telemetry {
  float pvPowerW = 0;
  float loadPowerW = 0;
  float batterySoc = 0;
  float batteryVoltageV = 0;
  float batteryCurrentA = 0;
  std::array<float, MaxCells> cellVoltageV{};
  size_t cellCount = 0;
  bool jkOnline = false;
  bool anenjiOnline = false;
  bool pylonOnline = false;
  uint32_t updatedAtMs = 0;
};
