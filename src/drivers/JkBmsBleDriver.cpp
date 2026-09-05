#include "drivers/JkBmsBleDriver.h"

#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEScan.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>

#include "drivers/JkBmsProtocol.h"

namespace {
constexpr uint16_t kServiceUuid = 0xFFE0;
constexpr uint16_t kCharacteristicUuid = 0xFFE1;
constexpr uint32_t kScanSeconds = 8;
constexpr uint32_t kRetryMs = 5000;
constexpr uint32_t kDataTimeoutMs = 15000;
constexpr uint32_t kRequestRetryMs = 4000;
constexpr uint32_t kLogIntervalMs = 15000;

void printHex(const char* prefix, const uint8_t* data, size_t length) {
  Serial.printf("%s [%u B] ", prefix, static_cast<unsigned>(length));
  for (size_t i = 0; i < length; ++i) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    if (i + 1 != length) Serial.print(' ');
  }
  Serial.println();
}

bool validMacText(const char* value) {
  if (!value || value[0] == '\0') return false;
  if (std::strlen(value) != 17) return false;
  for (size_t i = 0; i < 17; ++i) {
    if ((i + 1) % 3 == 0) {
      if (value[i] != ':') return false;
    } else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
      return false;
    }
  }
  return true;
}
}  // namespace

class JkBmsBleDriver::Impl : public BLEAdvertisedDeviceCallbacks, public BLEClientCallbacks {
 public:
  void begin(const Config& config) {
    verbose_ = config.verbose;
    autoConnect_ = config.autoConnect;
    targetMac_[0] = '\0';
    if (validMacText(config.mac)) {
      std::snprintf(targetMac_, sizeof(targetMac_), "%s", config.mac);
      for (char* c = targetMac_; *c; ++c) *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    }
    BLEDevice::init("SterownikDzialka-JK");
    scan_ = BLEDevice::getScan();
    scan_->setActiveScan(true);
    scan_->setInterval(160);
    scan_->setWindow(80);
    scan_->setAdvertisedDeviceCallbacks(this, false);
    started_ = true;
    Serial.printf("JK BLE: start, MAC=%s\n", targetMac_[0] ? targetMac_ : "AUTO");
  }

  void tick(BatteryData& output) {
    if (!started_) return;
    const uint32_t now = millis();
    if (connected_ && lastValidFrameMs_ && now - lastValidFrameMs_ > kDataTimeoutMs) {
      connected_ = false;
      if (client_ && client_->isConnected()) client_->disconnect();
      Serial.println("JK BLE: timeout danych, ponowne laczenie");
    }
    if (connected_ && now - lastRequestMs_ >= kRequestRetryMs &&
        (!lastValidFrameMs_ || now - lastValidFrameMs_ >= kRequestRetryMs)) {
      sendReadRequests();
    }
    if (!connected_ && !scanning_ && !connecting_ && autoConnect_ && now - lastAttemptMs_ >= kRetryMs) startScan();
    if (connectRequested_ && !connecting_) {
      connectRequested_ = false;
      connecting_ = true;
      if (xTaskCreate(connectTaskThunk, "jk-ble-connect", 8192, this, 1, nullptr) != pdPASS) {
        connecting_ = false;
        Serial.println("JK BLE: nie mozna uruchomic zadania laczenia");
      }
    }
    portENTER_CRITICAL(&dataMux_);
    output = battery_;
    portEXIT_CRITICAL(&dataMux_);
    output.online = connected_ && output.lastUpdateMs != 0 && now - output.lastUpdateMs <= kDataTimeoutMs;
  }

  void setVerbose(bool enabled) { verbose_ = enabled; }
  void setProtocolHint(BatteryProtocol protocol) {
    protocolHint_ = protocol;
    portENTER_CRITICAL(&dataMux_);
    battery_ = BatteryData{};
    portEXIT_CRITICAL(&dataMux_);
    lastValidFrameMs_ = 0;
  }
  bool connected() const { return connected_; }
  const char* mac() const { return connectedMac_; }
  const char* model() const { return model_; }
  const char* hardware() const { return hardware_; }
  const char* software() const { return software_; }
  uint32_t validFrames() const { return validFrames_; }
  uint32_t invalidFrames() const { return invalidFrames_; }

