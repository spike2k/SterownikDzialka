#pragma once

#include "core/Settings.h"
#include "core/Telemetry.h"

class PylontechEmulator {
 public:
  void begin(const Settings& settings);
  void tick(const Telemetry& telemetry);
  uint32_t takeRxEdges();

 private:
  static void IRAM_ATTR onRxEdge(void* arg);

  const Settings* settings_ = nullptr;
  volatile uint32_t rxEdges_ = 0;
  int rxPin_ = -1;
};
