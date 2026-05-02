#include "button.h"

#include <Arduino.h>

Button::Button(uint8_t pin, uint8_t activeLevel, const ButtonConfig& config)
    : pin_(pin),
      activeLevel_(activeLevel),
      config_(config),
      integrator_(0),
      rawOffStreak_(0) {}

void Button::bootAlign() {
  uint8_t activeVotes = 0;
  for (uint8_t i = 0; i < config_.bootSampleCycles; i++) {
    if (digitalRead(pin_) == activeLevel_) {
      activeVotes++;
    }
    delayMicroseconds(static_cast<unsigned int>(config_.bootSampleDelayUs));
  }
  integrator_ =
      (activeVotes >= config_.bootPressedMinVotes) ? config_.integratorMax : 0;
  rawOffStreak_ = 0;
}

bool Button::poll(const bool fastReleaseMode) {
  const bool rawPressed = digitalRead(pin_) == activeLevel_;

  if (fastReleaseMode && !rawPressed) {
    if (rawOffStreak_ < 255) {
      rawOffStreak_++;
    }
  } else {
    rawOffStreak_ = 0;
  }

  if (config_.fastOffStreakCount > 0 && fastReleaseMode &&
      rawOffStreak_ >= config_.fastOffStreakCount) {
    integrator_ = 0;
    return false;
  }

  if (rawPressed) {
    if (integrator_ < config_.integratorMax) {
      integrator_++;
    }
  } else if (integrator_ > 0) {
    integrator_--;
  }
  return isPressed();
}

bool Button::isPressed() const { return integrator_ >= config_.onThreshold; }
