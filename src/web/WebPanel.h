#pragma once

#include <WebServer.h>
#include "core/InputMonitor.h"
#include "core/RelayController.h"
#include "core/Settings.h"
#include "core/Telemetry.h"
#include "services/NetworkService.h"

class WebPanel {
 public:
  void begin(Telemetry& telemetry, RelayController& relays, NetworkService& network, Settings& settings,
             InputMonitor& inputs);
  void tick();

 private:
  void handleState();
  void handleSettingsGet();
  void handleSettingsPost();
  void handleMode();
  void handleRelay();
  void handleRemote();
  void handleReboot();
  String stateJson() const;
  String settingsJson() const;
  bool readPostedSettings(AppSettings& target, String& error);

  WebServer server_{80};
  Telemetry* telemetry_ = nullptr;
  RelayController* relays_ = nullptr;
  NetworkService* network_ = nullptr;
  Settings* settings_ = nullptr;
  InputMonitor* inputs_ = nullptr;
};
