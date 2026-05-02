#include "eeprom_store.h"

#include <Arduino.h>
#include <EEPROM.h>

EepromStore& EepromStore::instance() {
  static EepromStore s;
  return s;
}

void EepromStore::readBytes(uint16_t addr, void* dest, size_t len) const {
  auto* p = static_cast<uint8_t*>(dest);
  for (size_t i = 0; i < len; i++) {
    p[i] = EEPROM.read(static_cast<int>(addr + i));
  }
}

void EepromStore::updateBytes(uint16_t addr, const void* src, size_t len) const {
  const auto* p = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < len; i++) {
    EEPROM.update(static_cast<int>(addr + i), p[i]);
  }
}
