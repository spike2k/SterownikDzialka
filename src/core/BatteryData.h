#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t BatteryMaxCells = 32;
constexpr size_t BatteryMaxTemperatures = 5;

enum class BatteryProtocol : uint8_t {
  Unknown,
  Jk02_24S,
  Jk02_32S,
  Jk04,
};

struct BatteryData {
  float packVoltageV = 0.0f;
  float currentA = 0.0f;
  float powerW = 0.0f;
  float socPercent = 0.0f;
  std::array<float, BatteryMaxTemperatures> temperaturesC{};
  size_t temperatureCount = 0;
  float mosTemperatureC = 0.0f;
  std::array<float, BatteryMaxCells> cellVoltageV{};
  std::array<float, BatteryMaxCells> cellResistanceOhm{};
  size_t cellCount = 0;
  float minCellVoltageV = 0.0f;
  float maxCellVoltageV = 0.0f;
  float deltaCellVoltageV = 0.0f;
  float averageCellVoltageV = 0.0f;
  uint8_t minCellNumber = 0;
  uint8_t maxCellNumber = 0;
  float balancingCurrentA = 0.0f;
  uint8_t balancerStatus = 0;
  float remainingCapacityAh = 0.0f;
  float fullCapacityAh = 0.0f;
  uint32_t cycleCount = 0;
  float cycleCapacityAh = 0.0f;
  uint8_t sohPercent = 0;
  bool chargeMosOn = false;
  bool dischargeMosOn = false;
  bool prechargeOn = false;
  bool heatingOn = false;
  bool balancing = false;
  uint32_t runtimeSeconds = 0;
  uint32_t alarms = 0;
  uint32_t lastUpdateMs = 0;
  bool online = false;
  BatteryProtocol protocol = BatteryProtocol::Unknown;
};

inline const char* batteryProtocolName(BatteryProtocol protocol) {
  switch (protocol) {
    case BatteryProtocol::Jk02_24S: return "JK02_24S";
    case BatteryProtocol::Jk02_32S: return "JK02_32S";
    case BatteryProtocol::Jk04: return "JK04";
    default: return "UNKNOWN";
  }
}
