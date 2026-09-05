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
constexpr uint16_t kNotifyUuid = 0xFFE1;
constexpr uint16_t kWriteFallbackUuid = 0xFFE2;
constexpr uint32_t kScanSeconds = 8;
constexpr uint32_t kRetryMs = 5000;
constexpr uint32_t kDataTimeoutMs = 15000;
constexpr uint32_t kDeviceInfoRetryMs = 1500;
constexpr uint32_t kCellInfoRetryMs = 2500;
constexpr uint32_t kLogIntervalMs = 15000;

bool uuidEquals16(BLEUUID uuid, uint16_t shortUuid) {
  BLEUUID wanted(shortUuid);
  if (uuid.equals(wanted)) return true;
  return uuid.toString() == wanted.toString();
}

bool isKnownJkMacPrefix(const std::string& address) {
  return address.rfind("20:21:11:", 0) == 0 || address.rfind("c8:47:8c:", 0) == 0 ||
         address.rfind("c8:47:80:", 0) == 0;
}

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
    if (connected_ && lastNotifyMs_ && now - lastNotifyMs_ > kDataTimeoutMs) {
      connected_ = false;
      if (client_ && client_->isConnected()) client_->disconnect();
      Serial.println("JK BLE: timeout danych, ponowne laczenie");
    }
    if (connected_) pollCommands(now);
    if (!connected_ && !scanning_ && !connecting_ && !connectRequested_ && autoConnect_ &&
        now - lastAttemptMs_ >= kRetryMs) {
      startScan();
    }
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
    const bool jk = JkBmsBleDriver::looksLikeJk(name.c_str(), ffe0) || isKnownJkMacPrefix(address);
    Serial.printf("BLE  name=%s  MAC=%s  RSSI=%d%s\n", name.empty() ? "(brak)" : name.c_str(),
                  address.c_str(), device.getRSSI(), jk ? "  [JK-BMS?]" : "");
    const bool selected = targetMac_[0] ? strcasecmp(targetMac_, address.c_str()) == 0 : jk;
    if (!selected || connecting_ || connected_ || connectRequested_) return;
    std::snprintf(candidateMac_, sizeof(candidateMac_), "%s", address.c_str());
    candidateAddressType_ = device.getAddressType();
    connectRequested_ = true;
    scanning_ = false;
    if (scan_) {
      scan_->stop();
      scan_->clearResults();
    }
  }

  void onConnect(BLEClient*) override {}

  void onDisconnect(BLEClient*) override {
    connected_ = false;
    connecting_ = false;
    scanning_ = false;
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

  void abortConnect(const char* reason) {
    Serial.println(reason);
    writeCharacteristic_ = nullptr;
    notifyCharacteristic_ = nullptr;
    connected_ = false;
    connecting_ = false;
    scanning_ = false;
    lastAttemptMs_ = millis();
    if (client_ && client_->isConnected()) client_->disconnect();
  }

  void connectTask() {
    lastAttemptMs_ = millis();
    writeCharacteristic_ = nullptr;
    notifyCharacteristic_ = nullptr;
    Serial.printf("JK BLE: laczenie z %s...\n", candidateMac_);
    if (!client_) {
      client_ = BLEDevice::createClient();
      client_->setClientCallbacks(this);
    }
    if (!client_->connect(BLEAddress(candidateMac_), candidateAddressType_)) {
      abortConnect("JK BLE: polaczenie nieudane");
      return;
    }
    client_->setMTU(517);
    delay(200);
    // getServices() w dumpie niszczy wczesniej pobrane BLERemoteService — zrzut przed getService(FFE0).
    if (verbose_ && !dumpedServices_) {
      dumpServices();
      dumpedServices_ = true;
    }
    BLERemoteService* service = client_->getService(BLEUUID(kServiceUuid));
    if (!service) {
      abortConnect("JK BLE: brak service FFE0");
      return;
    }
    auto* chars = service->getCharacteristicsByHandle();
    if (!chars || chars->empty()) {
      abortConnect("JK BLE: service FFE0 bez charakterystyk");
      return;
    }
    BLERemoteCharacteristic* ffe2Write = nullptr;
    for (const auto& entry : *chars) {
      BLERemoteCharacteristic* characteristic = entry.second;
      const bool canWrite = characteristic->canWriteNoResponse() || characteristic->canWrite();
      const bool canNotify = characteristic->canNotify() || characteristic->canIndicate();
      if (uuidEquals16(characteristic->getUUID(), kNotifyUuid)) {
        if (!writeCharacteristic_ && canWrite) writeCharacteristic_ = characteristic;
        if (!notifyCharacteristic_ && canNotify) notifyCharacteristic_ = characteristic;
      } else if (uuidEquals16(characteristic->getUUID(), kWriteFallbackUuid) && canWrite) {
        ffe2Write = characteristic;
      }
    }
    if (!writeCharacteristic_ && ffe2Write) writeCharacteristic_ = ffe2Write;
    if (!writeCharacteristic_ || !notifyCharacteristic_) {
      abortConnect("JK BLE: nie znaleziono zapisu/notify w FFE0 (FFE1/FFE2)");
      return;
    }
    Serial.printf("JK BLE: write handle=0x%04X notify handle=0x%04X\n", writeCharacteristic_->getHandle(),
                  notifyCharacteristic_->getHandle());
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
    lastNotifyMs_ = 0;
    lastRequestMs_ = 0;
    gotDeviceInfo_ = false;
    gotCellStream_ = false;
    Serial.println("JK-BMS CONNECTED");
    Serial.printf("MAC: %s\n", connectedMac_);
    sendCommand(0x97);
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

  void sendCommand(uint8_t command) {
    if (!connected_ || !writeCharacteristic_) return;
    // ESPHome zostawia bajt 16 na 0; niezerowy licznik na 0x96 daje ACK C8, ale bez strumienia 0x02.
    const auto frame = JkBmsProtocol::buildReadCommand(command, 0);
    writeCharacteristic_->writeValue(const_cast<uint8_t*>(frame.data()), frame.size(), false);
    lastRequestMs_ = millis();
    if (verbose_) {
      if (command == 0x97) printHex("JK TX DEVICE INFO", frame.data(), frame.size());
      else if (command == 0x96) printHex("JK TX CELL STREAM", frame.data(), frame.size());
      else printHex("JK TX", frame.data(), frame.size());
    }
  }

  void pollCommands(uint32_t now) {
    if (gotCellStream_) return;
    if (!gotDeviceInfo_) {
      if (lastRequestMs_ == 0 || now - lastRequestMs_ >= kDeviceInfoRetryMs) sendCommand(0x97);
      return;
    }
    if (lastRequestMs_ == 0 || now - lastRequestMs_ >= kCellInfoRetryMs) sendCommand(0x96);
  }

  void onNotify(const uint8_t* data, size_t length) {
    lastNotifyMs_ = millis();
    if (verbose_) printHex("JK NOTIFY", data, length);
    if (length >= 7 && data[0] == 0xAA && data[1] == 0x55 && data[2] == 0x90 && data[3] == 0xEB && data[4] == 0xC8) {
      Serial.printf("JK CMD ACK %s\n", data[6] ? "OK" : "REJECT");
      return;
    }
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
      gotDeviceInfo_ = true;
      if (protocolHint_ == BatteryProtocol::Unknown && hardware_[0] == '1' && hardware_[1] == '1') {
        protocolHint_ = BatteryProtocol::Jk02_32S;
      }
      Serial.printf("JK DEVICE model=%s hw=%s sw=%s\n", model_, hardware_, software_);
      return;
    }
    BatteryData decoded;
    if (!JkBmsProtocol::decode(frame_, JkBmsProtocol::FrameSize, decoded, protocolHint_, millis())) {
      if (frame_[4] == 0x02) Serial.println("JK FRAME: wariant niejednoznaczny; w probe wymus p auto|24|32|04");
      if (frame_[4] == 0x01) Serial.println("JK FRAME: ustawienia 0x01 (strumien cel powinien ruszyc)");
      return;
    }
    gotCellStream_ = true;
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
  bool dumpedServices_ = false;
  volatile bool gotDeviceInfo_ = false;
  volatile bool gotCellStream_ = false;
  char targetMac_[18]{};
  char candidateMac_[18]{};
  char connectedMac_[18]{};
  char model_[17]{};
  char hardware_[9]{};
  char software_[9]{};
  esp_ble_addr_type_t candidateAddressType_ = BLE_ADDR_TYPE_PUBLIC;
  uint8_t frame_[JkBmsProtocol::MaxFrameSize]{};
  size_t assemblerLength_ = 0;
  size_t headerMatch_ = 0;
  volatile uint32_t validFrames_ = 0;
  volatile uint32_t invalidFrames_ = 0;
  volatile uint32_t lastAttemptMs_ = 0;
  volatile uint32_t lastRequestMs_ = 0;
  volatile uint32_t lastValidFrameMs_ = 0;
  volatile uint32_t lastNotifyMs_ = 0;
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
