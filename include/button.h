#pragma once

#include <stdint.h>

struct ButtonConfig {
  uint8_t integratorMax = 8;
  uint8_t onThreshold = 4;
  /** When `poll(fastRelease)` has fastRelease true, this many consecutive “off” raw reads force immediate release. */
  uint8_t fastOffStreakCount = 3;
  uint8_t bootSampleCycles = 40;
  uint8_t bootPressedMinVotes = 21;  // pressed if strictly more than half of 40 → hiVotes > 20
  uint16_t bootSampleDelayUs = 200;
};

/**
 * Debounced digital input: active when pin reads `activeLevel` (HIGH or LOW) for enough integrate steps.
 * `bootAlign()` does a short burst of samples after boot. Call `poll()` each loop.
 */
class Button {
public:
  Button(uint8_t pin, uint8_t activeLevel, const ButtonConfig& config = ButtonConfig());

  /** Sample pin many times quickly; sets internal debouncer to pressed or released. */
  void bootAlign();

  /**
   * Read pin and update debouncer. Returns debounced pressed state.
   * If fastReleaseMode is true (e.g. parent “session” wants a quick exit), consecutive raw-off
   * samples shorten debounce decay so edges are not delayed.
   */
  bool poll(bool fastReleaseMode);

  bool isPressed() const;

private:
  uint8_t pin_;
  uint8_t activeLevel_;
  ButtonConfig config_;
  uint8_t integrator_;
  uint8_t rawOffStreak_;
};
