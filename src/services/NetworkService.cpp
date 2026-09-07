#include "services/NetworkService.h"

#include <HTTPClient.h>
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
      const bool refreshLoads =
          lastMqttLoadRefreshMs_ == 0 || millis() - lastMqttLoadRefreshMs_ >= Config::mqttLoadRefreshMs;
      publishLoadCommands(refreshLoads);
      if (refreshLoads) lastMqttLoadRefreshMs_ = millis();
      if (millis() - lastMqttPublishMs_ >= Config::mqttPublishIntervalMs) {
        publish(telemetry);
        lastMqttPublishMs_ = millis();
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
  if (!mqtt_.connected() || device.isEmpty()) return false;
  const String topic = "ems/remote/" + device + "/set";
  return mqtt_.publish(topic.c_str(), enabled ? "ON" : "OFF", false);
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

  if (!mqtt_.subscribe(Config::mqttRelayCommandTopic, 1) || !mqtt_.subscribe(Config::mqttOtaCommandTopic, 1)) {
    Serial.println("MQTT subscribe error");
    mqtt_.disconnect();
    return;
  }
  mqtt_.publish(Config::mqttStatusTopic, kOnline, true);
  publishOtaStatus("ready");
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

void NetworkService::publishLoadCommands(bool force) {
  if (!mqtt_.connected() || !relays_ || !settings_) return;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    const char* key = settings_->values.loads[index].mqttKey;
    if (lastLoadKeyValid_[index] && strcmp(lastLoadKey_[index], key) != 0) {
      if (lastLoadKey_[index][0] != '\0') publishLoad(lastLoadKey_[index], false);
      lastLoadKeyValid_[index] = false;
    }
    if (key[0] == '\0') continue;
    const bool enabled = relays_->state(index);
    if (!force && lastLoadKeyValid_[index] && lastLoadOn_[index] == enabled) continue;
    if (!publishLoad(key, enabled)) continue;
    strncpy(lastLoadKey_[index], key, Config::labelBytes - 1);
    lastLoadKey_[index][Config::labelBytes - 1] = '\0';
    lastLoadOn_[index] = enabled;
    lastLoadKeyValid_[index] = true;
  }
}

bool NetworkService::publishLoad(const char* key, bool enabled) {
  if (!key || key[0] == '\0') return false;
  char topic[80];
  snprintf(topic, sizeof(topic), "%s%s/set", Config::mqttLoadCommandPrefix, key);
  return mqtt_.publish(topic, enabled ? "ON" : "OFF", false);
}

void NetworkService::handlePendingOta() {
  char expectedSha256[sizeof(pendingOtaSha256_)];
  memcpy(expectedSha256, pendingOtaSha256_, sizeof(expectedSha256));
  pendingOtaSha256_[0] = '\0';

  publishOtaStatus("downloading");
  mqtt_.loop();
  Serial.printf("OTA: pobieranie %s\n", Config::otaFirmwareUrl);

  String error;
  if (!otaUpdater_.install(expectedSha256, error)) {
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
  String json = String("{\"state\":\"") + state + "\",\"currentFirmwareMd5\":\"" + ESP.getSketchMD5() + "\"";
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

  if (!relays_ || relays_->mode() != ControlMode::Manual) return;
  const String topicText(topic);
  if (!topicText.startsWith(Config::mqttRelayCommandPrefix) || !topicText.endsWith("/set")) return;
  const size_t indexStart = strlen(Config::mqttRelayCommandPrefix);
  const String indexText = topicText.substring(indexStart, topicText.length() - 4);
  if (indexText.length() != 1 || !isDigit(indexText[0])) return;
  const int index = indexText.toInt();
  if (index < 0 || index >= static_cast<int>(Config::loadCount)) return;
  if (relays_->pin(static_cast<size_t>(index)) < 0) return;

  String command;
  for (unsigned int position = 0; position < length; ++position) command += static_cast<char>(payload[position]);
  bool enabled = false;
  if (command == "ON" || command == "1" || command == "true") {
    enabled = true;
  } else if (command != "OFF" && command != "0" && command != "false") {
    return;
  }
  relays_->setRelay(static_cast<size_t>(index), enabled);
}