  void onResult(BLEAdvertisedDevice device) override {
    const std::string address = device.getAddress().toString();
    const std::string name = device.haveName() ? device.getName() : std::string();
    const bool ffe0 = device.haveServiceUUID() && device.isAdvertisingService(BLEUUID(kServiceUuid));
    const bool knownPrefix = address.rfind("20:21:11:", 0) == 0 || address.rfind("c8:47:8c:", 0) == 0;
    const bool jk = JkBmsBleDriver::looksLikeJk(name.c_str(), ffe0) || knownPrefix;
    Serial.printf("BLE  name=%s  MAC=%s  RSSI=%d%s\n", name.empty() ? "(brak)" : name.c_str(),
                  address.c_str(), device.getRSSI(), jk ? "  [JK-BMS?]" : "");
    const bool selected = targetMac_[0] ? strcasecmp(targetMac_, address.c_str()) == 0 : jk;
    if (!selected || connecting_ || connected_ || connectRequested_) return;
    std::snprintf(candidateMac_, sizeof(candidateMac_), "%s", address.c_str());
    candidateAddressType_ = device.getAddressType();
    connectRequested_ = true;
    if (scan_) scan_->stop();
  }

  void onConnect(BLEClient*) override {}

  void onDisconnect(BLEClient*) override {
    connected_ = false;
    connecting_ = false;
    writeCharacteristic_ = nullptr;
    notifyCharacteristic_ = nullptr;
    lastAttemptMs_ = millis();
    Serial.println("JK-BMS DISCONNECTED");
  }

 private:
  static void scanCompleteThunk(BLEScanResults) {
    if (active_) {
      active_->scanning_ = false;
      if (active_->scan_) active_->scan_->clearResults();
    }
  }

  static void connectTaskThunk(void* argument) {
    static_cast<Impl*>(argument)->connectTask();
    vTaskDelete(nullptr);
  }

  void startScan() {
    active_ = this;
    lastAttemptMs_ = millis();
    scanning_ = true;
    Serial.printf("JK BLE: skanowanie %lu s...\n", static_cast<unsigned long>(kScanSeconds));
    if (!scan_->start(kScanSeconds, scanCompleteThunk, false)) scanning_ = false;
  }

  void connectTask() {
    lastAttemptMs_ = millis();
    Serial.printf("JK BLE: laczenie z %s...\n", candidateMac_);
    if (!client_) {
      client_ = BLEDevice::createClient();
      client_->setClientCallbacks(this);
    }
    if (!client_->connect(BLEAddress(candidateMac_), candidateAddressType_)) {
      Serial.println("JK BLE: polaczenie nieudane");
      connecting_ = false;
      return;
    }
    client_->setMTU(517);
    BLERemoteService* service = client_->getService(BLEUUID(kServiceUuid));
    if (verbose_) dumpServices();
    if (!service) {
      Serial.println("JK BLE: brak service FFE0");
      client_->disconnect();
      connecting_ = false;
      return;
    }
    auto* chars = service->getCharacteristicsByHandle();
    for (const auto& entry : *chars) {
      BLERemoteCharacteristic* characteristic = entry.second;
      if (!characteristic->getUUID().equals(BLEUUID(kCharacteristicUuid))) continue;
      if (!writeCharacteristic_ && (characteristic->canWriteNoResponse() || characteristic->canWrite())) {
        writeCharacteristic_ = characteristic;
      }
      if (!notifyCharacteristic_ && (characteristic->canNotify() || characteristic->canIndicate())) {
        notifyCharacteristic_ = characteristic;
      }
    }
    if (!writeCharacteristic_ || !notifyCharacteristic_) {
      Serial.println("JK BLE: nie znaleziono pary FFE1 write/notify");
      client_->disconnect();
      connecting_ = false;
      return;
    }
    notifyCharacteristic_->registerForNotify(
        [this](BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) { onNotify(data, length); });
    std::snprintf(connectedMac_, sizeof(connectedMac_), "%s", candidateMac_);
    connected_ = true;
    connecting_ = false;
    portENTER_CRITICAL(&dataMux_);
    battery_.online = false;
    battery_.lastUpdateMs = 0;
    portEXIT_CRITICAL(&dataMux_);
    assemblerLength_ = 0;
    headerMatch_ = 0;
    lastValidFrameMs_ = 0;
    Serial.println("JK-BMS CONNECTED");
    Serial.printf("MAC: %s\n", connectedMac_);
    sendReadRequests();
  }

