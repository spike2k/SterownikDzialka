#include <cassert>
#include <cmath>
#include <cstdint>

#include "drivers/JkBmsProtocol.h"

namespace {
void put16(uint8_t* frame, size_t offset, uint16_t value) {
  frame[offset] = static_cast<uint8_t>(value);
  frame[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* frame, size_t offset, uint32_t value) {
  frame[offset] = static_cast<uint8_t>(value);
  frame[offset + 1] = static_cast<uint8_t>(value >> 8);
  frame[offset + 2] = static_cast<uint8_t>(value >> 16);
  frame[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void makeFrame(uint8_t* frame) {
  frame[0] = 0x55;
  frame[1] = 0xAA;
  frame[2] = 0xEB;
  frame[3] = 0x90;
  frame[4] = 0x02;
}

void seal(uint8_t* frame) { frame[299] = JkBmsProtocol::checksum(frame, 299); }

void test32() {
  uint8_t frame[JkBmsProtocol::FrameSize]{};
  makeFrame(frame);
  for (size_t i = 0; i < 8; ++i) put16(frame, 6 + i * 2, static_cast<uint16_t>(3331 + i * 2));
  put32(frame, 150, 26704);
  put32(frame, 158, static_cast<uint32_t>(static_cast<int32_t>(-12400)));
  put16(frame, 162, 251);
  put16(frame, 164, 263);
  put32(frame, 166, 0x20);
  frame[172] = 1;
  frame[173] = 87;
  frame[198] = 1;
  frame[199] = 1;
  seal(frame);
  BatteryData data;
  assert(JkBmsProtocol::decode(frame, sizeof(frame), data, BatteryProtocol::Unknown, 42));
  assert(data.protocol == BatteryProtocol::Jk02_32S);
  assert(data.cellCount == 8);
  assert(std::fabs(data.packVoltageV - 26.704f) < 0.001f);
  assert(std::fabs(data.currentA + 12.4f) < 0.001f);
  assert(data.socPercent == 87);
  assert(data.chargeMosOn && data.dischargeMosOn && data.balancing);
  assert(data.alarms == 0x20);
}

void test24() {
  uint8_t frame[JkBmsProtocol::FrameSize]{};
  makeFrame(frame);
  for (size_t i = 0; i < 8; ++i) put16(frame, 6 + i * 2, 3300);
  put32(frame, 118, 26400);
  put32(frame, 126, 5000);
  frame[141] = 75;
  frame[166] = 1;
  frame[167] = 0;
  seal(frame);
  BatteryData data;
  assert(JkBmsProtocol::decode(frame, sizeof(frame), data));
  assert(data.protocol == BatteryProtocol::Jk02_24S);
  assert(data.cellCount == 8);
  assert(data.currentA == 5.0f);
  assert(data.chargeMosOn && !data.dischargeMosOn);
}
}  // namespace

int main() {
  test32();
  test24();
  const auto command = JkBmsProtocol::buildReadCommand(0x97, 3);
  assert(command[0] == 0xAA && command[4] == 0x97 && command[16] == 3);
  assert(command[19] == JkBmsProtocol::checksum(command.data(), 19));
  return 0;
}
