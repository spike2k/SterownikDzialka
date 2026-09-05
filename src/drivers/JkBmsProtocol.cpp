#include "drivers/JkBmsProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
uint16_t u16le(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t u32le(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

float f32le(const uint8_t* data, size_t offset) {
  const uint32_t raw = u32le(data, offset);
  float value = 0.0f;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

void copyText(const uint8_t* source, size_t width, char* target, size_t targetSize) {
  if (!target || targetSize == 0) return;
  size_t count = 0;
  while (count < width && count + 1 < targetSize && source[count] != 0) {
    const uint8_t c = source[count];
    target[count] = c >= 32 && c <= 126 ? static_cast<char>(c) : '?';
    ++count;
  }
  target[count] = '\0';
}

void finishCells(BatteryData& battery) {
  battery.minCellVoltageV = 0.0f;
  battery.maxCellVoltageV = 0.0f;
  battery.minCellNumber = 0;
  battery.maxCellNumber = 0;
  for (size_t i = 0; i < battery.cellCount; ++i) {
    const float value = battery.cellVoltageV[i];
    if (!(value > 0.1f && value < 6.0f)) continue;
    if (battery.minCellNumber == 0 || value < battery.minCellVoltageV) {
      battery.minCellVoltageV = value;
      battery.minCellNumber = static_cast<uint8_t>(i + 1);
    }
    if (battery.maxCellNumber == 0 || value > battery.maxCellVoltageV) {
      battery.maxCellVoltageV = value;
      battery.maxCellNumber = static_cast<uint8_t>(i + 1);
    }
  }
  battery.deltaCellVoltageV = battery.maxCellNumber && battery.minCellNumber
                                  ? battery.maxCellVoltageV - battery.minCellVoltageV
                                  : 0.0f;
}

int scoreJk02(const uint8_t* data, bool is32) {
  const size_t offset = is32 ? 32 : 0;
  const float pack = static_cast<float>(u32le(data, 118 + offset)) * 0.001f;
  const float current = static_cast<float>(static_cast<int32_t>(u32le(data, 126 + offset))) * 0.001f;
  const uint8_t soc = data[141 + offset];
  const size_t slots = is32 ? 32 : 24;
  float cellSum = 0.0f;
  size_t activeCells = 0;
  for (size_t i = 0; i < slots; ++i) {
    const float cell = static_cast<float>(u16le(data, 6 + i * 2)) * 0.001f;
    if (cell > 1.0f && cell < 5.0f) {
      cellSum += cell;
      ++activeCells;
    }
  }
  int score = 0;
  if (pack > 5.0f && pack < 200.0f) score += 4;
  if (std::fabs(current) < 1000.0f) score += 1;
  if (soc <= 100) score += 2;
  if (activeCells >= 4) score += 2;
  if (pack > 0.0f && cellSum > 0.0f && std::fabs(pack - cellSum) < std::max(3.0f, pack * 0.15f)) score += 5;
  return score;
}

bool looksLikeJk04(const uint8_t* data) {
  const float first = f32le(data, 6);
  const uint16_t jk02First = u16le(data, 6);
  return first > 1.0f && first < 5.0f && !(jk02First >= 1000 && jk02First <= 5000);
}

void decodeJk02(const uint8_t* data, BatteryData& battery, BatteryProtocol protocol) {
  const bool is32 = protocol == BatteryProtocol::Jk02_32S;
  const size_t cellSlots = is32 ? 32 : 24;
  const size_t offset = is32 ? 32 : 0;
  battery.cellVoltageV.fill(0.0f);
  battery.cellCount = 0;
  for (size_t i = 0; i < cellSlots; ++i) {
    const float cell = static_cast<float>(u16le(data, 6 + i * 2)) * 0.001f;
    battery.cellVoltageV[i] = cell;
    if (cell > 0.1f && cell < 6.0f) battery.cellCount = i + 1;
  }
  battery.packVoltageV = static_cast<float>(u32le(data, 118 + offset)) * 0.001f;
  battery.currentA = static_cast<float>(static_cast<int32_t>(u32le(data, 126 + offset))) * 0.001f;
  battery.powerW = battery.packVoltageV * battery.currentA;
  battery.temperaturesC.fill(0.0f);
  battery.temperaturesC[0] = static_cast<float>(static_cast<int16_t>(u16le(data, 130 + offset))) * 0.1f;
  battery.temperaturesC[1] = static_cast<float>(static_cast<int16_t>(u16le(data, 132 + offset))) * 0.1f;
  battery.temperatureCount = 2;
  if (is32) {
    battery.temperaturesC[2] = static_cast<float>(static_cast<int16_t>(u16le(data, 258))) * 0.1f;
    battery.temperaturesC[3] = static_cast<float>(static_cast<int16_t>(u16le(data, 256))) * 0.1f;
    battery.temperaturesC[4] = static_cast<float>(static_cast<int16_t>(u16le(data, 254))) * 0.1f;
    battery.temperatureCount = 5;
    battery.alarms = u32le(data, 166);
  } else {
    battery.alarms = u16le(data, 136);
  }
  battery.balancing = data[140 + offset] != 0;
  battery.socPercent = static_cast<float>(data[141 + offset]);
  battery.chargeMosOn = data[166 + offset] != 0;
  battery.dischargeMosOn = data[167 + offset] != 0;
  battery.protocol = protocol;
  finishCells(battery);
}

void decodeJk04(const uint8_t* data, BatteryData& battery) {
  battery.cellVoltageV.fill(0.0f);
  battery.cellCount = 0;
  battery.packVoltageV = 0.0f;
  for (size_t i = 0; i < 24; ++i) {
    const float cell = f32le(data, 6 + i * 4);
    if (std::isfinite(cell) && cell > 0.1f && cell < 6.0f) {
      battery.cellVoltageV[i] = cell;
      battery.cellCount = i + 1;
      battery.packVoltageV += cell;
    }
  }
  battery.currentA = 0.0f;
  battery.powerW = 0.0f;
  battery.socPercent = 0.0f;
  battery.temperatureCount = 0;
  battery.alarms = 0;
  battery.chargeMosOn = false;
  battery.dischargeMosOn = false;
  battery.balancing = data[220] != 0;
  battery.protocol = BatteryProtocol::Jk04;
  finishCells(battery);
}
}  // namespace

uint8_t JkBmsProtocol::checksum(const uint8_t* data, size_t length) {
  uint8_t result = 0;
  for (size_t i = 0; i < length; ++i) result = static_cast<uint8_t>(result + data[i]);
  return result;
}

std::array<uint8_t, 20> JkBmsProtocol::buildReadCommand(uint8_t command, uint8_t sequence) {
  std::array<uint8_t, 20> frame{};
  frame[0] = 0xAA;
  frame[1] = 0x55;
  frame[2] = 0x90;
  frame[3] = 0xEB;
  frame[4] = command;
  frame[16] = sequence;
  frame[19] = checksum(frame.data(), frame.size() - 1);
  return frame;
}

bool JkBmsProtocol::validFrame(const uint8_t* data, size_t length) {
  return data && length >= FrameSize && data[0] == 0x55 && data[1] == 0xAA && data[2] == 0xEB &&
         data[3] == 0x90 && checksum(data, FrameSize - 1) == data[FrameSize - 1];
}

bool JkBmsProtocol::decode(const uint8_t* data, size_t length, BatteryData& battery,
                           BatteryProtocol forcedProtocol, uint32_t timestampMs) {
  if (!validFrame(data, length) || data[4] != 0x02) return false;
  BatteryProtocol protocol = forcedProtocol;
  if (protocol == BatteryProtocol::Unknown) {
    if (looksLikeJk04(data)) {
      protocol = BatteryProtocol::Jk04;
    } else {
      const int score24 = scoreJk02(data, false);
      const int score32 = scoreJk02(data, true);
      if (std::max(score24, score32) < 8 || score24 == score32) return false;
      protocol = score32 > score24 ? BatteryProtocol::Jk02_32S : BatteryProtocol::Jk02_24S;
    }
  }
  if (protocol == BatteryProtocol::Jk04) decodeJk04(data, battery);
  else decodeJk02(data, battery, protocol);
  battery.lastUpdateMs = timestampMs;
  battery.online = battery.cellCount > 0;
  return battery.online;
}

bool JkBmsProtocol::decodeDeviceInfo(const uint8_t* data, size_t length, char* model, size_t modelSize,
                                     char* hardware, size_t hardwareSize, char* software, size_t softwareSize) {
  if (!validFrame(data, length) || data[4] != 0x03) return false;
  copyText(data + 6, 16, model, modelSize);
  copyText(data + 22, 8, hardware, hardwareSize);
  copyText(data + 30, 8, software, softwareSize);
  return true;
}
