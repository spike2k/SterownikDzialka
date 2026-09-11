#include "services/NetworkService.h"

#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "AppConfig.h"
#include "TlsCertificates.h"
#include "core/TelemetryJson.h"

namespace {
constexpr time_t kMinimumValidTime = 1704067200;
constexpr char kOnline[] = "online";
constexpr char kOffline[] = "offline";

bool validSha256(const char* value, size_t length) {
  if (length != 64) return false;
  for (size_t index = 0; index < length; ++index) {
    if (!isHexadecimalDigit(value[index])) return false;
  }
  return true;
}
}

void NetworkService::onWifiEvent(WiFiEvent_t event) {
  if (event != ARDUINO_EVENT_WIFI_STA_GOT_IP) return;
  Serial.println();
  Serial.print("Panel WWW: http://");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void NetworkService::begin(RelayController& relays, Settings& settings) {
  relays_ = &relays;
  settings_ = &settings;
  wifiStartedMs_ = millis();
  WiFi.persistent(false);
  WiFi.setHostname(Config::deviceName);
  WiFi.onEvent(onWifiEvent);
  mqttTlsClient_.setCACert(TlsCertificates::letsEncryptRootX1);
  mqttTlsClient_.setHandshakeTimeout(Config::mqttSocketTimeoutSeconds);
  mqtt_.setKeepAlive(Config::mqttKeepAliveSeconds);
  mqtt_.setSocketTimeout(Config::mqttSocketTimeoutSeconds);
  applyMqtt();
  mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
    onMqtt(topic, payload, length);
  });
  mqtt_.setBufferSize(1536);
  connectWifi();
}

void NetworkService::tick(const Telemetry& telemetry) {
  if (!settings_) return;

  if (!wifiConnected()) {
    staOkSinceMs_ = 0;
    if (millis() - lastWifiAttemptMs_ >= Config::wifiRetryMs) connectWifi();
    if (!apEnabled_ && settings_->values.wifiSsid[0] != '\0' &&
        millis() - wifiStartedMs_ >= Config::wifiApFallbackMs) {
      startAccessPoint();
      connectWifi();
    }
  } else {
    if (staOkSinceMs_ == 0) staOkSinceMs_ = millis();
    if (apEnabled_ && settings_->values.wifiSsid[0] != '\0' && millis() - staOkSinceMs_ > 60000) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      apEnabled_ = false;
    }
  }

  if (wifiConnected() && pendingOtaSha256_[0] != '\0') {
    handlePendingOta();
    return;
  }

  if (wifiConnected() && settings_->values.mqttHost[0] != '\0') {
    if (!mqtt_.connected() && millis() - lastMqttAttemptMs_ >= Config::mqttRetryMs) connectMqtt();
    mqtt_.loop();
    if (mqtt_.connected()) {
      updateSunMode(telemetry.pvPowerW);
      const uint32_t now = millis();
      const bool refreshLoads =
          lastMqttLoadRefreshMs_ == 0 || now - lastMqttLoadRefreshMs_ >= currentLoadRefreshMs();
      const bool loadChanged = publishLoadCommands(refreshLoads);
      const bool controlChanged = publishControlState(false);
      if (refreshLoads) lastMqttLoadRefreshMs_ = now;
      if (loadChanged || controlChanged) startBurst();

      const bool dueByInterval =
          lastMqttPublishMs_ == 0 || now - lastMqttPublishMs_ >= currentPublishIntervalMs();
      const bool dueByDelta = significantTelemetryChange(telemetry);
      if (dueByDelta) startBurst();

      if (stateGetRequested_ || loadChanged || controlChanged || dueByInterval || dueByDelta) {
        publish(telemetry);
        rememberPublished(telemetry);
        lastMqttPublishMs_ = now;
        stateGetRequested_ = false;
      }
    }
  }
}

void NetworkService::applyWifi() {
  disconnectMqtt();
  WiFi.disconnect(false, true);
  apEnabled_ = false;
  wifiStartedMs_ = millis();
  staOkSinceMs_ = 0;
  connectWifi();
}

