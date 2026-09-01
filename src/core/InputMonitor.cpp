#include "core/InputMonitor.h"

void InputMonitor::begin(Settings& settings) {
  settings_ = &settings;
  reconfigure();
}

void InputMonitor::reconfigure() {
  if (!settings_) return;
  for (size_t index = 0; index < Config::statusCount; ++index) {
    const int gpio = settings_->values.statusPins[index];
    if (gpio < 0) continue;
    pinMode(gpio, gpio >= 34 ? INPUT : INPUT_PULLUP);
  }
}

bool InputMonitor::configured(size_t index) const {
  return pin(index) >= 0;
}

bool InputMonitor::active(size_t index) const {
  const int gpio = pin(index);
  if (gpio < 0 || !settings_) return false;
  const bool high = digitalRead(gpio) == HIGH;
  return settings_->values.statusActiveLow ? !high : high;
}

int InputMonitor::pin(size_t index) const {
  if (!settings_ || index >= Config::statusCount) return -1;
  return settings_->values.statusPins[index];
}
