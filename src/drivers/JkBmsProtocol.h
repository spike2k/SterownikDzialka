#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/BatteryData.h"

class JkBmsProtocol {
 public:
  static constexpr size_t FrameSize = 300;
  static constexpr size_t MaxFrameSize = 400;

  static uint8_t checksum(const uint8_t* data, size_t length);
  static std::array<uint8_t, 20> buildReadCommand(uint8_t command, uint8_t sequence);
  static bool validFrame(const uint8_t* data, size_t length);
  static bool decode(const uint8_t* data, size_t length, BatteryData& battery,
                     BatteryProtocol forcedProtocol = BatteryProtocol::Unknown,
                     uint32_t timestampMs = 0);
  static bool decodeDeviceInfo(const uint8_t* data, size_t length, char* model, size_t modelSize,
                               char* hardware, size_t hardwareSize, char* software, size_t softwareSize);
};
