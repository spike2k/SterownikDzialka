#include "drivers/AnenjiProtocol.h"

namespace AnenjiProtocol {
namespace {
constexpr uint16_t kMaxHoldingCount = 125;

int16_t asI16(uint16_t raw) { return static_cast<int16_t>(raw); }

float scaleU16(uint16_t raw, float scale) { return static_cast<float>(raw) * scale; }

float scaleI16(uint16_t raw, float scale) { return static_cast<float>(asI16(raw)) * scale; }

float clampPositivePowerW(uint16_t raw) {
  const int16_t watts = asI16(raw);
  if (watts < 0 || watts > static_cast<int16_t>(kMaxPlausiblePowerW)) return 0.0f;
  return static_cast<float>(watts);
}
}

uint16_t crc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                       : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

bool crcValid(const uint8_t* frame, size_t length) {
  if (!frame || length < 4) return false;
  const uint16_t received = static_cast<uint16_t>(frame[length - 2]) |
                            (static_cast<uint16_t>(frame[length - 1]) << 8U);
  return received == crc16(frame, length - 2);
}

bool buildReadHolding(uint8_t* out, size_t outSize, uint8_t slave, uint16_t firstRegister,
                      uint16_t registerCount) {
  if (!out || outSize < kRequestSize) return false;
  if (slave == 0 || registerCount == 0 || registerCount > kMaxHoldingCount) return false;
  out[0] = slave;
  out[1] = kReadHolding;
  out[2] = static_cast<uint8_t>(firstRegister >> 8U);
  out[3] = static_cast<uint8_t>(firstRegister & 0xFFU);
  out[4] = static_cast<uint8_t>(registerCount >> 8U);
  out[5] = static_cast<uint8_t>(registerCount & 0xFFU);
  const uint16_t crc = crc16(out, 6);
  out[6] = static_cast<uint8_t>(crc & 0xFFU);
  out[7] = static_cast<uint8_t>(crc >> 8U);
  return true;
}

bool parseReadHoldingResponse(const uint8_t* frame, size_t length, uint8_t expectedSlave,
                              uint16_t expectedCount, uint16_t* registers, size_t registerCapacity) {
  if (!frame || !registers) return false;
  if (expectedCount == 0 || expectedCount > registerCapacity) return false;
  const size_t expectedLength = 5U + static_cast<size_t>(expectedCount) * 2U;
  if (length != expectedLength) return false;
  if (frame[0] != expectedSlave || frame[1] != kReadHolding) return false;
  if (frame[2] != static_cast<uint8_t>(expectedCount * 2U)) return false;
  if (!crcValid(frame, length)) return false;
  for (uint16_t index = 0; index < expectedCount; ++index) {
    const size_t offset = 3U + static_cast<size_t>(index) * 2U;
    registers[index] = (static_cast<uint16_t>(frame[offset]) << 8U) | frame[offset + 1];
  }
  return true;
}

bool extractReadHoldingResponse(const uint8_t* data, size_t length, uint8_t expectedSlave,
                                uint16_t expectedCount, uint16_t* registers, size_t registerCapacity) {
  if (!data || expectedCount == 0) return false;
  const size_t frameLength = 5U + static_cast<size_t>(expectedCount) * 2U;
  if (length < frameLength) {
    return parseReadHoldingResponse(data, length, expectedSlave, expectedCount, registers,
                                    registerCapacity);
  }
  for (size_t offset = 0; offset + frameLength <= length; ++offset) {
    if (parseReadHoldingResponse(data + offset, frameLength, expectedSlave, expectedCount, registers,
                                 registerCapacity)) {
      return true;
    }
  }
  return false;
}

bool decodeLiveBlock(const uint16_t* registers, size_t count, LiveReading& reading) {
  if (!registers || count < kLiveRegisterCount) return false;
  reading.operationMode = registers[kOperationMode];
  reading.outputVoltageV = scaleU16(registers[kOutputVoltage], 0.1f);
  reading.inverterPowerW = clampPositivePowerW(registers[kInverterPower]);
  reading.loadPowerW = clampPositivePowerW(registers[kOutputActivePower]);
  reading.batteryVoltageV = scaleU16(registers[kBatteryVoltage], 0.1f);
  reading.batteryCurrentA = scaleI16(registers[kBatteryCurrent], 0.1f);
  reading.pvVoltageV = scaleU16(registers[kPvVoltage], 0.1f);
  return true;
}

bool decodeStatusBlock(const uint16_t* registers, size_t count, LiveReading& reading) {
  if (!registers || count < kStatusRegisterCount) return false;
  reading.pvPowerW = clampPositivePowerW(registers[kPvPower]);
  reading.loadPercent = registers[kLoadPercent];
  return true;
}

const char* operationModeName(uint16_t mode) {
  switch (mode) {
    case 0:
      return "PowerOn";
    case 1:
      return "Standby";
    case 2:
      return "Mains";
    case 3:
      return "OffGrid";
    case 4:
      return "Bypass";
    case 5:
      return "Charging";
    case 6:
      return "Fault";
    default:
      return "?";
  }
}
}
