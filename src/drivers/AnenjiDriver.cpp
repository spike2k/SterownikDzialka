#include "drivers/AnenjiDriver.h"

#include <Arduino.h>

void AnenjiDriver::begin(const Settings& settings) {
#if !EMS_SIMULATION
  serial_.begin(Config::anenjiBaud, SERIAL_8N1, settings.values.anenjiRxPin, settings.values.anenjiTxPin);
#else
  (void)settings;
#endif
}

bool AnenjiDriver::poll(Telemetry& telemetry) {
  (void)serial_;
  telemetry.anenjiOnline = false;
  return false;
}