void NetworkService::applyMqtt() {
  if (!settings_) return;
  disconnectMqtt();
  mqtt_.setServer(settings_->values.mqttHost, settings_->values.mqttPort);
  lastMqttAttemptMs_ = 0;
}

bool NetworkService::wifiConnected() const { return WiFi.status() == WL_CONNECTED; }
bool NetworkService::mqttConnected() { return mqtt_.connected(); }
bool NetworkService::accessPointActive() const { return apEnabled_ || (WiFi.getMode() & WIFI_MODE_AP); }
String NetworkService::stationIp() const { return wifiConnected() ? WiFi.localIP().toString() : String(); }
String NetworkService::apIp() const { return accessPointActive() ? WiFi.softAPIP().toString() : String(); }

String NetworkService::ipAddress() const {
  String text;
  const String sta = stationIp();
  const String ap = apIp();
  if (sta.length()) text = sta;
  if (ap.length()) {
    if (text.length()) text += " / ";
    text += ap;
  }
  return text.length() ? text : String("offline");
}

bool NetworkService::sendRemoteMqtt(const String& device, bool enabled) {
  if (!mqtt_.connected() || device.isEmpty() || !Settings::validMqttKey(device.c_str())) return false;
  const String legacyTopic = "ems/remote/" + device + "/set";
  const String satelliteTopic = String(Config::mqttSatelliteCommandPrefix) + device +
                                Config::mqttSatelliteCommandSuffix;
  const String openBekenTopic = device + "/0/set";
  const bool legacyPublished = mqtt_.publish(legacyTopic.c_str(), enabled ? "ON" : "OFF", false);
  const bool satellitePublished = mqtt_.publish(satelliteTopic.c_str(), enabled ? "ON" : "OFF", false);
  const bool openBekenPublished = mqtt_.publish(openBekenTopic.c_str(), enabled ? "1" : "0", false);
  return legacyPublished || satellitePublished || openBekenPublished;
}

bool NetworkService::sendRemoteHttp(const String& url, bool enabled) {
  if (!wifiConnected() || !url.startsWith("http")) return false;
  HTTPClient request;
  request.begin(url);
  request.addHeader("Content-Type", "application/json");
  const int result = request.POST(enabled ? "{\"state\":true}" : "{\"state\":false}");
  request.end();
  return result >= 200 && result < 300;
}

void NetworkService::connectWifi() {
  lastWifiAttemptMs_ = millis();
  if (!settings_) return;
  const auto& cfg = settings_->values;
  if (cfg.wifiSsid[0] == '\0') {
    startAccessPoint();
    return;
  }
  WiFi.mode(apEnabled_ ? WIFI_AP_STA : WIFI_STA);
  WiFi.setHostname(Config::deviceName);
  Serial.print("Łączenie z Wi-Fi: ");
  Serial.println(cfg.wifiSsid);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);
}

