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
#ifndef EMS_JK_BMS_MAC
#define EMS_JK_BMS_MAC ""
#endif

namespace Config {
constexpr char deviceName[] = "sterownik-dzialka";
// Jawny identyfikator wdrozenia; ESP.getSketchMD5() okazal sie niewystarczajacy
// do rozroznienia obrazu przed i po zdalnej aktualizacji.
constexpr char firmwareVersion[] = "2026.09.11-mqtt-contract-3";
constexpr char accessPointName[] = "SterownikDzialka-Setup";
constexpr char mqttStateTopic[] = "ems/sterownik-dzialka/state";
constexpr char mqttStateGetTopic[] = "ems/sterownik-dzialka/state/get";
constexpr char mqttStatusTopic[] = "ems/sterownik-dzialka/status";
constexpr char mqttModeCommandTopic[] = "ems/sterownik-dzialka/mode/set";
constexpr char mqttModeStateTopic[] = "ems/sterownik-dzialka/mode/state";
constexpr char mqttRelayCommandTopic[] = "ems/sterownik-dzialka/relay/+/set";
constexpr char mqttRelayCommandPrefix[] = "ems/sterownik-dzialka/relay/";
constexpr char mqttLoadCommandPrefix[] = "ems/sterownik-dzialka/load/";
// Wspolny kontrakt satelitow (Tasmota/OpenBeken): komenda nie-retained,
// potwierdzony stan i LWT retained.
constexpr char mqttSatelliteCommandPrefix[] = "cmnd/";
constexpr char mqttSatelliteCommandSuffix[] = "/POWER";
constexpr char mqttSatelliteStateTopic[] = "stat/+/POWER";
constexpr char mqttSatelliteStatePrefix[] = "stat/";
constexpr char mqttSatelliteAvailabilityTopic[] = "tele/+/LWT";
constexpr char mqttSatelliteAvailabilityPrefix[] = "tele/";
// Adapter przejsciowy dla juz zainstalowanych OpenBeken (kanal przekaznika 0).
constexpr char mqttOpenBekenStateTopic[] = "+/0/get";
constexpr char mqttOpenBekenStateSuffix[] = "/0/get";
constexpr char mqttOpenBekenAvailabilityTopic[] = "+/connected";
constexpr char mqttOpenBekenAvailabilitySuffix[] = "/connected";
constexpr char mqttOtaCommandTopic[] = "ems/sterownik-dzialka/ota/set";
constexpr char mqttOtaStatusTopic[] = "ems/sterownik-dzialka/ota/status";
constexpr char otaFirmwareUrl[] = "https://www.warsztatweb.pl/esp32/dzialka/firmware.bin";
constexpr uint16_t defaultMqttPort = 8883;
constexpr uint16_t mqttKeepAliveSeconds = 30;
constexpr uint16_t mqttSocketTimeoutSeconds = 15;
constexpr uint32_t mqttRetryMs = 5000;
constexpr uint32_t telemetryIntervalMs = 1000;
// MQTT state: częściej gdy PV produkuje, rzadziej w nocy; skok mocy / state/get wymusza od razu.
constexpr uint32_t mqttPublishDayMs = 15000;
constexpr uint32_t mqttPublishNightMs = 300000;
constexpr uint32_t mqttLoadRefreshDayMs = 15000;
constexpr uint32_t mqttLoadRefreshNightMs = 300000;
constexpr uint32_t mqttDayConfirmMs = 30000;
constexpr uint32_t mqttNightConfirmMs = 120000;
constexpr uint32_t mqttBurstWindowMs = 90000;
constexpr float mqttDayEnterPvW = 50.0f;
constexpr float mqttNightEnterPvW = 20.0f;
constexpr float mqttDeltaLoadW = 150.0f;
constexpr float mqttDeltaPvW = 80.0f;
constexpr float mqttDeltaSoc = 1.0f;
constexpr float mqttDeltaBatteryA = 2.0f;
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
constexpr uint32_t anenjiBaud = 9600;  // potwierdzone podsłuchem dongla, slave 1 FC03
constexpr uint32_t pylonBaud = 115200;
constexpr uint32_t busDebugIdleMs = 5000;
constexpr uint32_t jkDataTimeoutMs = 15000;
}
