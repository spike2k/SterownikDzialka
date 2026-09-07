#include <Arduino.h>
#include <esp_task_wdt.h>

#include "AppConfig.h"
#include "core/BusMonitor.h"
#include "core/InputMonitor.h"
#include "core/RelayController.h"
#include "core/Settings.h"
#include "core/Telemetry.h"

#include <cstring>
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
  anenji.begin(settings);
  pylontech.begin(settings);
  busMonitor.begin(settings, jkBms, anenji, pylontech);
  network.begin(relays, settings);
  if (settings.values.mqttHost[0]) {
    const uint32_t deadline = millis() + 30000;
    while (!network.mqttConnected() && millis() < deadline) {
      esp_task_wdt_reset();
      network.tick(telemetry);
      delay(20);
    }
    Serial.printf("MQTT %s heap=%u maxblock=%u\n", network.mqttConnected() ? "OK" : "oczekuje",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  jkBms.begin(jkConfig);
  webPanel.begin(telemetry, relays, network, settings, inputs);
}

void loop() {
  esp_task_wdt_reset();

  jkBms.tick(batteryData);
  telemetry.jkOnline = batteryData.online;
  telemetry.bms = batteryData;
  strncpy(telemetry.jkMac, jkBms.mac() ? jkBms.mac() : "", sizeof(telemetry.jkMac) - 1);
  telemetry.jkMac[sizeof(telemetry.jkMac) - 1] = '\0';
  strncpy(telemetry.jkModel, jkBms.deviceModel() ? jkBms.deviceModel() : "", sizeof(telemetry.jkModel) - 1);
  telemetry.jkModel[sizeof(telemetry.jkModel) - 1] = '\0';
  strncpy(telemetry.jkHardware, jkBms.hardwareVersion() ? jkBms.hardwareVersion() : "",
          sizeof(telemetry.jkHardware) - 1);
  telemetry.jkHardware[sizeof(telemetry.jkHardware) - 1] = '\0';
  strncpy(telemetry.jkSoftware, jkBms.softwareVersion() ? jkBms.softwareVersion() : "",
          sizeof(telemetry.jkSoftware) - 1);
  telemetry.jkSoftware[sizeof(telemetry.jkSoftware) - 1] = '\0';
  telemetry.jkValidFrames = jkBms.validFrames();
  telemetry.jkInvalidFrames = jkBms.invalidFrames();
  if (batteryData.online) {
    telemetry.batterySoc = batteryData.socPercent;
    telemetry.batteryVoltageV = batteryData.packVoltageV;
    telemetry.batteryCurrentA = batteryData.currentA;
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