void NetworkService::startAccessPoint() {
  if (apEnabled_) return;
  WiFi.mode(settings_ && settings_->values.wifiSsid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(Config::accessPointName);
  apEnabled_ = true;
  Serial.print("AP ");
  Serial.print(Config::accessPointName);
  Serial.print(" http://");
  Serial.println(WiFi.softAPIP());
}

void NetworkService::connectMqtt() {
  if (!settings_ || settings_->values.mqttHost[0] == '\0') return;
  lastMqttAttemptMs_ = millis();
  if (!clockReady()) return;

  const uint64_t chipId = ESP.getEfuseMac();
  char clientId[48];
  snprintf(clientId, sizeof(clientId), "%s-%04X%08X", Config::deviceName,
           static_cast<uint16_t>(chipId >> 32), static_cast<uint32_t>(chipId));

  const char* username = settings_->values.mqttUser[0] != '\0' ? settings_->values.mqttUser : nullptr;
  const char* password = username ? settings_->values.mqttPassword : nullptr;
  Serial.printf("MQTT TLS try heap=%u maxblock=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  const bool connected = mqtt_.connect(clientId, username, password, Config::mqttStatusTopic, 1, true, kOffline, true);
  if (!connected) {
    char tlsError[128] = {};
    const int tlsErrorCode = mqttTlsClient_.lastError(tlsError, sizeof(tlsError));
    Serial.printf("MQTT connection error %d; TLS %d: %s (heap=%u maxblock=%u)\n", mqtt_.state(), tlsErrorCode,
                  tlsErrorCode ? tlsError : "no TLS error reported", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return;
  }

  if (!mqtt_.subscribe(Config::mqttRelayCommandTopic, 1) || !mqtt_.subscribe(Config::mqttModeCommandTopic, 1) ||
      !mqtt_.subscribe(Config::mqttOtaCommandTopic, 1) || !mqtt_.subscribe(Config::mqttStateGetTopic, 1) ||
      !mqtt_.subscribe(Config::mqttSatelliteStateTopic, 1) ||
      !mqtt_.subscribe(Config::mqttSatelliteAvailabilityTopic, 1) ||
      !mqtt_.subscribe(Config::mqttOpenBekenStateTopic, 1) ||
      !mqtt_.subscribe(Config::mqttOpenBekenAvailabilityTopic, 1)) {
    Serial.println("MQTT subscribe error");
    mqtt_.disconnect();
    return;
  }
  mqtt_.publish(Config::mqttStatusTopic, kOnline, true);
  publishControlState(true);
  publishOtaStatus("ready");
  lastMqttPublishMs_ = 0;
  lastMqttLoadRefreshMs_ = 0;
  for (size_t index = 0; index < Config::loadCount; ++index) lastLoadKeyValid_[index] = false;
  Serial.println("MQTT TLS OK");
}

void NetworkService::disconnectMqtt() {
  if (!mqtt_.connected()) return;
  mqtt_.publish(Config::mqttStatusTopic, kOffline, true);
  mqtt_.disconnect();
}

bool NetworkService::clockReady() {
  time_t currentTime = time(nullptr);
  if (currentTime >= kMinimumValidTime) return true;
  if (!timeSyncStarted_) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    timeSyncStarted_ = true;
    Serial.println("NTP sync started");
  }
  return false;
}

void NetworkService::publish(const Telemetry& telemetry) {
  const String json = mqttTelemetryJson(telemetry);
  mqtt_.publish(Config::mqttStateTopic, json.c_str(), true);
}

bool NetworkService::publishControlState(bool force) {
  if (!mqtt_.connected() || !relays_) return false;
  bool changed = false;
  const ControlMode mode = relays_->mode();
  if (force || !lastModeValid_ || mode != lastMode_) {
    if (mqtt_.publish(Config::mqttModeStateTopic, mode == ControlMode::Auto ? "auto" : "manual", true)) {
      lastMode_ = mode;
      lastModeValid_ = true;
      changed = true;
    }
  }
  for (size_t index = 0; index < Config::loadCount; ++index) {
    const bool enabled = relays_->confirmedState(index);
    if (!force && lastRelayStateValid_[index] && lastRelayOn_[index] == enabled) continue;
    if (!publishRelayState(index)) continue;
    lastRelayOn_[index] = enabled;
    lastRelayStateValid_[index] = true;
    changed = true;
  }
  return changed;
}

bool NetworkService::publishRelayState(size_t index) {
  if (!mqtt_.connected() || !relays_ || index >= Config::loadCount) return false;
  char topic[80];
  snprintf(topic, sizeof(topic), "%s%u/state", Config::mqttRelayCommandPrefix, static_cast<unsigned>(index));
  return mqtt_.publish(topic, relays_->confirmedState(index) ? "ON" : "OFF", true);
}

void NetworkService::updateSunMode(float pvPowerW) {
  const uint32_t now = millis();
  if (daytime_) {
    if (pvPowerW < Config::mqttNightEnterPvW) {
      if (lowPvSinceMs_ == 0) lowPvSinceMs_ = now;
      if (now - lowPvSinceMs_ >= Config::mqttNightConfirmMs) {
        daytime_ = false;
        highPvSinceMs_ = 0;
        Serial.println("MQTT: tryb noc (PV niski)");
      }
    } else {
      lowPvSinceMs_ = 0;
    }
    return;
  }

  if (pvPowerW >= Config::mqttDayEnterPvW) {
    if (highPvSinceMs_ == 0) highPvSinceMs_ = now;
    if (now - highPvSinceMs_ >= Config::mqttDayConfirmMs) {
      daytime_ = true;
      lowPvSinceMs_ = 0;
      lastMqttPublishMs_ = 0;
      Serial.println("MQTT: tryb dzien (PV produkuje)");
    }
  } else {
    highPvSinceMs_ = 0;
  }
}

uint32_t NetworkService::currentPublishIntervalMs() const {
  return (daytime_ || burstActive()) ? Config::mqttPublishDayMs : Config::mqttPublishNightMs;
}

uint32_t NetworkService::currentLoadRefreshMs() const {
  return (daytime_ || burstActive()) ? Config::mqttLoadRefreshDayMs : Config::mqttLoadRefreshNightMs;
}

bool NetworkService::significantTelemetryChange(const Telemetry& telemetry) const {
  if (!havePublishedSnapshot_) return false;
  if (fabsf(telemetry.pvPowerW - lastPublishedPvW_) >= Config::mqttDeltaPvW) return true;
  if (fabsf(telemetry.loadPowerW - lastPublishedLoadW_) >= Config::mqttDeltaLoadW) return true;
  if (fabsf(telemetry.batterySoc - lastPublishedSoc_) >= Config::mqttDeltaSoc) return true;
  if (fabsf(telemetry.batteryCurrentA - lastPublishedBatteryA_) >= Config::mqttDeltaBatteryA) return true;
  return false;
}

void NetworkService::rememberPublished(const Telemetry& telemetry) {
  lastPublishedPvW_ = telemetry.pvPowerW;
  lastPublishedLoadW_ = telemetry.loadPowerW;
  lastPublishedSoc_ = telemetry.batterySoc;
  lastPublishedBatteryA_ = telemetry.batteryCurrentA;
  havePublishedSnapshot_ = true;
}

void NetworkService::startBurst() {
  burstUntilMs_ = millis() + Config::mqttBurstWindowMs;
  if (burstUntilMs_ == 0) burstUntilMs_ = 1;
}

bool NetworkService::burstActive() const {
  return burstUntilMs_ != 0 && static_cast<int32_t>(millis() - burstUntilMs_) < 0;
}

bool NetworkService::publishLoadCommands(bool force) {
  if (!mqtt_.connected() || !relays_ || !settings_) return false;
  bool changed = false;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    const char* key = settings_->values.loads[index].mqttKey;
    if (lastLoadKeyValid_[index] && strcmp(lastLoadKey_[index], key) != 0) {
      if (lastLoadKey_[index][0] != '\0') publishLoad(lastLoadKey_[index], false);
      lastLoadKeyValid_[index] = false;
      changed = true;
    }
    if (key[0] == '\0') continue;
    const bool enabled = relays_->state(index);
    const bool isChange = !lastLoadKeyValid_[index] || lastLoadOn_[index] != enabled;
    if (!force && !isChange) continue;
    if (!publishLoad(key, enabled)) continue;
    if (isChange) changed = true;
    strncpy(lastLoadKey_[index], key, Config::labelBytes - 1);
    lastLoadKey_[index][Config::labelBytes - 1] = '\0';
    lastLoadOn_[index] = enabled;
    lastLoadKeyValid_[index] = true;
  }
  return changed;
}

bool NetworkService::publishLoad(const char* key, bool enabled) {
  if (!key || key[0] == '\0') return false;
  char legacyTopic[80];
  char satelliteTopic[80];
  char openBekenTopic[80];
  snprintf(legacyTopic, sizeof(legacyTopic), "%s%s/set", Config::mqttLoadCommandPrefix, key);
  snprintf(satelliteTopic, sizeof(satelliteTopic), "%s%s%s", Config::mqttSatelliteCommandPrefix, key,
           Config::mqttSatelliteCommandSuffix);
  snprintf(openBekenTopic, sizeof(openBekenTopic), "%s/0/set", key);
  const bool legacyPublished = mqtt_.publish(legacyTopic, enabled ? "ON" : "OFF", false);
  const bool satellitePublished = mqtt_.publish(satelliteTopic, enabled ? "ON" : "OFF", false);
  const bool openBekenPublished = mqtt_.publish(openBekenTopic, enabled ? "1" : "0", false);
  return legacyPublished || satellitePublished || openBekenPublished;
}

int NetworkService::loadIndexForMqttKey(const String& key) const {
  if (!settings_ || key.isEmpty() || !Settings::validMqttKey(key.c_str())) return -1;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (key.equals(settings_->values.loads[index].mqttKey)) return static_cast<int>(index);
  }
  return -1;
}

