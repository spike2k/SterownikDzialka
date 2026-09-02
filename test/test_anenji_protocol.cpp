#include "drivers/AnenjiProtocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int failures = 0;

void expect(bool condition, const char* name) {
  if (condition) {
    std::printf("OK  %s\n", name);
    return;
  }
  std::printf("FAIL %s\n", name);
  ++failures;
}

void expectEq(uint16_t actual, uint16_t wanted, const char* name) {
  if (actual == wanted) {
    std::printf("OK  %s\n", name);
    return;
  }
  std::printf("FAIL %s actual=%u wanted=%u\n", name, actual, wanted);
  ++failures;
}

void expectNear(float actual, float wanted, float epsilon, const char* name) {
  const float delta = actual > wanted ? actual - wanted : wanted - actual;
  if (delta <= epsilon) {
    std::printf("OK  %s\n", name);
    return;
  }
  std::printf("FAIL %s actual=%f wanted=%f\n", name, actual, wanted);
  ++failures;
}

void expectStr(const char* actual, const char* wanted, const char* name) {
  expect(actual && wanted && std::strcmp(actual, wanted) == 0, name);
}
}

int main() {
  uint8_t request[AnenjiProtocol::kRequestSize]{};
  expect(AnenjiProtocol::buildReadHolding(request, sizeof(request), 1, 200, 22), "build live read");
  const uint8_t capturedLiveReq[] = {0x01, 0x03, 0x00, 0xC8, 0x00, 0x16, 0x45, 0xFA};
  bool liveReqOk = true;
  for (size_t i = 0; i < sizeof(capturedLiveReq); ++i) liveReqOk = liveReqOk && request[i] == capturedLiveReq[i];
  expect(liveReqOk, "dongle TX 01 03 00 C8 00 16 45 FA");

  expect(AnenjiProtocol::buildReadHolding(request, sizeof(request), 1, 223, 13), "build status read");
  const uint8_t capturedStatusReq[] = {0x01, 0x03, 0x00, 0xDF, 0x00, 0x0D, 0xB5, 0xF5};
  bool statusReqOk = true;
  for (size_t i = 0; i < sizeof(capturedStatusReq); ++i)
    statusReqOk = statusReqOk && request[i] == capturedStatusReq[i];
  expect(statusReqOk, "dongle TX 01 03 00 DF 00 0D B5 F5");

  expect(!AnenjiProtocol::buildReadHolding(request, sizeof(request), 1, 200, 0), "reject empty read");
  expect(!AnenjiProtocol::buildReadHolding(request, sizeof(request), 0, 200, 22), "reject slave 0");

  const uint8_t emptyOne[] = {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44};
  expect(AnenjiProtocol::crcValid(emptyOne, sizeof(emptyOne)), "CRC empty one-register");

  // First dual-line sniff, ~90 W load.
  const uint8_t liveLow[] = {
      0x01, 0x03, 0x2C, 0xB0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xFC, 0x00,
      0x0A, 0x13, 0x88, 0x00, 0x5A, 0x00, 0x00, 0x08, 0xFC, 0x00, 0x06, 0x13, 0x88, 0x00, 0x4F, 0x00,
      0x8A, 0x01, 0x0E, 0xFF, 0xC8, 0xFF, 0x8F, 0x0E, 0x7D, 0x00, 0xA8, 0x00, 0x00, 0x00, 0x00, 0xC9,
      0xE6};
  uint16_t regs[AnenjiProtocol::kLiveRegisterCount]{};
  expect(AnenjiProtocol::parseReadHoldingResponse(liveLow, sizeof(liveLow), 1, 22, regs, 22),
         "parse live ~90 W");
  AnenjiProtocol::LiveReading low{};
  expect(AnenjiProtocol::decodeLiveBlock(regs, 22, low), "decode live ~90 W");
  expectEq(low.operationMode, 3, "mode OffGrid");
  expectNear(low.loadPowerW, 79.0f, 0.01f, "load is 213=79 W not 208=90 W");
  expectNear(low.inverterPowerW, 90.0f, 0.01f, "inverter power 208=90 W");
  expectNear(low.outputVoltageV, 230.0f, 0.05f, "output 210=230.0 V");
  expectNear(low.batteryVoltageV, 27.0f, 0.01f, "battery 27.0 V");
  expectNear(low.batteryCurrentA, -5.6f, 0.05f, "battery current 216=-5.6 A");
  expectNear(low.pvVoltageV, 16.8f, 0.05f, "PV voltage 219=16.8 V not power");

  // Later sniff with a large load (~914 W). Same commands, new values.
  const uint8_t liveHigh[] = {
      0x01, 0x03, 0x2C, 0xB0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x08, 0x00,
      0x2A, 0x13, 0x8E, 0x03, 0xC0, 0x00, 0x00, 0x09, 0x08, 0x00, 0x28, 0x13, 0x8E, 0x03, 0x92, 0x03,
      0x9C, 0x01, 0x09, 0xFE, 0x86, 0xFC, 0x17, 0x0E, 0x0D, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00, 0xC3,
      0x69};
  expect(sizeof(liveHigh) == AnenjiProtocol::kLiveResponseSize, "high-load live is 49 B");
  expect(AnenjiProtocol::crcValid(liveHigh, sizeof(liveHigh)), "CRC high-load live");
  expect(AnenjiProtocol::parseReadHoldingResponse(liveHigh, sizeof(liveHigh), 1, 22, regs, 22),
         "parse live ~914 W");
  AnenjiProtocol::LiveReading high{};
  expect(AnenjiProtocol::decodeLiveBlock(regs, 22, high), "decode live ~914 W");
  expectNear(high.loadPowerW, 914.0f, 0.01f, "load 213=914 W");
  expectNear(high.inverterPowerW, 960.0f, 0.01f, "inverter 208=960 W");
  expectNear(high.outputVoltageV, 231.2f, 0.05f, "output 231.2 V");
  expectNear(high.batteryVoltageV, 26.5f, 0.05f, "battery 26.5 V");
  expectNear(high.batteryCurrentA, -37.8f, 0.05f, "battery -37.8 A discharge");
  expectNear(high.pvVoltageV, 16.6f, 0.05f, "PV voltage 16.6 V");

  const uint8_t statusHigh[] = {0x01, 0x03, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x16,
                                0x00, 0x19, 0x00, 0x14, 0x00, 0x56, 0x00, 0x00, 0x00, 0x60, 0xFE,
                                0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x41};
  expect(sizeof(statusHigh) == AnenjiProtocol::kStatusResponseSize, "status frame is 31 B");
  uint16_t statusRegs[AnenjiProtocol::kStatusRegisterCount]{};
  expect(AnenjiProtocol::parseReadHoldingResponse(statusHigh, sizeof(statusHigh), 1, 13, statusRegs, 13),
         "parse status 223");
  expect(AnenjiProtocol::decodeStatusBlock(statusRegs, 13, high), "decode status 223");
  expectNear(high.pvPowerW, 0.0f, 0.01f, "PV power 223=0 W at night");
  expectEq(high.loadPercent, 21, "load 21% of 4200 W");

  uint8_t broken[sizeof(liveHigh)]{};
  for (size_t i = 0; i < sizeof(liveHigh); ++i) broken[i] = liveHigh[i];
  broken[sizeof(liveHigh) - 1] ^= 0x01;
  expect(!AnenjiProtocol::parseReadHoldingResponse(broken, sizeof(broken), 1, 22, regs, 22),
         "reject bad CRC");

  expectStr(AnenjiProtocol::operationModeName(3), "OffGrid", "mode name OffGrid");
  expectStr(AnenjiProtocol::operationModeName(2), "Mains", "mode name Mains");

  uint16_t insane[AnenjiProtocol::kLiveRegisterCount]{};
  for (size_t i = 0; i < 22; ++i) insane[i] = regs[i];
  insane[AnenjiProtocol::kOutputActivePower] = 20000;
  AnenjiProtocol::LiveReading clamped{};
  expect(AnenjiProtocol::decodeLiveBlock(insane, 22, clamped), "decode insane load");
  expectNear(clamped.loadPowerW, 0.0f, 0.01f, "clamp load above 5 kW to 0");

  uint8_t padded[sizeof(liveHigh) + 4] = {0x00, 0x02, 0x00, 0x02};
  for (size_t i = 0; i < sizeof(liveHigh); ++i) padded[i + 4] = liveHigh[i];
  expect(AnenjiProtocol::extractReadHoldingResponse(padded, sizeof(padded), 1, 22, regs, 22),
         "extract FC03 after junk prefix");
  expect(AnenjiProtocol::crcValid(AnenjiProtocol::kWakeupFrame, AnenjiProtocol::kWakeupSize),
         "wakeup 01 AA 06 DE A2 has Modbus CRC");

  if (failures) {
    std::printf("%d failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("all tests passed");
  return EXIT_SUCCESS;
}
