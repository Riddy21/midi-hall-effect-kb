#pragma once

// Configurable analog mux. Build MuxConfig (muxDefaultConfig() uses config.h macros) and own a Mux in main / tests.

#include "config.h"
#include <stdint.h>

struct MuxConfig {
  /** Selectable channels; selectChannel uses `channel % channelCount`. */
  uint8_t channelCount;
  /** Enable pin (drive LOW while selecting, matching the previous Uno sketch). */
  uint8_t pinEnable;
  /** How many address lines to drive (1..4). addressPins[0] is LSB. */
  uint8_t addressBits;
  uint8_t addressPins[4];
};

inline MuxConfig muxDefaultConfig() {
  MuxConfig c{};
  c.channelCount = static_cast<uint8_t>(MUX_CHANNEL_COUNT);
  c.pinEnable = static_cast<uint8_t>(PIN_MUX_ENABLE);
  c.addressBits = static_cast<uint8_t>(MUX_ADDRESS_BIT_COUNT);
  if (c.addressBits > 4) {
    c.addressBits = 4;
  }
  c.addressPins[0] = static_cast<uint8_t>(PIN_MUX_ADDRESS_0);
  c.addressPins[1] = static_cast<uint8_t>(PIN_MUX_ADDRESS_1);
  c.addressPins[2] = static_cast<uint8_t>(PIN_MUX_ADDRESS_2);
  c.addressPins[3] = static_cast<uint8_t>(PIN_MUX_ADDRESS_3);
  return c;
}

class Mux {
public:
  explicit Mux(const MuxConfig& config);

  void init();
  void selectChannel(uint8_t channel);

  const MuxConfig& config() const { return cfg_; }

private:
  MuxConfig cfg_;
};
