#pragma once

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "core/RelayController.h"
#include "core/Settings.h"
#include "core/Telemetry.h"

class NetworkService {
 public:
  void begin(RelayController& relays, Settings& settings);
  void tick(const Telemetry& telemetry);
  void applyWifi();
  void applyMqtt();
  bool wifiConnected() const;
  bool mqttConnected();
  bool accessPointActive() const;
  String ipAddress() const;
  String stationIp() const;
  String apIp() const;
  bool sendRemoteMqtt(const String& device, bool enabled);
  bool sendRemoteHttp(const String& url, bool enabled);

 private:
  static void onWifiEvent(WiFiEvent_t event);
  void connectWifi();
  void startAccessPoint();
  void connectMqtt();
  void disconnectMqtt();
  bool clockReady();
  void publish(const Telemetry& telemetry);
  void publishLoadCommands(bool force);
  bool publishLoad(const char* key, bool enabled);
  void onMqtt(char* topic, uint8_t* payload, unsigned int length);
  String telemetryJson(const Telemetry& telemetry) const;

  WiFiClientSecure mqttTlsClient_;
  PubSubClient mqtt_{mqttTlsClient_};
  RelayController* relays_ = nullptr;
  Settings* settings_ = nullptr;
  uint32_t lastWifiAttemptMs_ = 0;
  uint32_t lastMqttAttemptMs_ = 0;
  uint32_t lastMqttPublishMs_ = 0;
  uint32_t lastMqttLoadRefreshMs_ = 0;
  uint32_t wifiStartedMs_ = 0;
  uint32_t staOkSinceMs_ = 0;
  bool timeSyncStarted_ = false;
  bool apEnabled_ = false;
  char lastLoadKey_[Config::loadCount][Config::labelBytes] = {};
  bool lastLoadOn_[Config::loadCount] = {};
  bool lastLoadKeyValid_[Config::loadCount] = {};
};
