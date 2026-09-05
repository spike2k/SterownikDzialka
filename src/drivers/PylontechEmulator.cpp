#include "drivers/PylontechEmulator.h"

#include <Arduino.h>

void IRAM_ATTR PylontechEmulator::onRxEdge(void* arg) {
  auto* self = static_cast<PylontechEmulator*>(arg);
  self->rxEdges_++;
}

void PylontechEmulator::begin(const Settings& settings) {
  if (rxPin_ >= 0) detachInterrupt(rxPin_);
  settings_ = &settings;
  rxEdges_ = 0;
  rxPin_ = settings.values.pylonRxPin;
  if (rxPin_ < 0) return;
  pinMode(rxPin_, INPUT);
  attachInterruptArg(rxPin_, onRxEdge, this, FALLING);
}

void PylontechEmulator::tick(const BatteryData& battery) {
  (void)battery;
  (void)settings_;
}

uint32_t PylontechEmulator::takeRxEdges() {
  noInterrupts();
  const uint32_t count = rxEdges_;
  rxEdges_ = 0;
  interrupts();
  return count;
}
