#pragma once

#include "core/Settings.h"
#include "drivers/AnenjiDriver.h"
#include "drivers/JkBmsDriver.h"
#include "drivers/PylontechEmulator.h"

class BusMonitor {
 public:
  void begin(Settings& settings, JkBmsDriver& jk, AnenjiDriver& anenji, PylontechEmulator& pylon);
  void tick();

 private:
  void pollConsole();
  void handleCommand(String line);
  void printHelp() const;
  void printStatus() const;
  void dumpUart(const char* name, HardwareSerial& serial, uint32_t baud, int rxPin, uint32_t& lastActivityMs);
  void dumpPylon();
  static void printDump(const char* name, const uint8_t* data, size_t length);

  Settings* settings_ = nullptr;
  JkBmsDriver* jk_ = nullptr;
  AnenjiDriver* anenji_ = nullptr;
  PylontechEmulator* pylon_ = nullptr;
  String line_;
  uint32_t lastJkMs_ = 0;
  uint32_t lastAnenjiMs_ = 0;
  uint32_t lastPylonMs_ = 0;
};
