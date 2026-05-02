#pragma once

// On-chip EEPROM as objects. Include as "eeprom_store.h"; Arduino library is <EEPROM.h>.

#include <stddef.h>
#include <stdint.h>

class EepromStore {
public:
  static EepromStore& instance();

  void readBytes(uint16_t addr, void* dest, size_t len) const;
  void updateBytes(uint16_t addr, const void* src, size_t len) const;

  template<typename T>
  T readAs(uint16_t addr) const {
    T v{};
    readBytes(addr, &v, sizeof(T));
    return v;
  }

  template<typename T>
  void writeAs(uint16_t addr, const T& value) const {
    updateBytes(addr, &value, sizeof(T));
  }

  template<typename T>
  bool readIfVersion(uint16_t addr, T* out, uint16_t magic, uint8_t version) const {
    readBytes(addr, out, sizeof(T));
    return out->magic == magic && out->version == version;
  }

  template<typename T>
  void writeVersioned(uint16_t addr, const T& body, uint16_t magic, uint8_t version) const {
    T copy = body;
    copy.magic = magic;
    copy.version = version;
    writeAs(addr, copy);
  }

private:
  EepromStore() = default;
  EepromStore(const EepromStore&) = delete;
  EepromStore& operator=(const EepromStore&) = delete;
};

/**
 * Typed EEPROM window at a fixed base address. Use read(), write(), readIfVersion(),
 * writeVersioned() so the value type is explicit at the callsite.
 */
template<typename T>
class EepromSlot {
public:
  explicit constexpr EepromSlot(uint16_t base) : base_(base) {}

  T read() const { return EepromStore::instance().readAs<T>(base_); }

  void write(const T& value) const { EepromStore::instance().writeAs(base_, value); }

  bool readIfVersion(T* out, uint16_t magic, uint8_t version) const {
    return EepromStore::instance().readIfVersion(base_, out, magic, version);
  }

  void writeVersioned(const T& body, uint16_t magic, uint8_t version) const {
    EepromStore::instance().writeVersioned(base_, body, magic, version);
  }

  uint16_t address() const { return base_; }

private:
  uint16_t base_;
};
