#pragma once

#include "core/Settings.h"
#include "drivers/BatteryEmulator.h"

class PylontechEmulator : public BatteryEmulator {
 public:
  void begin(const Settings& settings);
  void tick(const BatteryData& battery) override;
  uint32_t takeRxEdges();

 private:
  static void IRAM_ATTR onRxEdge(void* arg);

  const Settings* settings_ = nullptr;
  volatile uint32_t rxEdges_ = 0;
  int rxPin_ = -1;
};
