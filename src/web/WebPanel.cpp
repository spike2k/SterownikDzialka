#include "web/WebPanel.h"

#include <Arduino.h>
#include <cstring>
#include "AppConfig.h"
#include "core/Settings.h"
#include "web/PanelPage.h"

namespace {
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

void copyZ(char* dest, size_t size, const String& src) {
  strncpy(dest, src.c_str(), size - 1);
  dest[size - 1] = '\0';
}

int8_t pinArg(WebServer& server, const char* name, int8_t fallback) {
  if (!server.hasArg(name)) return fallback;
  const int value = server.arg(name).toInt();
  if (value < -1 || value > 39) return fallback;
  return static_cast<int8_t>(value);
}

String pinArrayJson(const int8_t* pins, size_t count) {
  String json = "[";
  for (size_t index = 0; index < count; ++index) {
    if (index) json += ',';
    json += String(pins[index]);
  }
  json += ']';
  return json;
}

String nameArrayJson(const char names[][Config::labelBytes], size_t count) {
  String json = "[";
  for (size_t index = 0; index < count; ++index) {
    if (index) json += ',';
    json += jsonEscape(names[index]);
  }
  json += ']';
  return json;
}

String loadsJson(const AppSettings& cfg) {
  String json = "[";
  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (index) json += ',';
    const auto& channel = cfg.loads[index];
    json += "{\"name\":" + jsonEscape(channel.name);
    json += ",\"mqttKey\":" + jsonEscape(channel.mqttKey);
    json += ",\"pin\":" + String(channel.pin);
    json += ",\"priority\":" + String(channel.priority);
    json += ",\"powerW\":" + String(channel.powerW) + "}";
  }
  json += ']';
  return json;
}

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void normalizeMqttKey(char* key) {
  for (char* cursor = key; *cursor; ++cursor) {
    if (*cursor >= 'A' && *cursor <= 'Z') *cursor = static_cast<char>(*cursor - 'A' + 'a');
  }
}
}

