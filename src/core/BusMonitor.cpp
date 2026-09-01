#include "core/BusMonitor.h"

#include <Arduino.h>

namespace {
String lower(String text) {
  text.trim();
  text.toLowerCase();
  return text;
}
}

void BusMonitor::begin(Settings& settings, JkBmsDriver& jk, AnenjiDriver& anenji, PylontechEmulator& pylon) {
  settings_ = &settings;
  jk_ = &jk;
  anenji_ = &anenji;
  pylon_ = &pylon;
  lastJkMs_ = lastAnenjiMs_ = lastPylonMs_ = millis();
  Serial.println("Debug RS: help | debug jk | debug inv | debug pylon | debug off");
  printStatus();
}

void BusMonitor::tick() {
  if (!settings_) return;
  pollConsole();
  if (settings_->values.debugJk) {
    dumpUart("JK", jk_->serial(), Config::jkBaud, settings_->values.jkRxPin, lastJkMs_);
  }
  if (settings_->values.debugAnenji) {
    dumpUart("INV", anenji_->serial(), Config::anenjiBaud, settings_->values.anenjiRxPin, lastAnenjiMs_);
  }
  if (settings_->values.debugPylon) dumpPylon();
}

void BusMonitor::pollConsole() {
  while (Serial.available()) {
    const char next = static_cast<char>(Serial.read());
    if (next == '\n' || next == '\r') {
      if (line_.length()) handleCommand(line_);
      line_ = "";
    } else if (line_.length() < 48) {
      line_ += next;
    }
  }
}

void BusMonitor::handleCommand(String line) {
  line = lower(line);
  if (line == "help" || line == "?") {
    printHelp();
    return;
  }
  if (line == "debug") {
    printStatus();
    return;
  }

  bool* flag = nullptr;
  const char* name = nullptr;
  int mode = 0;  // 0 toggle, 1 on, -1 off, 2 all on, 3 all off

  if (line == "debug off") {
    mode = 3;
  } else if (line == "debug on" || line == "debug all") {
    mode = 2;
  } else if (line.startsWith("debug jk")) {
    flag = &settings_->values.debugJk;
    name = "JK";
    if (line.endsWith(" on")) mode = 1;
    else if (line.endsWith(" off")) mode = -1;
  } else if (line.startsWith("debug inv") || line.startsWith("debug anenji") || line.startsWith("debug falownik")) {
    flag = &settings_->values.debugAnenji;
    name = "INV";
    if (line.endsWith(" on")) mode = 1;
    else if (line.endsWith(" off")) mode = -1;
  } else if (line.startsWith("debug pylon")) {
    flag = &settings_->values.debugPylon;
    name = "PYLON";
    if (line.endsWith(" on")) mode = 1;
    else if (line.endsWith(" off")) mode = -1;
  } else {
    Serial.println("Nieznana komenda. Wpisz: help");
    return;
  }

  if (mode == 2 || mode == 3) {
    const bool enabled = mode == 2;
    settings_->values.debugJk = enabled;
    settings_->values.debugAnenji = enabled;
    settings_->values.debugPylon = enabled;
  } else if (flag) {
    *flag = mode == 0 ? !*flag : mode > 0;
    Serial.print(name);
    Serial.println(*flag ? ": debug ON" : ": debug OFF");
  }

  settings_->save();
  lastJkMs_ = lastAnenjiMs_ = lastPylonMs_ = millis();
  printStatus();
}

void BusMonitor::printHelp() const {
  Serial.println("Komendy debug RS (USB 115200):");
  Serial.println("  debug           status");
  Serial.println("  debug jk        wl/wyl JK BMS RS485");
  Serial.println("  debug inv       wl/wyl falownik RS232");
  Serial.println("  debug pylon     wl/wyl Pylontech RS485");
  Serial.println("  debug on|off    wszystkie");
  Serial.println("JK i falownik: hex ramek RX. Pylon: zliczanie zboczy na RX");
  Serial.println("(ESP32 ma 3 UART-y, USB zajmuje jeden, Pylon nie ma wolnego UART).");
}

void BusMonitor::printStatus() const {
  if (!settings_) return;
  Serial.print("Debug RS  JK=");
  Serial.print(settings_->values.debugJk ? "ON" : "off");
  Serial.print("  INV=");
  Serial.print(settings_->values.debugAnenji ? "ON" : "off");
  Serial.print("  PYLON=");
  Serial.println(settings_->values.debugPylon ? "ON" : "off");
}

void BusMonitor::dumpUart(const char* name, HardwareSerial& serial, uint32_t baud, int rxPin, uint32_t& lastActivityMs) {
  uint8_t buffer[64];
  size_t length = 0;
  while (serial.available() && length < sizeof(buffer)) {
    buffer[length++] = static_cast<uint8_t>(serial.read());
  }
  if (length) {
    lastActivityMs = millis();
    printDump(name, buffer, length);
    return;
  }
  if (millis() - lastActivityMs < Config::busDebugIdleMs) return;
  lastActivityMs = millis();
  Serial.print(name);
  Serial.print(" cisza (0 B / 5s) nasluch GPIO ");
  Serial.print(rxPin);
  Serial.print(" @ ");
  Serial.print(baud);
  Serial.println(" 8N1");
}

void BusMonitor::dumpPylon() {
  const uint32_t edges = pylon_->takeRxEdges();
  if (edges) {
    lastPylonMs_ = millis();
    Serial.print("PYLON RX aktywnosc: ");
    Serial.print(edges);
    Serial.print(" zboczy  GPIO ");
    Serial.println(settings_->values.pylonRxPin);
    return;
  }
  if (millis() - lastPylonMs_ < Config::busDebugIdleMs) return;
  lastPylonMs_ = millis();
  Serial.print("PYLON cisza (0 zboczy / 5s) nasluch GPIO ");
  Serial.print(settings_->values.pylonRxPin);
  Serial.println(" (RS485 RX)");
}

void BusMonitor::printDump(const char* name, const uint8_t* data, size_t length) {
  Serial.print(name);
  Serial.print(" RX ");
  Serial.print(length);
  Serial.print(" B  |");
  for (size_t index = 0; index < length; ++index) {
    if (data[index] < 16) Serial.print('0');
    Serial.print(data[index], HEX);
    Serial.print(index + 1 == length ? '|' : ' ');
  }
  Serial.print("  \"");
  for (size_t index = 0; index < length; ++index) {
    const char glyph = static_cast<char>(data[index]);
    Serial.print(glyph >= 32 && glyph <= 126 ? glyph : '.');
  }
  Serial.println('"');
}
