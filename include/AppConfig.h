#pragma once

#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef EMS_WIFI_SSID
#define EMS_WIFI_SSID "dlink-iot"
#endif
#ifndef EMS_WIFI_PASSWORD
#define EMS_WIFI_PASSWORD "qwerty123"
#endif
#ifndef EMS_MQTT_HOST
#define EMS_MQTT_HOST "mqtt.ele365.eu"
#endif
#ifndef EMS_MQTT_PORT
#define EMS_MQTT_PORT 8883
#endif
#ifndef EMS_MQTT_USER
#define EMS_MQTT_USER ""
#endif
#ifndef EMS_MQTT_PASSWORD
#define EMS_MQTT_PASSWORD ""
#endif
#ifndef EMS_SIMULATION
#define EMS_SIMULATION 0
#endif

namespace Config {
constexpr char deviceName[] = "sterownik-dzialka";
constexpr char accessPointName[] = "SterownikDzialka-Setup";
constexpr char mqttStateTopic[] = "ems/sterownik-dzialka/state";
constexpr char mqttStatusTopic[] = "ems/sterownik-dzialka/status";
constexpr char mqttRelayCommandTopic[] = "ems/sterownik-dzialka/relay/+/set";
constexpr char mqttRelayCommandPrefix[] = "ems/sterownik-dzialka/relay/";
constexpr char mqttLoadCommandPrefix[] = "ems/sterownik-dzialka/load/";
constexpr uint16_t defaultMqttPort = 8883;
constexpr uint16_t mqttKeepAliveSeconds = 30;
constexpr uint16_t mqttSocketTimeoutSeconds = 15;
constexpr uint32_t mqttRetryMs = 5000;
constexpr uint32_t telemetryIntervalMs = 1000;
constexpr uint32_t mqttPublishIntervalMs = 5000;
constexpr uint32_t mqttLoadRefreshMs = 30000;
constexpr uint32_t telemetryStaleMs = 15000;
constexpr uint32_t wifiRetryMs = 15000;
constexpr uint32_t wifiApFallbackMs = 25000;

constexpr size_t relayCount = 4;
constexpr size_t loadCount = 10;
constexpr size_t statusCount = 4;

// Mapa GPIO startowa (ESP32 DevKit). Zmiana z panelu WWW, bez rekompilacji.
constexpr int defaultRelayPins[relayCount] = {16, 17, 18, 19};
constexpr bool defaultRelayActiveLow = true;

constexpr int defaultStatusPins[statusCount] = {-1, -1, -1, -1};
constexpr bool defaultStatusActiveLow = true;

constexpr int jkRxPin = 27;
constexpr int jkTxPin = 26;
constexpr int anenjiRxPin = 33;
constexpr int anenjiTxPin = 32;
constexpr int pylonRxPin = 22;
constexpr int pylonTxPin = 23;

constexpr uint16_t defaultCellDriftAlarmMv = 50;
constexpr uint16_t defaultSurplusReserveW = 100;
constexpr uint16_t defaultLoadHysteresisW = 80;
constexpr uint16_t defaultLoadMinToggleMs = 20000;
constexpr float chargeIdleAmps = 0.15f;
constexpr size_t labelBytes = 21;
constexpr uint32_t jkBaud = 115200;
constexpr uint32_t anenjiBaud = 2400;
constexpr uint32_t pylonBaud = 115200;
constexpr uint32_t busDebugIdleMs = 5000;
}