  void dumpServices() {
    auto* services = client_->getServices();
    Serial.println("BLE SERVICES / CHARACTERISTICS:");
    for (const auto& serviceEntry : *services) {
      BLERemoteService* service = serviceEntry.second;
      Serial.printf("  SERVICE %s\n", service->getUUID().toString().c_str());
      auto* chars = service->getCharacteristicsByHandle();
      for (const auto& charEntry : *chars) {
        BLERemoteCharacteristic* chr = charEntry.second;
        Serial.printf("    CHAR %s handle=0x%04X R=%d W=%d WNR=%d N=%d I=%d\n",
                      chr->getUUID().toString().c_str(), chr->getHandle(), chr->canRead(), chr->canWrite(),
                      chr->canWriteNoResponse(), chr->canNotify(), chr->canIndicate());
      }
    }
  }

  void sendReadRequests() {
    if (!connected_ || !writeCharacteristic_) return;
    const auto deviceInfo = JkBmsProtocol::buildReadCommand(0x97, sequence_++);
    const auto cellInfo = JkBmsProtocol::buildReadCommand(0x96, sequence_++);
    writeCharacteristic_->writeValue(const_cast<uint8_t*>(deviceInfo.data()), deviceInfo.size(), false);
    writeCharacteristic_->writeValue(const_cast<uint8_t*>(cellInfo.data()), cellInfo.size(), false);
    lastRequestMs_ = millis();
    if (verbose_) {
      printHex("JK TX DEVICE INFO", deviceInfo.data(), deviceInfo.size());
      printHex("JK TX SETTINGS/STREAM", cellInfo.data(), cellInfo.size());
    }
  }

  void onNotify(const uint8_t* data, size_t length) {
    if (verbose_) printHex("JK NOTIFY", data, length);
    static constexpr uint8_t header[] = {0x55, 0xAA, 0xEB, 0x90};
    size_t start = 0;
    if (length >= sizeof(header) && std::memcmp(data, header, sizeof(header)) == 0) {
      std::memcpy(frame_, header, sizeof(header));
      assemblerLength_ = sizeof(header);
      headerMatch_ = 0;
      start = sizeof(header);
    }
    for (size_t i = start; i < length; ++i) {
      const uint8_t value = data[i];
      if (assemblerLength_ == 0) {
        if (value == header[headerMatch_]) {
          ++headerMatch_;
          if (headerMatch_ == sizeof(header)) {
            std::memcpy(frame_, header, sizeof(header));
            assemblerLength_ = sizeof(header);
            headerMatch_ = 0;
          }
        } else {
          headerMatch_ = value == header[0] ? 1 : 0;
        }
        continue;
      }
      if (assemblerLength_ < sizeof(frame_)) frame_[assemblerLength_++] = value;
      if (assemblerLength_ == JkBmsProtocol::FrameSize) {
        handleFrame();
        assemblerLength_ = 0;
        headerMatch_ = 0;
      }
    }
  }

