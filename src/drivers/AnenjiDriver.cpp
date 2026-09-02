#include "drivers/AnenjiDriver.h"

#include "drivers/AnenjiProtocol.h"

#include <Arduino.h>

namespace {
constexpr uint32_t kRxTimeoutMs = 800;
constexpr uint32_t kRxIdleMs = 40;
constexpr uint32_t kInterQueryMs = 40;
constexpr uint32_t kWakeupSettleMs = 200;
constexpr uint32_t kHelloRetryMs = 15000;
constexpr uint32_t kFailLogMs = 5000;
constexpr uint32_t kOkLogMs = 15000;
constexpr size_t kRxScratchSize = 96;
}

void AnenjiDriver::begin(const Settings& settings) {
  settings_ = &settings;
  enabled_ = false;
  wasOnline_ = false;
  invert_ = false;
  lastHelloMs_ = 0;
#if EMS_SIMULATION
  (void)settings;
  return;
#else
  if (settings.values.anenjiRxPin < 0 || settings.values.anenjiTxPin < 0) {
    Serial.println("ANENJI UART wylaczony (pin < 0)");
    return;
  }
  enabled_ = true;
  Serial.printf("ANENJI SMG-II slave=%u @ %lu 8N1 RX=%d TX=%d (RJ45 bez zamiany pin 1/2)\n",
                AnenjiProtocol::kSlave, static_cast<unsigned long>(Config::anenjiBaud),
                settings.values.anenjiRxPin, settings.values.anenjiTxPin);
  Serial.println("ANENJI: tylko FC03. Dongle wypiety. Najpierw jak dongle, bez wakeup.");
  hello();
#endif
}

void AnenjiDriver::openSerial(bool invert) {
  invert_ = invert;
  serial_.end();
  delay(50);
  serial_.setRxBufferSize(512);
  serial_.begin(Config::anenjiBaud, SERIAL_8N1, settings_->values.anenjiRxPin,
                settings_->values.anenjiTxPin, invert);
  delay(80);
  while (serial_.available()) serial_.read();
}

void AnenjiDriver::hello() {
  lastHelloMs_ = millis();
  openSerial(false);
  if (probeFault()) {
    Serial.println("ANENJI UART 9600 8N1, bez inwersji");
    return;
  }

  Serial.println("ANENJI: proba UART invert (MAX3232 / poziomy TTL)");
  openSerial(true);
  if (probeFault()) {
    Serial.println("ANENJI UART invert=ON dziala — zostawiam");
    return;
  }

  Serial.println("ANENJI: invert nie pomogl, wracam do normalnego UART i probe wakeup 01 AA");
  openSerial(false);
  serial_.write(AnenjiProtocol::kWakeupFrame, AnenjiProtocol::kWakeupSize);
  serial_.flush();
  printFrame("ANENJI TX wakeup", AnenjiProtocol::kWakeupFrame, AnenjiProtocol::kWakeupSize);
  delay(kWakeupSettleMs);
  uint8_t wakeRx[32]{};
  const size_t wakeLength = receive(wakeRx, sizeof(wakeRx), 250);
  if (wakeLength) printFrame("ANENJI RX wakeup", wakeRx, wakeLength);
  if (probeFault()) {
    Serial.println("ANENJI hello po wakeup OK");
    return;
  }
  Serial.println("ANENJI hello FAIL. RJ45 pin 1/2 zostaw jak bylo (zamiana = cisza).");
  Serial.println("Nastepne: zamien TTL GPIO32/33 w panelu (RX/TX) i zrestartuj.");
}

bool AnenjiDriver::probeFault() {
  uint8_t scratch[kRxScratchSize]{};
  size_t rxLength = 0;
  uint16_t faultRegs[AnenjiProtocol::kFaultRegisterCount]{};
  const bool ok = readHolding(AnenjiProtocol::kFaultFirstRegister, AnenjiProtocol::kFaultRegisterCount,
                              faultRegs, AnenjiProtocol::kFaultRegisterCount, scratch, sizeof(scratch),
                              rxLength);
  if (ok) {
    Serial.println("ANENJI hello FC03 100 OK");
    return true;
  }
  Serial.print("ANENJI hello FC03 100 FAIL ");
  if (rxLength) printFrame("RX", scratch, rxLength);
  else Serial.println("(cisza)");
  return false;
}