bool NetworkService::parseOnOff(const uint8_t* payload, unsigned int length, bool& enabled) const {
  String value;
  value.reserve(length);
  for (unsigned int index = 0; index < length; ++index) value += static_cast<char>(payload[index]);
  value.trim();
  value.toLowerCase();
  if (value == "on" || value == "1" || value == "true") {
    enabled = true;
    return true;
  }
  if (value == "off" || value == "0" || value == "false") {
    enabled = false;
    return true;
  }
  return false;
}

void NetworkService::handlePendingOta() {
  char expectedSha256[sizeof(pendingOtaSha256_)];
  memcpy(expectedSha256, pendingOtaSha256_, sizeof(expectedSha256));
  pendingOtaSha256_[0] = '\0';

  publishOtaStatus("downloading");
  mqtt_.loop();
  Serial.printf("OTA: pobieranie %s\n", Config::otaFirmwareUrl);

  // HTTPS i finalizacja obrazu mogą trwać dłużej niż watchdog pętli głównej.
  // W razie błędu przywracamy nadzór; po sukcesie urządzenie natychmiast się restartuje.
  const bool watchdogWasRegistered = esp_task_wdt_delete(nullptr) == ESP_OK;
  String error;
  if (!otaUpdater_.install(expectedSha256, error)) {
    if (watchdogWasRegistered) esp_task_wdt_add(nullptr);
    Serial.printf("OTA error: %s\n", error.c_str());
    publishOtaStatus("error", error.c_str());
    return;
  }

  Serial.println("OTA: firmware zweryfikowany, restart");
  publishOtaStatus("installed");
  delay(250);
  ESP.restart();
}

