#include <Arduino.h>
#include <esp_task_wdt.h>

#include "AppConfig.h"
#include "core/BusMonitor.h"
#include "core/InputMonitor.h"
#include "core/RelayController.h"
#include "core/Settings.h"
#include "core/Telemetry.h"
#include "drivers/AnenjiDriver.h"
#include "drivers/JkBmsBleDriver.h"
#include "drivers/PylontechEmulator.h"
#include "services/NetworkService.h"
#include "web/WebPanel.h"

Settings settings;
Telemetry telemetry;
RelayController relays;
InputMonitor inputs;
BatteryData batteryData;
JkBmsBleDriver jkBms;
AnenjiDriver anenji;
PylontechEmulator pylontech;
NetworkService network;
WebPanel webPanel;
BusMonitor busMonitor;
uint32_t lastTelemetryMs = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nSterownik Dzialka EMS start");
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(nullptr);

  settings.begin();
  Serial.printf("WiFi SSID: %s\n", settings.values.wifiSsid[0] ? settings.values.wifiSsid : "(tryb AP)");
  Serial.print("GPIO odbiorniki:");
  for (size_t index = 0; index < Config::loadCount; ++index) {
    Serial.printf(" %d", settings.values.loads[index].pin);
  }
  Serial.println();
  Serial.printf("JK BLE MAC: %s | ANENJI RX/TX: %d %d | Pylon RX/TX: %d %d\n",
                settings.values.jkBmsMac[0] ? settings.values.jkBmsMac : "AUTO", settings.values.anenjiRxPin,
                settings.values.anenjiTxPin, settings.values.pylonRxPin, settings.values.pylonTxPin);
  relays.begin(settings);
  inputs.begin(settings);
  JkBmsBleDriver::Config jkConfig;
  jkConfig.mac = settings.values.jkBmsMac;
  jkConfig.verbose = settings.values.debugJk;
  jkBms.begin(jkConfig);
  anenji.begin(settings);
  pylontech.begin(settings);
  busMonitor.begin(settings, jkBms, anenji, pylontech);
  network.begin(relays, settings);
  webPanel.begin(telemetry, relays, network, settings, inputs);
}

void loop() {
  esp_task_wdt_reset();

  jkBms.tick(batteryData);
  telemetry.jkOnline = batteryData.online;
  if (batteryData.online) {
    telemetry.batterySoc = batteryData.socPercent;
    telemetry.batteryVoltageV = batteryData.packVoltageV;
    telemetry.batteryCurrentA = batteryData.currentA;
    telemetry.cellVoltageV = batteryData.cellVoltageV;
    telemetry.cellCount = batteryData.cellCount;
    telemetry.updatedAtMs = batteryData.lastUpdateMs;
  }

  if (millis() - lastTelemetryMs >= Config::telemetryIntervalMs) {
    anenji.poll(telemetry);
    lastTelemetryMs = millis();
  }

  const bool telemetryHealthy = (telemetry.jkOnline || telemetry.anenjiOnline) &&
                                millis() - telemetry.updatedAtMs < Config::telemetryStaleMs;
  const float pvPowerW = telemetry.anenjiOnline ? telemetry.pvPowerW : 0;
  const float loadPowerW = telemetry.anenjiOnline ? telemetry.loadPowerW : 0;
  relays.tick(telemetryHealthy, pvPowerW, loadPowerW);
  pylontech.tick(batteryData);
  network.tick(telemetry);
  busMonitor.tick();
  webPanel.tick();
  delay(2);
}
