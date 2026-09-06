#include "core/TelemetryJson.h"

#include <cstdio>
#include "drivers/AnenjiProtocol.h"

namespace {
void appendU16Array(String& json, const uint16_t* values, size_t count) {
  json += '[';
  for (size_t index = 0; index < count; ++index) {
    if (index) json += ',';
    json += String(values[index]);
  }
  json += ']';
}

void appendFloatArray(String& json, const float* values, size_t count, unsigned int decimals) {
  json += '[';
  for (size_t index = 0; index < count; ++index) {
    if (index) json += ',';
    json += String(values[index], decimals);
  }
  json += ']';
}

void formatRuntime(uint32_t seconds, char* out, size_t outSize) {
  const uint32_t days = seconds / 86400U;
  seconds %= 86400U;
  const uint32_t hours = seconds / 3600U;
  seconds %= 3600U;
  const uint32_t minutes = seconds / 60U;
  seconds %= 60U;
  snprintf(out, outSize, "%lud %02lu:%02lu:%02lu", static_cast<unsigned long>(days),
           static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}
}

String jsonEscape(const char* text) {
  String out = "\"";
  if (!text) return out + "\"";
  for (const char* cursor = text; *cursor; ++cursor) {
    if (*cursor == '"' || *cursor == '\\') out += '\\';
    if (*cursor == '\n') {
      out += "\\n";
      continue;
    }
    out += *cursor;
  }
  out += '"';
  return out;
}

void appendInverterJson(String& json, const Telemetry& telemetry) {
  const auto& inv = telemetry.inverter;
  json += "{\"online\":";
  json += telemetry.anenjiOnline ? "true" : "false";
  if (!telemetry.anenjiOnline) {
    json += '}';
    return;
  }
  json += ",\"mode\":";
  json += jsonEscape(AnenjiProtocol::operationModeName(inv.operationMode));
  json += ",\"modeId\":" + String(inv.operationMode);
  json += ",\"flags\":" + String(inv.flags);
  json += ",\"mainsV\":" + String(inv.mainsVoltageV, 1);
  json += ",\"mainsHz\":" + String(inv.mainsFrequencyHz, 2);
  json += ",\"mainsW\":" + String(inv.mainsPowerW, 0);
  json += ",\"inverterV\":" + String(inv.inverterVoltageV, 1);
  json += ",\"inverterA\":" + String(inv.inverterCurrentA, 1);
  json += ",\"inverterHz\":" + String(inv.inverterFrequencyHz, 2);
  json += ",\"inverterW\":" + String(inv.inverterPowerW, 0);
  json += ",\"inverterChargeW\":" + String(inv.inverterChargePowerW, 0);
  json += ",\"outputV\":" + String(inv.outputVoltageV, 1);
  json += ",\"outputA\":" + String(inv.outputCurrentA, 1);
  json += ",\"outputHz\":" + String(inv.outputFrequencyHz, 2);
  json += ",\"loadW\":" + String(inv.loadPowerW, 0);
  json += ",\"loadVa\":" + String(inv.outputApparentPowerVa, 0);
  json += ",\"loadPercent\":" + String(inv.loadPercent);
  json += ",\"batteryV\":" + String(inv.batteryVoltageV, 1);
  json += ",\"batteryA\":" + String(inv.batteryCurrentA, 1);
  json += ",\"batteryW\":" + String(inv.batteryPowerW, 0);
  json += ",\"batteryA2\":" + String(inv.batteryCurrent2A, 1);
  json += ",\"batterySoc\":" + String(inv.batterySocPercent);
  json += ",\"dcBusV\":" + String(inv.dcBusVoltageV, 1);
  json += ",\"pvV\":" + String(inv.pvVoltageV, 1);
  json += ",\"pvA\":" + String(inv.pvCurrentA, 1);
  json += ",\"pvW\":" + String(inv.pvPowerW, 0);
  json += ",\"pvChargeW\":" + String(inv.pvChargePowerW, 0);
  json += ",\"dcdcC\":" + String(inv.dcdcTemperatureC, 0);
  json += ",\"inverterC\":" + String(inv.inverterTemperatureC, 0);
  json += ",\"statusOk\":";
  json += inv.statusOk ? "true" : "false";
  json += ",\"liveRegs\":";
  appendU16Array(json, inv.liveRegs, AnenjiProtocol::kLiveRegisterCount);
  json += ",\"statusRegs\":";
  appendU16Array(json, inv.statusRegs, AnenjiProtocol::kStatusRegisterCount);
  json += '}';
}

void appendBmsJson(String& json, const Telemetry& telemetry) {
  const auto& bms = telemetry.bms;
  json += "{\"online\":";
  json += telemetry.jkOnline ? "true" : "false";
  json += ",\"mac\":" + jsonEscape(telemetry.jkMac);
  json += ",\"model\":" + jsonEscape(telemetry.jkModel);
  json += ",\"hw\":" + jsonEscape(telemetry.jkHardware);
  json += ",\"sw\":" + jsonEscape(telemetry.jkSoftware);
  json += ",\"protocol\":" + jsonEscape(batteryProtocolName(bms.protocol));
  json += ",\"validFrames\":" + String(telemetry.jkValidFrames);
  json += ",\"invalidFrames\":" + String(telemetry.jkInvalidFrames);
  if (!telemetry.jkOnline) {
    json += '}';
    return;
  }
  json += ",\"packV\":" + String(bms.packVoltageV, 3);
  json += ",\"currentA\":" + String(bms.currentA, 3);
  json += ",\"powerW\":" + String(bms.powerW, 1);
  json += ",\"soc\":" + String(bms.socPercent, 0);
  json += ",\"soh\":" + String(bms.sohPercent);
  json += ",\"remainingAh\":" + String(bms.remainingCapacityAh, 3);
  json += ",\"fullAh\":" + String(bms.fullCapacityAh, 3);
  json += ",\"cycles\":" + String(bms.cycleCount);
  json += ",\"cycleAh\":" + String(bms.cycleCapacityAh, 3);
  json += ",\"runtimeS\":" + String(bms.runtimeSeconds);
  char runtime[24];
  formatRuntime(bms.runtimeSeconds, runtime, sizeof(runtime));
  json += ",\"runtime\":" + jsonEscape(runtime);
  json += ",\"mosC\":" + String(bms.mosTemperatureC, 1);
  json += ",\"tempsC\":";
  appendFloatArray(json, bms.temperaturesC.data(), bms.temperatureCount, 1);
  json += ",\"chargeMos\":";
  json += bms.chargeMosOn ? "true" : "false";
  json += ",\"dischargeMos\":";
  json += bms.dischargeMosOn ? "true" : "false";
  json += ",\"precharge\":";
  json += bms.prechargeOn ? "true" : "false";
  json += ",\"heating\":";
  json += bms.heatingOn ? "true" : "false";
  json += ",\"balancing\":";
  json += bms.balancing ? "true" : "false";
  json += ",\"balancerStatus\":" + String(bms.balancerStatus);
  json += ",\"balanceA\":" + String(bms.balancingCurrentA, 3);
  json += ",\"alarms\":" + String(bms.alarms);
  char alarmHex[11];
  snprintf(alarmHex, sizeof(alarmHex), "0x%lX", static_cast<unsigned long>(bms.alarms));
  json += ",\"alarmsHex\":" + jsonEscape(alarmHex);
  json += ",\"cellMinV\":" + String(bms.minCellVoltageV, 3);
  json += ",\"cellMaxV\":" + String(bms.maxCellVoltageV, 3);
  json += ",\"cellDeltaV\":" + String(bms.deltaCellVoltageV, 3);
  json += ",\"cellAvgV\":" + String(bms.averageCellVoltageV, 3);
  json += ",\"cellMin\":" + String(bms.minCellNumber);
  json += ",\"cellMax\":" + String(bms.maxCellNumber);
  json += ",\"cells\":";
  appendFloatArray(json, bms.cellVoltageV.data(), bms.cellCount, 3);
  json += ",\"resistances\":";
  appendFloatArray(json, bms.cellResistanceOhm.data(), bms.cellCount, 3);
  json += '}';
}

String mqttTelemetryJson(const Telemetry& telemetry) {
  String json;
  json.reserve(2800);
  json += "{\"pvW\":" + String(telemetry.pvPowerW, 0);
  json += ",\"loadW\":" + String(telemetry.loadPowerW, 0);
  json += ",\"soc\":" + String(telemetry.batterySoc, 1);
  json += ",\"batteryV\":" + String(telemetry.batteryVoltageV, 2);
  json += ",\"batteryA\":" + String(telemetry.batteryCurrentA, 1);
  json += ",\"jk\":";
  json += telemetry.jkOnline ? "true" : "false";
  json += ",\"anenji\":";
  json += telemetry.anenjiOnline ? "true" : "false";
  json += ",\"pylon\":";
  json += telemetry.pylonOnline ? "true" : "false";
  json += ",\"inverter\":";
  appendInverterJson(json, telemetry);
  json += ",\"bms\":";
  appendBmsJson(json, telemetry);
  json += '}';
  return json;
}
