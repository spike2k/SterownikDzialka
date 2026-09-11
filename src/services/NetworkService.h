#pragma once

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "core/RelayController.h"
#include "core/Settings.h"
#include "core/Telemetry.h"
#include "services/OtaUpdater.h"

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
  bool publishControlState(bool force);
  bool publishRelayState(size_t index);
  bool publishLoadCommands(bool force);
  bool publishLoad(const char* key, bool enabled);
  int loadIndexForMqttKey(const String& key) const;
  bool parseOnOff(const uint8_t* payload, unsigned int length, bool& enabled) const;
  void handlePendingOta();
  void publishOtaStatus(const char* state, const char* detail = nullptr);
  void onMqtt(char* topic, uint8_t* payload, unsigned int length);
  void updateSunMode(float pvPowerW);
  uint32_t currentPublishIntervalMs() const;
  uint32_t currentLoadRefreshMs() const;
  bool significantTelemetryChange(const Telemetry& telemetry) const;
  void rememberPublished(const Telemetry& telemetry);
  void startBurst();
  bool burstActive() const;

  WiFiClientSecure mqttTlsClient_;
  PubSubClient mqtt_{mqttTlsClient_};
  OtaUpdater otaUpdater_;
  RelayController* relays_ = nullptr;
  Settings* settings_ = nullptr;
  uint32_t lastWifiAttemptMs_ = 0;
  uint32_t lastMqttAttemptMs_ = 0;
  uint32_t lastMqttPublishMs_ = 0;
  uint32_t lastMqttLoadRefreshMs_ = 0;
  uint32_t wifiStartedMs_ = 0;
  uint32_t staOkSinceMs_ = 0;
  uint32_t lowPvSinceMs_ = 0;
  uint32_t highPvSinceMs_ = 0;
  uint32_t burstUntilMs_ = 0;
  bool timeSyncStarted_ = false;
  bool apEnabled_ = false;
  bool daytime_ = true;
  bool stateGetRequested_ = false;
  bool havePublishedSnapshot_ = false;
  float lastPublishedPvW_ = 0;
  float lastPublishedLoadW_ = 0;
  float lastPublishedSoc_ = 0;
  float lastPublishedBatteryA_ = 0;
  char lastLoadKey_[Config::loadCount][Config::labelBytes] = {};
  bool lastLoadOn_[Config::loadCount] = {};
  bool lastLoadKeyValid_[Config::loadCount] = {};
  bool lastRelayOn_[Config::loadCount] = {};
  bool lastRelayStateValid_[Config::loadCount] = {};
  ControlMode lastMode_ = ControlMode::Auto;
  bool lastModeValid_ = false;
  char pendingOtaSha256_[65] = {};
};
