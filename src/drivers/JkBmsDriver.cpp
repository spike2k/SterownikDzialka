#include "drivers/JkBmsDriver.h"

#include <Arduino.h>

void JkBmsDriver::begin(const Settings& settings) {
#if !EMS_SIMULATION
  serial_.begin(Config::jkBaud, SERIAL_8N1, settings.values.jkRxPin, settings.values.jkTxPin);
#else
  (void)settings;
#endif
}

bool JkBmsDriver::poll(Telemetry& telemetry) {
#if EMS_SIMULATION
  telemetry.jkOnline = false;
  return false;
#else
  (void)serial_;
  telemetry.jkOnline = false;
  telemetry.cellCount = 0;
  return false;
#endif
}
