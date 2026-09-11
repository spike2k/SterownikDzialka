#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <cstring>
#include <esp_ota_ops.h>
#include <time.h>

#include "AppConfig.h"
#include "TlsCertificates.h"
#include "core/Settings.h"
#include "services/OtaUpdater.h"

namespace {
constexpr char kBridgeVersion[] = "2026.09.11-ota-bridge-3";
constexpr char kFinalFirmwareUrl[] = "https://www.warsztatweb.pl/esp32/dzialka/firmware-full.bin";
constexpr char kFinalFirmwareSha256[] = "d99f3f26d3ae84a0800e11996058b6aacb92721eb941e90ea444f0e99314481b";
constexpr char kOnline[] = "maintenance";
constexpr char kOffline[] = "offline";

Settings settings;
WiFiClientSecure mqttTls;
PubSubClient mqtt(mqttTls);
OtaUpdater updater;
char pendingSha256[65] = {};
uint32_t lastConnectAttemptMs = 0;
uint32_t lastAutomaticOtaAttemptMs = 0;

bool validSha256(const uint8_t* payload, unsigned int length) {
  if (length != 64) return false;
  for (unsigned int index = 0; index < length; ++index) {
    if (!isxdigit(static_cast<unsigned char>(payload[index]))) return false;
  }
  return true;
}

void publishStatus(const char* state, const char* detail = nullptr) {
  if (!mqtt.connected()) return;
  String json = String("{\"state\":\"") + state + "\",\"firmwareVersion\":\"" + kBridgeVersion +
                "\",\"currentFirmwareMd5\":\"" + ESP.getSketchMD5() + "\"";
  if (detail && detail[0]) {
    String safe(detail);
    safe.replace("\\", "\\\\");
    safe.replace("\"", "\\\"");
    json += ",\"detail\":\"" + safe + "\"";
  }
  json += '}';
  mqtt.publish(Config::mqttOtaStatusTopic, json.c_str(), true);
}

void onMqtt(char* topic, uint8_t* payload, unsigned int length) {
  if (strcmp(topic, Config::mqttOtaCommandTopic) != 0) return;
  if (!validSha256(payload, length)) {
    publishStatus("rejected", "komenda musi zawierac 64 znaki SHA-256");
    return;
  }
  for (unsigned int index = 0; index < length; ++index) {
    pendingSha256[index] = static_cast<char>(tolower(payload[index]));
  }
  pendingSha256[length] = '\0';
  mqtt.publish(Config::mqttOtaCommandTopic, "", true);
  publishStatus("accepted");
}

void makeOutputsSafe() {
  const uint8_t inactive = settings.values.relayActiveLow ? HIGH : LOW;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    const int pin = settings.values.loads[index].pin;
    if (!Settings::validOutputPin(pin)) continue;
    digitalWrite(pin, inactive);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, inactive);
  }
}

bool clockReady() {
  return time(nullptr) >= 1700000000;
}

void connectMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED || !clockReady()) return;
  char clientId[48];
  snprintf(clientId, sizeof(clientId), "ota-bridge-%08X", static_cast<unsigned>(ESP.getEfuseMac()));
  const char* user = settings.values.mqttUser[0] ? settings.values.mqttUser : nullptr;
  const char* password = settings.values.mqttPassword[0] ? settings.values.mqttPassword : nullptr;
  if (!mqtt.connect(clientId, user, password, Config::mqttStatusTopic, 0, true, kOffline)) return;
  if (!mqtt.subscribe(Config::mqttOtaCommandTopic, 1)) {
    mqtt.disconnect();
    return;
  }
  mqtt.publish(Config::mqttStatusTopic, kOnline, true);
  publishStatus("bridge-ready");
}
}

void setup() {
  // Arduino-ESP32 ma wlaczony rollback. Most musi zatwierdzic nowa partycje
  // przed Wi-Fi, NVS i jakakolwiek operacja, ktora moglaby go zrestartowac.
  esp_ota_mark_app_valid_cancel_rollback();
  Serial.begin(115200);
  delay(100);
  settings.begin();
  makeOutputsSafe();

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.values.wifiSsid, settings.values.wifiPassword);
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");

  mqttTls.setCACert(TlsCertificates::letsEncryptRootX1);
  mqttTls.setHandshakeTimeout(15);
  mqtt.setServer(settings.values.mqttHost, settings.values.mqttPort);
  mqtt.setKeepAlive(Config::mqttKeepAliveSeconds);
  mqtt.setSocketTimeout(Config::mqttSocketTimeoutSeconds);
  mqtt.setCallback(onMqtt);
}

void loop() {
  if (!mqtt.connected() && millis() - lastConnectAttemptMs >= Config::mqttRetryMs) {
    lastConnectAttemptMs = millis();
    connectMqtt();
  }
  mqtt.loop();

  if (pendingSha256[0]) {
    char expected[sizeof(pendingSha256)];
    memcpy(expected, pendingSha256, sizeof(expected));
    pendingSha256[0] = '\0';
    publishStatus("downloading");
    mqtt.loop();
    String error;
    if (!updater.install(expected, error)) {
      publishStatus("error", error.c_str());
    } else {
      publishStatus("installed");
      mqtt.loop();
      delay(500);
      ESP.restart();
    }
  }

  // Most sam przechodzi do pełnego EMS. Dzięki osobnemu URL nie wymaga
  // ręcznej podmiany pliku w krótkim oknie między dwoma restartami.
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED && clockReady() && now >= 15000 &&
      (lastAutomaticOtaAttemptMs == 0 || now - lastAutomaticOtaAttemptMs >= 60000)) {
    lastAutomaticOtaAttemptMs = now;
    publishStatus("installing-final");
    mqtt.loop();
    String error;
    if (!updater.installFrom(kFinalFirmwareUrl, kFinalFirmwareSha256, error)) {
      publishStatus("final-error", error.c_str());
    } else {
      publishStatus("final-installed");
      mqtt.loop();
      delay(500);
      ESP.restart();
    }
  }
  delay(5);
}
