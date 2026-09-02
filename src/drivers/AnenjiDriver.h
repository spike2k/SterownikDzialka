#pragma once

#include "core/Settings.h"
#include "core/Telemetry.h"

class AnenjiDriver {
 public:
  void begin(const Settings& settings);
  bool poll(Telemetry& telemetry);
  HardwareSerial& serial() { return serial_; }

 private:
  void openSerial(bool invert);
  void hello();
  bool probeFault();
  bool readHolding(uint16_t firstRegister, uint16_t registerCount, uint16_t* registers,
                   size_t registerCapacity, uint8_t* rxScratch, size_t rxScratchSize,
                   size_t& rxLength);
  size_t receive(uint8_t* buffer, size_t bufferSize, uint32_t timeoutMs);
  void markOffline(Telemetry& telemetry, const char* reason, const uint8_t* rx, size_t rxLength);
  void printFrame(const char* label, const uint8_t* data, size_t length) const;
  bool shouldLog(uint32_t intervalMs) const;

  HardwareSerial serial_{2};
  const Settings* settings_ = nullptr;
  bool enabled_ = false;
  bool wasOnline_ = false;
  bool invert_ = false;
  uint32_t lastLogMs_ = 0;
  uint32_t lastHelloMs_ = 0;
};