bool AnenjiDriver::poll(Telemetry& telemetry) {
#if EMS_SIMULATION
  (void)serial_;
  telemetry.anenjiOnline = false;
  return false;
#else
  if (!enabled_) {
    telemetry.anenjiOnline = false;
    return false;
  }

  if (!wasOnline_ && millis() - lastHelloMs_ >= kHelloRetryMs) hello();

  uint8_t scratch[kRxScratchSize]{};
  size_t liveLength = 0;
  uint16_t liveRegs[AnenjiProtocol::kLiveRegisterCount]{};
  if (!readHolding(AnenjiProtocol::kLiveFirstRegister, AnenjiProtocol::kLiveRegisterCount, liveRegs,
                   AnenjiProtocol::kLiveRegisterCount, scratch, sizeof(scratch), liveLength)) {
    markOffline(telemetry, liveLength ? "blok 200 zla ramka/CRC" : "blok 200 brak odpowiedzi", scratch,
                liveLength);
    return false;
  }

  AnenjiProtocol::LiveReading live{};
  if (!AnenjiProtocol::decodeLiveBlock(liveRegs, AnenjiProtocol::kLiveRegisterCount, live)) {
    markOffline(telemetry, "blok 200 niepelny", scratch, liveLength);
    return false;
  }

  delay(kInterQueryMs);

  size_t statusLength = 0;
  uint16_t statusRegs[AnenjiProtocol::kStatusRegisterCount]{};
  if (readHolding(AnenjiProtocol::kStatusFirstRegister, AnenjiProtocol::kStatusRegisterCount, statusRegs,
                  AnenjiProtocol::kStatusRegisterCount, scratch, sizeof(scratch), statusLength)) {
    AnenjiProtocol::decodeStatusBlock(statusRegs, AnenjiProtocol::kStatusRegisterCount, live);
  }

  telemetry.pvPowerW = live.pvPowerW;
  telemetry.loadPowerW = live.loadPowerW;
  telemetry.anenjiOnline = true;
  telemetry.updatedAtMs = millis();

  if (!wasOnline_ || shouldLog(kOkLogMs) || (settings_ && settings_->values.debugAnenji)) {
    Serial.printf("ANENJI OK PV=%.0fW load=%.0fW (%u%%) Vac=%.1f Vbat=%.1f Ibat=%.1f Vpv=%.1f %s%s\n",
                  live.pvPowerW, live.loadPowerW, live.loadPercent, live.outputVoltageV,
                  live.batteryVoltageV, live.batteryCurrentA, live.pvVoltageV,
                  AnenjiProtocol::operationModeName(live.operationMode), invert_ ? " invert" : "");
    lastLogMs_ = millis();
  }
  wasOnline_ = true;
  return true;
#endif
}

bool AnenjiDriver::readHolding(uint16_t firstRegister, uint16_t registerCount, uint16_t* registers,
                               size_t registerCapacity, uint8_t* rxScratch, size_t rxScratchSize,
                               size_t& rxLength) {
  rxLength = 0;
  uint8_t request[AnenjiProtocol::kRequestSize]{};
  if (!AnenjiProtocol::buildReadHolding(request, sizeof(request), AnenjiProtocol::kSlave, firstRegister,
                                        registerCount)) {
    return false;
  }

  while (serial_.available()) serial_.read();
  serial_.write(request, sizeof(request));
  serial_.flush();
  if (settings_ && settings_->values.debugAnenji) printFrame("TX", request, sizeof(request));

  rxLength = receive(rxScratch, rxScratchSize, kRxTimeoutMs);
  if (settings_ && settings_->values.debugAnenji && rxLength) printFrame("RX", rxScratch, rxLength);
  return AnenjiProtocol::extractReadHoldingResponse(rxScratch, rxLength, AnenjiProtocol::kSlave,
                                                    registerCount, registers, registerCapacity);
}

size_t AnenjiDriver::receive(uint8_t* buffer, size_t bufferSize, uint32_t timeoutMs) {
  size_t length = 0;
  const uint32_t startedAt = millis();
  uint32_t lastByteAt = startedAt;
  bool started = false;
  while (millis() - startedAt < timeoutMs) {
    while (serial_.available()) {
      const int value = serial_.read();
      if (value < 0) break;
      started = true;
      lastByteAt = millis();
      if (length < bufferSize) buffer[length++] = static_cast<uint8_t>(value);
    }
    if (started && millis() - lastByteAt >= kRxIdleMs) break;
    delay(1);
  }
  return length;
}

void AnenjiDriver::markOffline(Telemetry& telemetry, const char* reason, const uint8_t* rx,
                               size_t rxLength) {
  telemetry.anenjiOnline = false;
  telemetry.pvPowerW = 0;
  telemetry.loadPowerW = 0;
  if (wasOnline_ || shouldLog(kFailLogMs)) {
    Serial.print("ANENJI OFF ");
    Serial.print(reason);
    if (rxLength) {
      Serial.print(' ');
      printFrame("RX", rx, rxLength);
    } else {
      Serial.println();
    }
    lastLogMs_ = millis();
  }
  wasOnline_ = false;
}

void AnenjiDriver::printFrame(const char* label, const uint8_t* data, size_t length) const {
  Serial.print(label);
  Serial.print(" [");
  Serial.print(length);
  Serial.print(" B] ");
  for (size_t index = 0; index < length; ++index) {
    if (data[index] < 0x10) Serial.print('0');
    Serial.print(data[index], HEX);
    if (index + 1 != length) Serial.print(' ');
  }
  Serial.println();
}

bool AnenjiDriver::shouldLog(uint32_t intervalMs) const {
  return lastLogMs_ == 0 || millis() - lastLogMs_ >= intervalMs;
}