void NetworkService::publishOtaStatus(const char* state, const char* detail) {
  if (!mqtt_.connected()) return;
  String json = String("{\"state\":\"") + state + "\",\"firmwareVersion\":\"" + Config::firmwareVersion +
                "\",\"currentFirmwareMd5\":\"" + ESP.getSketchMD5() + "\"";
  if (detail && detail[0] != '\0') {
    String safeDetail(detail);
    safeDetail.replace("\\", "\\\\");
    safeDetail.replace("\"", "\\\"");
    json += ",\"detail\":\"" + safeDetail + "\"";
  }
  json += "}";
  mqtt_.publish(Config::mqttOtaStatusTopic, json.c_str(), true);
}

void NetworkService::onMqtt(char* topic, uint8_t* payload, unsigned int length) {
  const String topicText(topic);
  const String statePrefix(Config::mqttSatelliteStatePrefix);
  const String availabilityPrefix(Config::mqttSatelliteAvailabilityPrefix);
  const String powerSuffix(Config::mqttSatelliteCommandSuffix);
  const String lwtSuffix("/LWT");
  const String openBekenStateSuffix(Config::mqttOpenBekenStateSuffix);
  const String openBekenAvailabilitySuffix(Config::mqttOpenBekenAvailabilitySuffix);

  const bool canonicalState = topicText.startsWith(statePrefix) && topicText.endsWith(powerSuffix);
  const bool openBekenState = topicText.endsWith(openBekenStateSuffix);
  if (canonicalState || openBekenState) {
    const String key = canonicalState
                           ? topicText.substring(statePrefix.length(), topicText.length() - powerSuffix.length())
                           : topicText.substring(0, topicText.length() - openBekenStateSuffix.length());
    const int index = loadIndexForMqttKey(key);
    bool enabled = false;
    if (index < 0 || !parseOnOff(payload, length, enabled) || !relays_) return;
    relays_->reportRemoteState(static_cast<size_t>(index), enabled);
    if (relays_->mode() == ControlMode::Auto && relays_->state(static_cast<size_t>(index)) != enabled) {
      // Jedno natychmiastowe ponowienie; kolejne proby robi okresowy refresh,
      // wiec niedostepna satelita nie zalewa brokera w kazdej petli.
      lastLoadKeyValid_[index] = false;
    }
    lastRelayStateValid_[index] = false;
    stateGetRequested_ = true;
    startBurst();
    return;
  }

  const bool canonicalAvailability = topicText.startsWith(availabilityPrefix) && topicText.endsWith(lwtSuffix);
  const bool openBekenAvailability = topicText.endsWith(openBekenAvailabilitySuffix);
  if (canonicalAvailability || openBekenAvailability) {
    const String key = canonicalAvailability
                           ? topicText.substring(availabilityPrefix.length(), topicText.length() - lwtSuffix.length())
                           : topicText.substring(0, topicText.length() - openBekenAvailabilitySuffix.length());
    const int index = loadIndexForMqttKey(key);
    if (index < 0 || !relays_) return;
    String value;
    for (unsigned int position = 0; position < length; ++position) value += static_cast<char>(payload[position]);
    value.trim();
    value.toLowerCase();
    if (value != "online" && value != "offline") return;
    const bool online = value == "online";
    relays_->reportRemoteAvailability(static_cast<size_t>(index), online);
    if (online) lastLoadKeyValid_[index] = false;
    stateGetRequested_ = true;
    startBurst();
    return;
  }

  if (strcmp(topic, Config::mqttStateGetTopic) == 0) {
    stateGetRequested_ = true;
    return;
  }

  if (strcmp(topic, Config::mqttOtaCommandTopic) == 0) {
    if (length == 0) return;
    if (!validSha256(reinterpret_cast<const char*>(payload), length)) {
      publishOtaStatus("rejected", "komenda musi zawierac 64 znaki SHA-256");
      return;
    }
    for (unsigned int index = 0; index < length; ++index) {
      pendingOtaSha256_[index] = static_cast<char>(tolower(payload[index]));
    }
    pendingOtaSha256_[length] = '\0';
    mqtt_.publish(Config::mqttOtaCommandTopic, "", true);
    publishOtaStatus("accepted");
    return;
  }

  if (strcmp(topic, Config::mqttModeCommandTopic) == 0) {
    if (!relays_) return;
    String command;
    for (unsigned int position = 0; position < length; ++position) command += static_cast<char>(payload[position]);
    command.toLowerCase();
    if (command == "auto") {
      relays_->setMode(ControlMode::Auto);
    } else if (command == "manual") {
      relays_->setMode(ControlMode::Manual);
    } else {
      return;
    }
    publishControlState(true);
    stateGetRequested_ = true;
    return;
  }

  if (!relays_ || relays_->mode() != ControlMode::Manual) return;
  if (!topicText.startsWith(Config::mqttRelayCommandPrefix) || !topicText.endsWith("/set")) return;
  const size_t indexStart = strlen(Config::mqttRelayCommandPrefix);
  const String indexText = topicText.substring(indexStart, topicText.length() - 4);
  if (indexText.length() != 1 || !isDigit(indexText[0])) return;
  const int index = indexText.toInt();
  if (index < 0 || index >= static_cast<int>(Config::loadCount)) return;
  if (relays_->pin(static_cast<size_t>(index)) < 0) return;

  bool enabled = false;
  if (!parseOnOff(payload, length, enabled)) return;
  if (relays_->setRelay(static_cast<size_t>(index), enabled)) {
    publishRelayState(static_cast<size_t>(index));
    lastRelayOn_[index] = relays_->state(static_cast<size_t>(index));
    lastRelayStateValid_[index] = true;
    stateGetRequested_ = true;
  }
}

