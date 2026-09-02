#pragma once

#include <cstddef>
#include <cstdint>

// Native Modbus RTU of ANENJI ANJ-4200W-24V (ISolar/EASUN SMG II / POW-HVM family).
// Confirmed by sniffing the factory WiFi Plug Pro and matching
// https://github.com/syssi/esphome-smg-ii : 9600 8N1, slave 1, FC03 only.
// No write helpers — registers 300+ are R/W on the bus; this firmware never sends FC06/FC10.
namespace AnenjiProtocol {
constexpr uint8_t kSlave = 1;
constexpr uint8_t kReadHolding = 0x03;
constexpr uint16_t kFaultFirstRegister = 100;     // dongle starts here
constexpr uint16_t kFaultRegisterCount = 3;
constexpr uint16_t kIdentityFirstRegister = 171;  // serial / device info
constexpr uint16_t kIdentityRegisterCount = 28;
constexpr uint16_t kLiveFirstRegister = 200;  // dongle: 01 03 00 C8 00 16
constexpr uint16_t kLiveRegisterCount = 22;   // 200..221
constexpr uint16_t kStatusFirstRegister = 223;  // dongle: 01 03 00 DF 00 0D
constexpr uint16_t kStatusRegisterCount = 13;   // 223..235
constexpr size_t kRequestSize = 8;
constexpr size_t kWakeupSize = 5;
// Proprietary SMG-II / Anenji wakeup (esphome-smg-ii). Not FC06/FC10.
inline constexpr uint8_t kWakeupFrame[kWakeupSize] = {0x01, 0xAA, 0x06, 0xDE, 0xA2};
constexpr size_t kLivePayloadBytes = kLiveRegisterCount * 2;
constexpr size_t kLiveResponseSize = 5 + kLivePayloadBytes;  // 49
constexpr size_t kStatusPayloadBytes = kStatusRegisterCount * 2;
constexpr size_t kStatusResponseSize = 5 + kStatusPayloadBytes;  // 31
constexpr float kMaxPlausiblePowerW = 5000.0f;

// Offsets inside the 200..221 block (dongle read of 22 holding registers).
enum LiveOffset : uint16_t {
  kFaultOrFlags = 0,           // 200 undocumented (dongle starts here; 0xB000 in sniffs)
  kOperationMode = 1,          // 201: 0 PowerOn, 1 Standby, 2 Mains, 3 OffGrid, 4 Bypass, 5 Charging, 6 Fault
  kMainsVoltage = 2,           // 202 × 0.1 V
  kMainsFrequency = 3,         // 203 × 0.01 Hz
  kMainsPower = 4,             // 204 W
  kInverterVoltage = 5,        // 205 × 0.1 V
  kInverterCurrent = 6,        // 206 × 0.1 A
  kInverterFrequency = 7,      // 207 × 0.01 Hz
  kInverterPower = 8,          // 208 W (inverter bridge, not house load)
  kInverterChargePower = 9,    // 209 W
  kOutputVoltage = 10,         // 210 × 0.1 V
  kOutputCurrent = 11,         // 211 × 0.1 A
  kOutputFrequency = 12,       // 212 × 0.01 Hz
  kOutputActivePower = 13,     // 213 W  ← house load
  kOutputApparentPower = 14,   // 214 VA
  kBatteryVoltage = 15,        // 215 × 0.1 V
  kBatteryCurrent = 16,        // 216 × 0.1 A signed (neg = discharge)
  kBatteryPower = 17,          // 217 W signed
  kDcBusVoltage = 18,          // 218 undocumented; sniffs show ~360 V DC bus
  kPvVoltage = 19,             // 219 × 0.1 V
  kPvCurrent = 20,             // 220 × 0.1 A
};

// Offsets inside the 223..235 block.
enum StatusOffset : uint16_t {
  kPvPower = 0,             // 223 W  ← PV production
  kPvChargePower = 1,       // 224 W
  kLoadPercent = 2,         // 225 % of rated output
  kDcdcTemperature = 3,     // 226 °C
  kInverterTemperature = 4, // 227 °C
  kBatterySoc = 6,          // 229 %
  kBatteryCurrent2 = 9,     // 232 × 0.1 A signed (same value as 216 in sniffs)
};

struct LiveReading {
  uint16_t operationMode = 0;
  float outputVoltageV = 0;
  float inverterPowerW = 0;
  float loadPowerW = 0;
  float batteryVoltageV = 0;
  float batteryCurrentA = 0;
  float pvVoltageV = 0;
  float pvPowerW = 0;
  uint16_t loadPercent = 0;
};

uint16_t crc16(const uint8_t* data, size_t length);
bool crcValid(const uint8_t* frame, size_t length);

// Builds an 8-byte FC03 request. Returns false if arguments are not a legal read.
bool buildReadHolding(uint8_t* out, size_t outSize, uint8_t slave, uint16_t firstRegister,
                      uint16_t registerCount);

bool parseReadHoldingResponse(const uint8_t* frame, size_t length, uint8_t expectedSlave,
                              uint16_t expectedCount, uint16_t* registers, size_t registerCapacity);

// Same as parse, but skips leading junk until a CRC-valid FC03 frame is found.
bool extractReadHoldingResponse(const uint8_t* data, size_t length, uint8_t expectedSlave,
                                uint16_t expectedCount, uint16_t* registers, size_t registerCapacity);

bool decodeLiveBlock(const uint16_t* registers, size_t count, LiveReading& reading);
bool decodeStatusBlock(const uint16_t* registers, size_t count, LiveReading& reading);
const char* operationModeName(uint16_t mode);
}
