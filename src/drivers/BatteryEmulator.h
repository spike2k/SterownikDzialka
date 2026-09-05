#pragma once

#include "core/BatteryData.h"

class BatteryEmulator {
 public:
  virtual ~BatteryEmulator() = default;
  virtual void tick(const BatteryData& battery) = 0;
};