  void handleFrame() {
    if (!JkBmsProtocol::validFrame(frame_, JkBmsProtocol::FrameSize)) {
      ++invalidFrames_;
      Serial.printf("JK FRAME INVALID: CRC/HEADER (bad=%lu)\n", static_cast<unsigned long>(invalidFrames_));
      return;
    }
    ++validFrames_;
    if (verbose_) Serial.printf("JK FRAME OK type=0x%02X count=%lu\n", frame_[4], static_cast<unsigned long>(validFrames_));
    if (frame_[4] == 0x03 && JkBmsProtocol::decodeDeviceInfo(frame_, JkBmsProtocol::FrameSize, model_, sizeof(model_),
                                                              hardware_, sizeof(hardware_), software_, sizeof(software_))) {
      Serial.printf("JK DEVICE model=%s hw=%s sw=%s\n", model_, hardware_, software_);
      return;
    }
    BatteryData decoded;
    if (!JkBmsProtocol::decode(frame_, JkBmsProtocol::FrameSize, decoded, protocolHint_, millis())) {
      if (frame_[4] == 0x02) Serial.println("JK FRAME: wariant niejednoznaczny; w probe wymus p auto|24|32|04");
      return;
    }
    portENTER_CRITICAL(&dataMux_);
    battery_ = decoded;
    portEXIT_CRITICAL(&dataMux_);
    lastValidFrameMs_ = decoded.lastUpdateMs;
    if (lastLoggedMs_ == 0 || millis() - lastLoggedMs_ >= kLogIntervalMs) {
      Serial.printf("JK OK %s %.2fV %+.2fA %.0fW SOC %.0f%% cells=%u\n", batteryProtocolName(decoded.protocol),
                    decoded.packVoltageV, decoded.currentA, decoded.powerW, decoded.socPercent,
                    static_cast<unsigned>(decoded.cellCount));
      lastLoggedMs_ = millis();
    }
  }

  inline static Impl* active_ = nullptr;
  BLEScan* scan_ = nullptr;
  BLEClient* client_ = nullptr;
  BLERemoteCharacteristic* writeCharacteristic_ = nullptr;
  BLERemoteCharacteristic* notifyCharacteristic_ = nullptr;
  portMUX_TYPE dataMux_ = portMUX_INITIALIZER_UNLOCKED;
  BatteryData battery_{};
  BatteryProtocol protocolHint_ = BatteryProtocol::Unknown;
  volatile bool started_ = false;
  volatile bool scanning_ = false;
  volatile bool connecting_ = false;
  volatile bool connectRequested_ = false;
  volatile bool connected_ = false;
  bool autoConnect_ = true;
  bool verbose_ = false;
  char targetMac_[18]{};
  char candidateMac_[18]{};
  char connectedMac_[18]{};
  char model_[17]{};
  char hardware_[9]{};
  char software_[9]{};
  esp_ble_addr_type_t candidateAddressType_ = BLE_ADDR_TYPE_PUBLIC;
  uint8_t sequence_ = 0;
  uint8_t frame_[JkBmsProtocol::MaxFrameSize]{};
  size_t assemblerLength_ = 0;
  size_t headerMatch_ = 0;
  volatile uint32_t validFrames_ = 0;
  volatile uint32_t invalidFrames_ = 0;
  volatile uint32_t lastAttemptMs_ = 0;
  volatile uint32_t lastRequestMs_ = 0;
  volatile uint32_t lastValidFrameMs_ = 0;
  uint32_t lastLoggedMs_ = 0;
};

JkBmsBleDriver::JkBmsBleDriver() : impl_(new Impl()) {}
JkBmsBleDriver::~JkBmsBleDriver() { delete impl_; }
void JkBmsBleDriver::begin() { impl_->begin(Config{}); }
void JkBmsBleDriver::begin(const Config& config) { impl_->begin(config); }
void JkBmsBleDriver::tick(BatteryData& battery) { impl_->tick(battery); }
void JkBmsBleDriver::setVerbose(bool enabled) { impl_->setVerbose(enabled); }
void JkBmsBleDriver::setProtocolHint(BatteryProtocol protocol) { impl_->setProtocolHint(protocol); }
bool JkBmsBleDriver::connected() const { return impl_->connected(); }
const char* JkBmsBleDriver::mac() const { return impl_->mac(); }
const char* JkBmsBleDriver::deviceModel() const { return impl_->model(); }
const char* JkBmsBleDriver::hardwareVersion() const { return impl_->hardware(); }
const char* JkBmsBleDriver::softwareVersion() const { return impl_->software(); }
uint32_t JkBmsBleDriver::validFrames() const { return impl_->validFrames(); }
uint32_t JkBmsBleDriver::invalidFrames() const { return impl_->invalidFrames(); }

bool JkBmsBleDriver::looksLikeJk(const char* name, bool advertisesFfe0) {
  if (advertisesFfe0) return true;
  if (!name) return false;
  std::string upper(name);
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
  return upper.find("JK") != std::string::npos || upper.find("JIKONG") != std::string::npos ||
         upper.find("BMS") != std::string::npos;
}
