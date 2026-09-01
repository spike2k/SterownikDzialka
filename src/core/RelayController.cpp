#include "core/RelayController.h"

void RelayController::begin(Settings& settings) {
  settings_ = &settings;
  armedPins_.fill(-1);
  lastToggleMs_.fill(0);
  reconfigure();
}

void RelayController::reconfigure() {
  if (!settings_) return;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    releasePin(armedPins_[index]);
    armedPins_[index] = -1;
    states_[index] = false;
    lastToggleMs_[index] = 0;
  }
  for (size_t index = 0; index < Config::loadCount; ++index) {
    const int gpio = settings_->values.loads[index].pin;
    if (gpio < 0) continue;
    pinMode(gpio, OUTPUT);
    armedPins_[index] = static_cast<int8_t>(gpio);
    apply(index, false);
  }
}

void RelayController::tick(bool telemetryHealthy, float pvPowerW, float loadPowerW) {
  if (!telemetryHealthy) {
    allOff();
    return;
  }
  if (mode_ != ControlMode::Auto || !settings_) return;

  const auto& cfg = settings_->values;
  float localOnW = 0;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (states_[index] && cfg.loads[index].pin >= 0) localOnW += cfg.loads[index].powerW;
  }
  float remaining = pvPowerW - loadPowerW + localOnW - cfg.surplusReserveW;

  uint8_t order[Config::loadCount];
  size_t count = 0;
  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (Settings::loadActive(cfg.loads[index]) && cfg.loads[index].powerW > 0) {
      order[count++] = static_cast<uint8_t>(index);
    }
  }
  for (size_t a = 1; a < count; ++a) {
    const uint8_t candidate = order[a];
    size_t b = a;
    while (b > 0) {
      const uint8_t previous = order[b - 1];
      const bool previousGoesAfter =
          cfg.loads[previous].priority > cfg.loads[candidate].priority ||
          (cfg.loads[previous].priority == cfg.loads[candidate].priority && previous > candidate);
      if (!previousGoesAfter) break;
      order[b] = previous;
      --b;
    }
    order[b] = candidate;
  }

  const uint32_t now = millis();
  bool lockedOn[Config::loadCount] = {};
  for (size_t step = 0; step < count; ++step) {
    const size_t index = order[step];
    const bool canToggle = lastToggleMs_[index] == 0 || now - lastToggleMs_[index] >= cfg.loadMinToggleMs;
    if (states_[index] && !canToggle) {
      lockedOn[index] = true;
      remaining -= cfg.loads[index].powerW;
    }
  }

  bool want[Config::loadCount] = {};
  for (size_t step = 0; step < count; ++step) {
    const size_t index = order[step];
    if (lockedOn[index]) {
      want[index] = true;
      continue;
    }
    const float need = cfg.loads[index].powerW + (states_[index] ? 0.0f : static_cast<float>(cfg.loadHysteresisW));
    if (remaining >= need) {
      want[index] = true;
      remaining -= cfg.loads[index].powerW;
    }
  }

  for (size_t step = 0; step < count; ++step) {
    const size_t index = order[step];
    if (want[index] == states_[index]) continue;
    const bool canToggle = lastToggleMs_[index] == 0 || now - lastToggleMs_[index] >= cfg.loadMinToggleMs;
    if (!canToggle) continue;
    apply(index, want[index]);
  }

  for (size_t index = 0; index < Config::loadCount; ++index) {
    if (!Settings::loadActive(cfg.loads[index]) || cfg.loads[index].powerW == 0) {
      if (states_[index]) apply(index, false);
    }
  }
}

bool RelayController::setRelay(size_t index, bool enabled) {
  if (index >= Config::loadCount || mode_ != ControlMode::Manual || !settings_) return false;
  if (!Settings::loadActive(settings_->values.loads[index])) return false;
  apply(index, enabled);
  return true;
}

void RelayController::setMode(ControlMode mode) {
  mode_ = mode;
  if (mode_ == ControlMode::Auto) allOff();
}

ControlMode RelayController::mode() const { return mode_; }
bool RelayController::state(size_t index) const { return index < Config::loadCount && states_[index]; }
int RelayController::pin(size_t index) const {
  if (!settings_ || index >= Config::loadCount) return -1;
  return settings_->values.loads[index].pin;
}
bool RelayController::active(size_t index) const {
  if (!settings_ || index >= Config::loadCount) return false;
  return Settings::loadActive(settings_->values.loads[index]);
}

void RelayController::allOff() {
  for (size_t index = 0; index < Config::loadCount; ++index) apply(index, false);
}

void RelayController::apply(size_t index, bool enabled) {
  if (index >= Config::loadCount) return;
  if (states_[index] != enabled) lastToggleMs_[index] = millis();
  states_[index] = enabled;
  const int gpio = armedPins_[index];
  if (gpio < 0 || !settings_) return;
  const bool pinLevel = settings_->values.relayActiveLow ? !enabled : enabled;
  digitalWrite(gpio, pinLevel ? HIGH : LOW);
}

void RelayController::releasePin(int pin) {
  if (pin < 0) return;
  pinMode(pin, INPUT);
}
