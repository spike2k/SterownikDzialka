#pragma once

#include <Arduino.h>
#include "AppConfig.h"

struct LoadChannel {
  char name[Config::labelBytes] = {};
  char mqttKey[Config::labelBytes] = {};
  int8_t pin = -1;
  uint8_t priority = 1;
  uint16_t powerW = 0;
};

struct AppSettings {
  uint32_t magic = 0;
  uint16_t version = 0;
  char wifiSsid[33] = "";
  char wifiPassword[65] = "";
  char mqttHost[65] = "";
  uint16_t mqttPort = Config::defaultMqttPort;
  char mqttUser[33] = "";
  char mqttPassword[65] = "";
  int8_t relayPins[Config::relayCount] = {
      Config::defaultRelayPins[0], Config::defaultRelayPins[1], Config::defaultRelayPins[2],
      Config::defaultRelayPins[3]};
  int8_t statusPins[Config::statusCount] = {
      Config::defaultStatusPins[0], Config::defaultStatusPins[1], Config::defaultStatusPins[2],
      Config::defaultStatusPins[3]};
  int8_t jkRxPin = Config::jkRxPin;
  int8_t jkTxPin = Config::jkTxPin;
  int8_t jkDePin = -1;
  int8_t anenjiRxPin = Config::anenjiRxPin;
  int8_t anenjiTxPin = Config::anenjiTxPin;
  int8_t pylonRxPin = Config::pylonRxPin;
  int8_t pylonTxPin = Config::pylonTxPin;
  int8_t pylonDePin = -1;
  bool relayActiveLow = Config::defaultRelayActiveLow;
  bool statusActiveLow = Config::defaultStatusActiveLow;
  uint16_t cellDriftAlarmMv = Config::defaultCellDriftAlarmMv;
  char relayNames[Config::relayCount][Config::labelBytes] = {};
  char statusNames[Config::statusCount][Config::labelBytes] = {};
  bool debugJk = false;
  bool debugAnenji = false;
  bool debugPylon = false;
  LoadChannel loads[Config::loadCount] = {};
  uint16_t surplusReserveW = Config::defaultSurplusReserveW;
  uint16_t loadHysteresisW = Config::defaultLoadHysteresisW;
  uint16_t loadMinToggleMs = Config::defaultLoadMinToggleMs;
};

class Settings {
 public:
  AppSettings values;

  void begin();
  void loadDefaults();
  void fillDefaultNames();
  bool save();
  bool commPinsDiffer(const AppSettings& other) const;
  String pinConflict() const;
  String mqttKeyConflict() const;
  static bool loadActive(const LoadChannel& channel);
  static bool validMqttKey(const char* key);
  static bool validOutputPin(int pin);
  static bool validInputPin(int pin);

 private:
  bool load();
  void migrateLoadsFromRelays();
  void syncLegacyRelays();
};
