#include "core/Settings.h"

#include <Preferences.h>
#include <string.h>

namespace {
constexpr uint32_t kMagic = 0x314D5345;
constexpr uint16_t kVersion = 4;
Preferences prefs;

void terminate(char* text, size_t size) {
  if (size == 0) return;
  text[size - 1] = '\0';
}
}

void Settings::begin() {
  prefs.begin("ems", false);
  if (load()) return;
  loadDefaults();
  if (EMS_WIFI_SSID[0] != '\0') {
    strncpy(values.wifiSsid, EMS_WIFI_SSID, sizeof(values.wifiSsid) - 1);
    strncpy(values.wifiPassword, EMS_WIFI_PASSWORD, sizeof(values.wifiPassword) - 1);
  }
  if (EMS_MQTT_HOST[0] != '\0') {
    strncpy(values.mqttHost, EMS_MQTT_HOST, sizeof(values.mqttHost) - 1);
    values.mqttPort = EMS_MQTT_PORT;
    strncpy(values.mqttUser, EMS_MQTT_USER, sizeof(values.mqttUser) - 1);
    strncpy(values.mqttPassword, EMS_MQTT_PASSWORD, sizeof(values.mqttPassword) - 1);
  }
  save();
}

void Settings::loadDefaults() {
  values = AppSettings{};
  values.magic = kMagic;
  values.version = kVersion;
  fillDefaultNames();
  syncLegacyRelays();
}

void Settings::fillDefaultNames() {
  for (size_t index = 0; index < Config::relayCount; ++index) {
    snprintf(values.relayNames[index], Config::labelBytes, "Przekaznik %u", static_cast<unsigned>(index + 1));
  }
  for (size_t index = 0; index < Config::statusCount; ++index) {
    snprintf(values.statusNames[index], Config::labelBytes, "Wejscie %u", static_cast<unsigned>(index + 1));
  }
  for (size_t index = 0; index < Config::loadCount; ++index) {
    LoadChannel& channel = values.loads[index];
    if (index < Config::relayCount) {
      snprintf(channel.name, Config::labelBytes, "Przekaznik %u", static_cast<unsigned>(index + 1));
      channel.pin = static_cast<int8_t>(Config::defaultRelayPins[index]);
    } else {
      snprintf(channel.name, Config::labelBytes, "Odbiornik %u", static_cast<unsigned>(index + 1));
      channel.pin = -1;
    }
    channel.mqttKey[0] = '\0';
    channel.priority = static_cast<uint8_t>(index + 1);
    channel.powerW = 0;
  }
}

void Settings::migrateLoadsFromRelays() {
  for (size_t index = 0; index < Config::loadCount; ++index) {
    LoadChannel& channel = values.loads[index];
    channel.mqttKey[0] = '\0';
    channel.priority = static_cast<uint8_t>(index + 1);
    channel.powerW = 0;
    if (index < Config::relayCount) {
      strncpy(channel.name, values.relayNames[index], Config::labelBytes - 1);
      channel.name[Config::labelBytes - 1] = '\0';
      if (channel.name[0] == '\0') {
        snprintf(channel.name, Config::labelBytes, "Przekaznik %u", static_cast<unsigned>(index + 1));
      }
      channel.pin = values.relayPins[index];
    } else {
      snprintf(channel.name, Config::labelBytes, "Odbiornik %u", static_cast<unsigned>(index + 1));
      channel.pin = -1;
    }
  }
  values.surplusReserveW = Config::defaultSurplusReserveW;
  values.loadHysteresisW = Config::defaultLoadHysteresisW;
  values.loadMinToggleMs = Config::defaultLoadMinToggleMs;
  syncLegacyRelays();
}

void Settings::syncLegacyRelays() {
  for (size_t index = 0; index < Config::relayCount; ++index) {
    values.relayPins[index] = values.loads[index].pin;
    strncpy(values.relayNames[index], values.loads[index].name, Config::labelBytes - 1);
    values.relayNames[index][Config::labelBytes - 1] = '\0';
  }
}

