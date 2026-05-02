#include "mux.h"

#include <Arduino.h>

Mux::Mux(const MuxConfig& config) : cfg_(config) {
  if (cfg_.channelCount == 0) {
    cfg_.channelCount = 1;
  }
  if (cfg_.addressBits > 4) {
    cfg_.addressBits = 4;
  }
}

void Mux::init() {
  pinMode(cfg_.pinEnable, OUTPUT);
  digitalWrite(cfg_.pinEnable, LOW);
  for (uint8_t i = 0; i < cfg_.addressBits; i++) {
    pinMode(cfg_.addressPins[i], OUTPUT);
  }
  selectChannel(0);
}

void Mux::selectChannel(uint8_t channel) {
  const uint8_t n = cfg_.channelCount ? cfg_.channelCount : 1;
  channel = static_cast<uint8_t>(channel % n);
  for (uint8_t i = 0; i < cfg_.addressBits; i++) {
    digitalWrite(cfg_.addressPins[i], (channel & (1u << i)) ? HIGH : LOW);
  }
}
