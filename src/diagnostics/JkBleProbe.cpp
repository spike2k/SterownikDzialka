#include <Arduino.h>

#include "AppConfig.h"
#include "core/BatteryData.h"
#include "drivers/JkBmsBleDriver.h"

namespace {
JkBmsBleDriver driver;
BatteryData battery;
uint32_t lastPrintMs = 0;
String commandLine;

const char* onOff(bool value) { return value ? "ON" : "OFF"; }

void printBattery() {
  Serial.println();
  Serial.println("================ JK BATTERY DATA ================");
  Serial.printf("JK-BMS %s\n", battery.online ? "ONLINE" : (driver.connected() ? "CONNECTED / WAITING FOR DATA" : "OFFLINE"));
  Serial.printf("MAC: %s\n", driver.mac()[0] ? driver.mac() : "-");
  Serial.printf("MODEL: %s  HW: %s  SW: %s\n", driver.deviceModel()[0] ? driver.deviceModel() : "?",
                driver.hardwareVersion()[0] ? driver.hardwareVersion() : "?",
                driver.softwareVersion()[0] ? driver.softwareVersion() : "?");
  Serial.printf("Protocol: %s\n\n", batteryProtocolName(battery.protocol));
  if (!battery.online) {
    Serial.printf("FRAMES OK/BAD: %lu/%lu\n", static_cast<unsigned long>(driver.validFrames()),
                  static_cast<unsigned long>(driver.invalidFrames()));
    Serial.println("=================================================");
    return;
  }
  Serial.printf("PACK: %.3f V\n", battery.packVoltageV);
  Serial.printf("CURRENT: %+.3f A\n", battery.currentA);
  Serial.printf("POWER: %+.0f W\n", battery.powerW);
  Serial.printf("SOC: %.0f %%\n\n", battery.socPercent);
  Serial.printf("CELL MIN: %.3f V (#%u)\n", battery.minCellVoltageV, battery.minCellNumber);
  Serial.printf("CELL MAX: %.3f V (#%u)\n", battery.maxCellVoltageV, battery.maxCellNumber);
  Serial.printf("DELTA: %.0f mV\n\n", battery.deltaCellVoltageV * 1000.0f);
  Serial.println("CELLS:");
  for (size_t i = 0; i < battery.cellCount; ++i) Serial.printf("%2u: %.3f V\n", static_cast<unsigned>(i + 1), battery.cellVoltageV[i]);
  Serial.println("\nTEMPERATURES:");
  for (size_t i = 0; i < battery.temperatureCount; ++i) Serial.printf("T%u: %.1f C\n", static_cast<unsigned>(i + 1), battery.temperaturesC[i]);
  Serial.printf("\nMOS CHARGE: %s\n", onOff(battery.chargeMosOn));
  Serial.printf("MOS DISCHARGE: %s\n", onOff(battery.dischargeMosOn));
  Serial.printf("BALANCER: %s\n", onOff(battery.balancing));
  Serial.printf("ALARMS: 0x%08lX\n", static_cast<unsigned long>(battery.alarms));
  Serial.printf("AGE: %lu ms  FRAMES OK/BAD: %lu/%lu\n", static_cast<unsigned long>(millis() - battery.lastUpdateMs),
                static_cast<unsigned long>(driver.validFrames()), static_cast<unsigned long>(driver.invalidFrames()));
  Serial.println("=================================================");
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  BatteryProtocol hint = BatteryProtocol::Unknown;
  if (line == "p auto") hint = BatteryProtocol::Unknown;
  else if (line == "p 24") hint = BatteryProtocol::Jk02_24S;
  else if (line == "p 32") hint = BatteryProtocol::Jk02_32S;
  else if (line == "p 04") hint = BatteryProtocol::Jk04;
  else if (line == "show" || line == "s") {
    printBattery();
    return;
  } else {
    Serial.println("Komendy: show | p auto | p 24 | p 32 | p 04");
    return;
  }
  driver.setProtocolHint(hint);
  battery = BatteryData{};
  Serial.printf("Parser ustawiony: %s (czekam na kolejna ramke 0x02)\n", batteryProtocolName(hint));
}

void pollConsole() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (commandLine.length()) handleCommand(commandLine);
      commandLine = "";
    } else if (commandLine.length() < 24) {
      commandLine += c;
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("\nJK-BMS BLE PROBE");
  Serial.println("Skanuje bez blokowania petli, laczy automatycznie i pokazuje wszystkie notify w HEX.");
  Serial.println("Komendy: show | p auto | p 24 | p 32 | p 04");
  Serial.printf("Filtr MAC: %s\n", EMS_JK_BMS_MAC[0] ? EMS_JK_BMS_MAC : "AUTO");
  JkBmsBleDriver::Config config;
  config.mac = EMS_JK_BMS_MAC;
  config.verbose = true;
  driver.begin(config);
}

void loop() {
  driver.tick(battery);
  pollConsole();
  if (millis() - lastPrintMs >= 5000) {
    lastPrintMs = millis();
    printBattery();
  }
  delay(2);
}