void WebPanel::begin(Telemetry& telemetry, RelayController& relays, NetworkService& network, Settings& settings,
                     InputMonitor& inputs) {
  telemetry_ = &telemetry;
  relays_ = &relays;
  network_ = &network;
  settings_ = &settings;
  inputs_ = &inputs;
  server_.on("/", HTTP_GET, [this]() { server_.send_P(200, "text/html; charset=utf-8", PanelHtml); });
  server_.on("/api/state", HTTP_GET, [this]() { handleState(); });
  server_.on("/api/settings", HTTP_GET, [this]() { handleSettingsGet(); });
  server_.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_.on("/api/mode", HTTP_POST, [this]() { handleMode(); });
  server_.on("/api/relay", HTTP_POST, [this]() { handleRelay(); });
  server_.on("/api/remote", HTTP_POST, [this]() { handleRemote(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.onNotFound([this]() { server_.send(404, "application/json", "{\"error\":\"not found\"}"); });
  server_.begin();
}

void WebPanel::tick() { server_.handleClient(); }
void WebPanel::handleState() { server_.send(200, "application/json", stateJson()); }
void WebPanel::handleSettingsGet() { server_.send(200, "application/json", settingsJson()); }

void WebPanel::handleMode() {
  relays_->setMode(server_.arg("value") == "manual" ? ControlMode::Manual : ControlMode::Auto);
  server_.send(204);
}

void WebPanel::handleRelay() {
  const bool accepted = relays_->setRelay(static_cast<size_t>(server_.arg("id").toInt()), server_.arg("state") == "1");
  server_.send(accepted ? 204 : 409, "application/json", accepted ? "" : "{\"error\":\"manual mode required\"}");
}

void WebPanel::handleRemote() {
  const bool enabled = server_.arg("state") == "1";
  bool accepted = false;
  if (server_.hasArg("device")) accepted = network_->sendRemoteMqtt(server_.arg("device"), enabled);
  if (server_.hasArg("url")) accepted = network_->sendRemoteHttp(server_.arg("url"), enabled);
  server_.send(accepted ? 204 : 503, "application/json", accepted ? "" : "{\"error\":\"remote unavailable\"}");
}

void WebPanel::handleReboot() {
  server_.send(200, "application/json", "{\"ok\":true}");
  delay(200);
  ESP.restart();
}

bool WebPanel::readPostedSettings(AppSettings& target, String& error) {
  copyZ(target.wifiSsid, sizeof(target.wifiSsid), server_.arg("wifiSsid"));
  if (server_.arg("wifiPassword").length()) {
    copyZ(target.wifiPassword, sizeof(target.wifiPassword), server_.arg("wifiPassword"));
  }
  copyZ(target.mqttHost, sizeof(target.mqttHost), server_.arg("mqttHost"));
  if (server_.arg("mqttPassword").length()) {
    copyZ(target.mqttPassword, sizeof(target.mqttPassword), server_.arg("mqttPassword"));
  }
  copyZ(target.mqttUser, sizeof(target.mqttUser), server_.arg("mqttUser"));
  int port = server_.hasArg("mqttPort") ? server_.arg("mqttPort").toInt() : target.mqttPort;
  if (port < 1 || port > 65535) port = 1883;
  target.mqttPort = static_cast<uint16_t>(port);

  for (size_t index = 0; index < Config::loadCount; ++index) {
    char nameId[20], pinId[20], mqttId[20], prioId[20], powerId[20];
    snprintf(nameId, sizeof(nameId), "loadName%u", static_cast<unsigned>(index));
    snprintf(pinId, sizeof(pinId), "loadPin%u", static_cast<unsigned>(index));
    snprintf(mqttId, sizeof(mqttId), "loadMqtt%u", static_cast<unsigned>(index));
    snprintf(prioId, sizeof(prioId), "loadPrio%u", static_cast<unsigned>(index));
    snprintf(powerId, sizeof(powerId), "loadPower%u", static_cast<unsigned>(index));
    LoadChannel& channel = target.loads[index];
    if (server_.hasArg(nameId)) copyZ(channel.name, Config::labelBytes, server_.arg(nameId));
    channel.pin = pinArg(server_, pinId, channel.pin);
    if (server_.hasArg(mqttId)) copyZ(channel.mqttKey, Config::labelBytes, server_.arg(mqttId));
    normalizeMqttKey(channel.mqttKey);
    if (server_.hasArg(prioId)) {
      channel.priority = static_cast<uint8_t>(clampInt(server_.arg(prioId).toInt(), 1, 10));
    }
    if (server_.hasArg(powerId)) {
      channel.powerW = static_cast<uint16_t>(clampInt(server_.arg(powerId).toInt(), 0, 30000));
    }
    if (channel.name[0] == '\0') {
      snprintf(channel.name, Config::labelBytes, "Odbiornik %u", static_cast<unsigned>(index + 1));
    }
  }
  if (server_.hasArg("surplusReserveW")) {
    target.surplusReserveW = static_cast<uint16_t>(clampInt(server_.arg("surplusReserveW").toInt(), 0, 5000));
  }
  if (server_.hasArg("loadHysteresisW")) {
    target.loadHysteresisW = static_cast<uint16_t>(clampInt(server_.arg("loadHysteresisW").toInt(), 0, 2000));
  }
  if (server_.hasArg("loadMinToggleMs")) {
    target.loadMinToggleMs = static_cast<uint16_t>(clampInt(server_.arg("loadMinToggleMs").toInt(), 0, 60000));
  }
  target.statusPins[0] = pinArg(server_, "status0", target.statusPins[0]);
  target.statusPins[1] = pinArg(server_, "status1", target.statusPins[1]);
  target.statusPins[2] = pinArg(server_, "status2", target.statusPins[2]);
  target.statusPins[3] = pinArg(server_, "status3", target.statusPins[3]);
  copyZ(target.statusNames[0], Config::labelBytes, server_.arg("statusName0"));
  copyZ(target.statusNames[1], Config::labelBytes, server_.arg("statusName1"));
  copyZ(target.statusNames[2], Config::labelBytes, server_.arg("statusName2"));
  copyZ(target.statusNames[3], Config::labelBytes, server_.arg("statusName3"));
  for (size_t index = 0; index < Config::statusCount; ++index) {
    if (target.statusNames[index][0] == '\0') {
      snprintf(target.statusNames[index], Config::labelBytes, "Wejscie %u", static_cast<unsigned>(index + 1));
    }
  }
  target.jkRxPin = pinArg(server_, "jkRx", target.jkRxPin);
  target.jkTxPin = pinArg(server_, "jkTx", target.jkTxPin);
  target.jkDePin = -1;
  target.anenjiRxPin = pinArg(server_, "anenjiRx", target.anenjiRxPin);
  target.anenjiTxPin = pinArg(server_, "anenjiTx", target.anenjiTxPin);
  target.pylonRxPin = pinArg(server_, "pylonRx", target.pylonRxPin);
  target.pylonTxPin = pinArg(server_, "pylonTx", target.pylonTxPin);
  target.pylonDePin = -1;
  if (server_.hasArg("relayActiveLow")) target.relayActiveLow = server_.arg("relayActiveLow") == "1";
  if (server_.hasArg("statusActiveLow")) target.statusActiveLow = server_.arg("statusActiveLow") == "1";
  if (server_.hasArg("debugJk")) target.debugJk = server_.arg("debugJk") == "1";
  if (server_.hasArg("debugAnenji")) target.debugAnenji = server_.arg("debugAnenji") == "1";
  if (server_.hasArg("debugPylon")) target.debugPylon = server_.arg("debugPylon") == "1";
  int drift = server_.hasArg("cellDriftAlarmMv") ? server_.arg("cellDriftAlarmMv").toInt() : target.cellDriftAlarmMv;
  if (drift < 5) drift = 5;
  if (drift > 500) drift = 500;
  target.cellDriftAlarmMv = static_cast<uint16_t>(drift);

  auto badOut = [&](int pin, const char* name) {
    if (!Settings::validOutputPin(pin)) error = String("Nieprawidlowy GPIO wyjscia: ") + name;
    return error.length() > 0;
  };
  auto badIn = [&](int pin, const char* name) {
    if (!Settings::validInputPin(pin)) error = String("Nieprawidlowy GPIO wejscia: ") + name;
    return error.length() > 0;
  };

  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (badOut(target.loads[index].pin, "odbiornik")) return false;
    if (!Settings::validMqttKey(target.loads[index].mqttKey)) {
      error = String("Nieprawidlowy identyfikator MQTT: ") + target.loads[index].mqttKey;
      return false;
    }
  }
  for (size_t index = 0; index < Config::statusCount; ++index) {
    if (badIn(target.statusPins[index], "wejscie stanu")) return false;
  }
  if (badIn(target.jkRxPin, "JK RX") || badOut(target.jkTxPin, "JK TX")) return false;
  if (badIn(target.anenjiRxPin, "falownik RX") || badOut(target.anenjiTxPin, "falownik TX")) return false;
  if (badIn(target.pylonRxPin, "Pylon RX") || badOut(target.pylonTxPin, "Pylon TX")) return false;
  return true;
}

void WebPanel::handleSettingsPost() {
  AppSettings next = settings_->values;
  String error;
  if (!readPostedSettings(next, error)) {
    server_.send(400, "application/json", "{\"error\":" + jsonEscape(error.c_str()) + "}");
    return;
  }
  Settings probe;
  probe.values = next;
  error = probe.pinConflict();
  if (error.length()) {
    server_.send(400, "application/json", "{\"error\":" + jsonEscape(error.c_str()) + "}");
    return;
  }
  error = probe.mqttKeyConflict();
  if (error.length()) {
    server_.send(400, "application/json", "{\"error\":" + jsonEscape(error.c_str()) + "}");
    return;
  }

  const bool wifiChanged = strcmp(next.wifiSsid, settings_->values.wifiSsid) != 0 ||
                           strcmp(next.wifiPassword, settings_->values.wifiPassword) != 0;
  const bool mqttChanged = strcmp(next.mqttHost, settings_->values.mqttHost) != 0 ||
                           next.mqttPort != settings_->values.mqttPort ||
                           strcmp(next.mqttUser, settings_->values.mqttUser) != 0 ||
                           strcmp(next.mqttPassword, settings_->values.mqttPassword) != 0;
  const bool commChanged = settings_->commPinsDiffer(next);
  bool loadPinsChanged = next.relayActiveLow != settings_->values.relayActiveLow;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (next.loads[index].pin != settings_->values.loads[index].pin) loadPinsChanged = true;
  }
  const bool statusChanged = memcmp(next.statusPins, settings_->values.statusPins, sizeof(next.statusPins)) != 0 ||
                             next.statusActiveLow != settings_->values.statusActiveLow;

  settings_->values = next;
  if (!settings_->save()) {
    server_.send(500, "application/json", "{\"error\":\"nie udalo sie zapisac NVS\"}");
    return;
  }
  if (wifiChanged) network_->applyWifi();
  if (mqttChanged) network_->applyMqtt();
  if (loadPinsChanged) relays_->reconfigure();
  if (statusChanged) inputs_->reconfigure();
  server_.send(200, "application/json", commChanged ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":true,\"reboot\":false}");
}

String WebPanel::settingsJson() const {
  const auto& cfg = settings_->values;
  String json;
  json.reserve(3200);
  json += "{\"wifiSsid\":" + jsonEscape(cfg.wifiSsid);
  json += ",\"wifiHasPassword\":" + String(cfg.wifiPassword[0] ? "true" : "false");
  json += ",\"mqttHost\":" + jsonEscape(cfg.mqttHost);
  json += ",\"mqttPort\":" + String(cfg.mqttPort);
  json += ",\"mqttUser\":" + jsonEscape(cfg.mqttUser);
  json += ",\"mqttHasPassword\":" + String(cfg.mqttPassword[0] ? "true" : "false");
  json += ",\"loads\":" + loadsJson(cfg);
  json += ",\"surplusReserveW\":" + String(cfg.surplusReserveW);
  json += ",\"loadHysteresisW\":" + String(cfg.loadHysteresisW);
  json += ",\"loadMinToggleMs\":" + String(cfg.loadMinToggleMs);
  json += ",\"statusPins\":" + pinArrayJson(cfg.statusPins, Config::statusCount);
  json += ",\"statusNames\":" + nameArrayJson(cfg.statusNames, Config::statusCount);
  json += ",\"relayActiveLow\":" + String(cfg.relayActiveLow ? "true" : "false");
  json += ",\"statusActiveLow\":" + String(cfg.statusActiveLow ? "true" : "false");
  json += ",\"debugJk\":" + String(cfg.debugJk ? "true" : "false");
  json += ",\"debugAnenji\":" + String(cfg.debugAnenji ? "true" : "false");
  json += ",\"debugPylon\":" + String(cfg.debugPylon ? "true" : "false");
  json += ",\"jkRx\":" + String(cfg.jkRxPin);
  json += ",\"jkTx\":" + String(cfg.jkTxPin);
  json += ",\"anenjiRx\":" + String(cfg.anenjiRxPin);
  json += ",\"anenjiTx\":" + String(cfg.anenjiTxPin);
  json += ",\"pylonRx\":" + String(cfg.pylonRxPin);
  json += ",\"pylonTx\":" + String(cfg.pylonTxPin);
  json += ",\"cellDriftAlarmMv\":" + String(cfg.cellDriftAlarmMv);
  json += ",\"defaults\":{";
  json += "\"loadPins\":[16,17,18,19,-1,-1,-1,-1,-1,-1],\"statusPins\":[-1,-1,-1,-1]";
  json += ",\"surplusReserveW\":100,\"loadHysteresisW\":80,\"loadMinToggleMs\":20000";
  json += ",\"relayActiveLow\":true,\"statusActiveLow\":true";
  json += ",\"jkRx\":27,\"jkTx\":26,\"anenjiRx\":33,\"anenjiTx\":32";
  json += ",\"pylonRx\":22,\"pylonTx\":23,\"cellDriftAlarmMv\":50}";
  json += "}";
  return json;
}

String WebPanel::stateJson() const {
  const bool healthy = telemetry_->jkOnline || telemetry_->anenjiOnline
                           ? millis() - telemetry_->updatedAtMs < Config::telemetryStaleMs
                           : false;
  const char* charge = "offline";
  float batteryW = 0;
  float solarCoverW = 0;
  float batteryCoverW = 0;
  float cellMinV = 0;
  float cellMaxV = 0;
  float cellDriftMv = 0;
  bool cellAlarm = false;

  if (telemetry_->jkOnline) {
    batteryW = telemetry_->batteryVoltageV * telemetry_->batteryCurrentA;
    if (telemetry_->batteryCurrentA > Config::chargeIdleAmps) charge = "charge";
    else if (telemetry_->batteryCurrentA < -Config::chargeIdleAmps) charge = "discharge";
    else charge = "idle";
    if (telemetry_->batteryCurrentA < -Config::chargeIdleAmps) batteryCoverW = -batteryW;
    if (telemetry_->cellCount > 0) {
      cellMinV = cellMaxV = telemetry_->cellVoltageV[0];
      for (size_t index = 1; index < telemetry_->cellCount; ++index) {
        const float volts = telemetry_->cellVoltageV[index];
        if (volts < cellMinV) cellMinV = volts;
        if (volts > cellMaxV) cellMaxV = volts;
      }
      cellDriftMv = (cellMaxV - cellMinV) * 1000.0f;
      cellAlarm = cellDriftMv >= settings_->values.cellDriftAlarmMv;
    }
  }
  if (telemetry_->anenjiOnline) {
    solarCoverW = telemetry_->pvPowerW < telemetry_->loadPowerW ? telemetry_->pvPowerW : telemetry_->loadPowerW;
    if (solarCoverW < 0) solarCoverW = 0;
  }

  String json;
  json.reserve(3600);
  json += "{\"pvW\":" + String(telemetry_->pvPowerW, 1);
  json += ",\"loadW\":" + String(telemetry_->loadPowerW, 1);
  json += ",\"soc\":" + String(telemetry_->batterySoc, 1);
  json += ",\"batteryV\":" + String(telemetry_->batteryVoltageV, 2);
  json += ",\"batteryA\":" + String(telemetry_->batteryCurrentA, 1);
  json += ",\"batteryW\":" + String(batteryW, 1);
  json += ",\"solarCoverW\":" + String(solarCoverW, 1);
  json += ",\"batteryCoverW\":" + String(batteryCoverW, 1);
  json += ",\"charge\":" + jsonEscape(charge);
  json += ",\"mode\":" + jsonEscape(relays_->mode() == ControlMode::Auto ? "auto" : "manual");
  json += ",\"healthy\":" + String(healthy ? "true" : "false");
  json += ",\"simulation\":false";
  json += ",\"ip\":" + jsonEscape(network_->ipAddress().c_str());
  json += ",\"ap\":" + String(network_->accessPointActive() ? "true" : "false");
  json += ",\"apSsid\":" + jsonEscape(Config::accessPointName);
  json += ",\"mqttHost\":" + jsonEscape(settings_->values.mqttHost);
  json += ",\"jk\":" + String(telemetry_->jkOnline ? "true" : "false");
  json += ",\"anenji\":" + String(telemetry_->anenjiOnline ? "true" : "false");
  json += ",\"pylon\":" + String(telemetry_->pylonOnline ? "true" : "false");
  json += ",\"wifi\":" + String(network_->wifiConnected() ? "true" : "false");
  json += ",\"mqtt\":" + String(network_->mqttConnected() ? "true" : "false");
  json += ",\"cellMinV\":" + String(cellMinV, 3);
  json += ",\"cellMaxV\":" + String(cellMaxV, 3);
  json += ",\"cellDriftMv\":" + String(cellDriftMv, 1);
  json += ",\"cellDriftAlarmMv\":" + String(settings_->values.cellDriftAlarmMv);
  json += ",\"cellAlarm\":" + String(cellAlarm ? "true" : "false");
  json += ",\"cells\":[";
  const size_t cellCount = telemetry_->jkOnline ? telemetry_->cellCount : 0;
  for (size_t index = 0; index < cellCount; ++index) {
    if (index) json += ',';
    json += String(telemetry_->cellVoltageV[index], 3);
  }
  json += "],\"loads\":[";
  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (index) json += ',';
    const auto& channel = settings_->values.loads[index];
    json += "{\"id\":" + String(index);
    json += ",\"on\":" + String(relays_->state(index) ? "true" : "false");
    json += ",\"pin\":" + String(channel.pin);
    json += ",\"mqttKey\":" + jsonEscape(channel.mqttKey);
    json += ",\"name\":" + jsonEscape(channel.name);
    json += ",\"priority\":" + String(channel.priority);
    json += ",\"powerW\":" + String(channel.powerW) + "}";
  }
  json += "],\"inputNames\":" + nameArrayJson(settings_->values.statusNames, Config::statusCount);
  json += ",\"inputs\":[";
  for (size_t index = 0; index < Config::statusCount; ++index) {
    if (index) json += ',';
    json += "{\"pin\":" + String(inputs_->pin(index));
    json += ",\"on\":" + String(inputs_->active(index) ? "true" : "false") + "}";
  }
  json += "]}";
  return json;
}