bool Settings::load() {
  AppSettings blob{};
  const size_t n = prefs.getBytes("cfg", &blob, sizeof(blob));
  if (n < 8 || blob.magic != kMagic) return false;
  values = blob;
  terminate(values.wifiSsid, sizeof(values.wifiSsid));
  terminate(values.wifiPassword, sizeof(values.wifiPassword));
  terminate(values.mqttHost, sizeof(values.mqttHost));
  terminate(values.mqttUser, sizeof(values.mqttUser));
  terminate(values.mqttPassword, sizeof(values.mqttPassword));
  terminate(values.jkBmsMac, sizeof(values.jkBmsMac));
  for (size_t index = 0; index < Config::relayCount; ++index) {
    terminate(values.relayNames[index], Config::labelBytes);
  }
  for (size_t index = 0; index < Config::statusCount; ++index) {
    terminate(values.statusNames[index], Config::labelBytes);
  }
  for (size_t index = 0; index < Config::loadCount; ++index) {
    terminate(values.loads[index].name, Config::labelBytes);
    terminate(values.loads[index].mqttKey, Config::labelBytes);
  }
  values.jkDePin = -1;
  values.pylonDePin = -1;
  const uint16_t storedVersion = values.version;
  if (storedVersion < 2) {
    fillDefaultNames();
    values.debugJk = false;
    values.debugAnenji = false;
    values.debugPylon = false;
  }
  if (storedVersion < 3) {
    migrateLoadsFromRelays();
  }
  if (storedVersion < 4) {
    strncpy(values.jkBmsMac, EMS_JK_BMS_MAC, sizeof(values.jkBmsMac) - 1);
    values.jkBmsMac[sizeof(values.jkBmsMac) - 1] = '\0';
  }
  if (storedVersion < kVersion) {
    values.version = kVersion;
    save();
  }
  return true;
}

bool Settings::save() {
  values.magic = kMagic;
  values.version = kVersion;
  values.jkDePin = -1;
  values.pylonDePin = -1;
  syncLegacyRelays();
  return prefs.putBytes("cfg", &values, sizeof(values)) == sizeof(values);
}

bool Settings::commPinsDiffer(const AppSettings& other) const {
  return strcmp(values.jkBmsMac, other.jkBmsMac) != 0 || values.anenjiRxPin != other.anenjiRxPin ||
         values.anenjiTxPin != other.anenjiTxPin ||
         values.pylonRxPin != other.pylonRxPin || values.pylonTxPin != other.pylonTxPin;
}

bool Settings::loadActive(const LoadChannel& channel) {
  return channel.pin >= 0 || channel.mqttKey[0] != '\0';
}

bool Settings::validMqttKey(const char* key) {
  if (!key || key[0] == '\0') return true;
  size_t length = 0;
  for (const char* cursor = key; *cursor; ++cursor, ++length) {
    const char c = *cursor;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok || length >= Config::labelBytes - 1) return false;
  }
  return length > 0;
}

String Settings::mqttKeyConflict() const {
  for (size_t index = 0; index < Config::loadCount; ++index) {
    const char* key = values.loads[index].mqttKey;
    if (key[0] == '\0') continue;
    if (!validMqttKey(key)) return String("Nieprawidlowy identyfikator MQTT: ") + key;
    for (size_t other = index + 1; other < Config::loadCount; ++other) {
      if (values.loads[other].mqttKey[0] != '\0' && strcmp(key, values.loads[other].mqttKey) == 0) {
        return String("Powtorzony identyfikator MQTT: ") + key;
      }
    }
  }
  return String();
}

String Settings::pinConflict() const {
  int used[32];
  const char* labels[32];
  size_t count = 0;

  auto add = [&](int pin, const char* label) {
    if (pin < 0 || count >= 32) return;
    used[count] = pin;
    labels[count] = label;
    ++count;
  };

  char loadLabels[Config::loadCount][24];
  for (size_t index = 0; index < Config::loadCount; ++index) {
    snprintf(loadLabels[index], sizeof(loadLabels[index]), "odbiornik %u", static_cast<unsigned>(index + 1));
    add(values.loads[index].pin, loadLabels[index]);
  }
  add(values.statusPins[0], "wejscie 1");
  add(values.statusPins[1], "wejscie 2");
  add(values.statusPins[2], "wejscie 3");
  add(values.statusPins[3], "wejscie 4");
  add(values.anenjiRxPin, "falownik RX");
  add(values.anenjiTxPin, "falownik TX");
  add(values.pylonRxPin, "Pylon RX");
  add(values.pylonTxPin, "Pylon TX");

  for (size_t i = 0; i < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      if (used[i] == used[j]) {
        return String("GPIO ") + String(used[i]) + " uzyte przez " + labels[i] + " i " + labels[j];
      }
    }
  }
  return String();
}

bool Settings::validOutputPin(int pin) {
  if (pin < 0) return true;
  if (pin > 39 || (pin >= 6 && pin <= 11) || pin >= 34) return false;
  return true;
}

bool Settings::validInputPin(int pin) {
  if (pin < 0) return true;
  if (pin > 39 || (pin >= 6 && pin <= 11)) return false;
  return true;
}
