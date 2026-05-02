#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include <unity.h>

#include "eeprom_store.h"
#include "key_calibration.h"
#include "serial_baud.h"

namespace {

constexpr uint16_t kMagic = 0xCA1Bu;
constexpr uint8_t kVersion = 1;

constexpr uint16_t kGenericTestAddr = 600;
constexpr uint16_t kGenericScratchBytes = 80;
uint8_t g_genericBackup[kGenericScratchBytes];

constexpr size_t kCalSlotBackupBytes = 64;
uint8_t g_calBackup[kCalSlotBackupBytes];

void backupEepromCalibrationSlot() {
  EepromStore::instance().readBytes(EEPROM_CAL_BASE_ADDR, g_calBackup, kCalSlotBackupBytes);
}

void restoreEepromCalibrationSlot() {
  for (size_t i = 0; i < kCalSlotBackupBytes; i++) {
    EEPROM.write(static_cast<int>(EEPROM_CAL_BASE_ADDR + static_cast<int>(i)), g_calBackup[i]);
  }
}

void backupGenericSlot() {
  EepromStore::instance().readBytes(kGenericTestAddr, g_genericBackup, kGenericScratchBytes);
}

void restoreGenericSlot() {
  for (size_t i = 0; i < kGenericScratchBytes; i++) {
    EEPROM.write(static_cast<int>(kGenericTestAddr + i), g_genericBackup[i]);
  }
}

}  // namespace

void test_eeprom_defaults_shape(void) {
  uint16_t min[EEPROM_CAL_KEY_COUNT];
  uint16_t max[EEPROM_CAL_KEY_COUNT];
  eepromCalibrationDefaults(min, max);
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    TEST_ASSERT_EQUAL_UINT16(0, min[i]);
    TEST_ASSERT_EQUAL_UINT16(1023, max[i]);
  }
}

void test_eeprom_load_invalid_magic_returns_false_and_defaults_ram(void) {
  EEPROM.put(EEPROM_CAL_BASE_ADDR, static_cast<uint16_t>(0xFFFF));
  uint16_t min[EEPROM_CAL_KEY_COUNT];
  uint16_t max[EEPROM_CAL_KEY_COUNT];
  memset(min, 0xA5, sizeof(min));
  memset(max, 0xA5, sizeof(max));
  const bool ok = eepromCalibrationLoad(min, max);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT16(0, min[0]);
  TEST_ASSERT_EQUAL_UINT16(1023, max[EEPROM_CAL_KEY_COUNT - 1]);
}

void test_eeprom_generic_bytes_and_scalars(void) {
  constexpr size_t kGenericTestLen = 8;
  EepromStore& nv = EepromStore::instance();
  const uint8_t pat[] = {0x01, 0x02, 0xAA, 0x55, 0x00, 0xFF, 0x10, 0x20};
  nv.updateBytes(kGenericTestAddr, pat, kGenericTestLen);
  uint8_t got[kGenericTestLen];
  nv.readBytes(kGenericTestAddr, got, kGenericTestLen);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(pat, got, kGenericTestLen);

  constexpr uint16_t u16 = 0xCAFE;
  const uint16_t u16addr = static_cast<uint16_t>(kGenericTestAddr + 0x10);
  const EepromSlot<uint16_t> u16slot(u16addr);
  u16slot.write(u16);
  const uint16_t u16r = u16slot.read();
  TEST_ASSERT_EQUAL_UINT16(u16, u16r);

  constexpr uint32_t u32 = 0x11223344u;
  const uint16_t u32addr = static_cast<uint16_t>(kGenericTestAddr + 0x20);
  const EepromSlot<uint32_t> u32slot(u32addr);
  u32slot.write(u32);
  const uint32_t u32r = u32slot.read();
  TEST_ASSERT_EQUAL_UINT32(u32, u32r);
}

struct alignas(1) TestPod {
  uint8_t a;
  uint16_t b;
};
void test_eeprom_pod_read_write(void) {
  const uint16_t addr = static_cast<uint16_t>(kGenericTestAddr + 0x30);
  const EepromSlot<TestPod> slot(addr);
  TestPod w{};
  w.a = 0x7E;
  w.b = 0x4816;
  slot.write(w);
  const TestPod r = slot.read();
  TEST_ASSERT_EQUAL_UINT8(w.a, r.a);
  TEST_ASSERT_EQUAL_UINT16(w.b, r.b);
}

void test_eeprom_versioned_pod_helpers(void) {
  struct TagBlob {
    uint16_t magic;
    uint8_t version;
    uint8_t pad;
    uint32_t payload;
  };
  const uint16_t addr = static_cast<uint16_t>(kGenericTestAddr + 0x40);
  const EepromSlot<TagBlob> slot(addr);
  TagBlob w{};
  w.payload = 0xDEADBEEFu;
  slot.writeVersioned(w, kMagic, kVersion);
  TagBlob r{};
  TEST_ASSERT_TRUE(slot.readIfVersion(&r, kMagic, kVersion));
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, r.payload);
}

void test_eeprom_roundtrip(void) {
  uint16_t wMin[EEPROM_CAL_KEY_COUNT];
  uint16_t wMax[EEPROM_CAL_KEY_COUNT];
  eepromCalibrationDefaults(wMin, wMax);
  wMin[0] = 100;
  wMax[0] = 900;
  wMin[EEPROM_CAL_KEY_COUNT - 1] = 5;
  wMax[EEPROM_CAL_KEY_COUNT - 1] = 800;
  eepromCalibrationSave(wMin, wMax);

  uint16_t rMin[EEPROM_CAL_KEY_COUNT];
  uint16_t rMax[EEPROM_CAL_KEY_COUNT];
  const bool ok = eepromCalibrationLoad(rMin, rMax);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT16(100, rMin[0]);
  TEST_ASSERT_EQUAL_UINT16(900, rMax[0]);
  TEST_ASSERT_EQUAL_UINT16(5, rMin[EEPROM_CAL_KEY_COUNT - 1]);
  TEST_ASSERT_EQUAL_UINT16(800, rMax[EEPROM_CAL_KEY_COUNT - 1]);
}

void test_eeprom_layout_end_is_sane(void) {
  const uint16_t end = eepromCalibrationEndExclusive();
  TEST_ASSERT_GREATER_THAN_UINT16(EEPROM_CAL_BASE_ADDR, end);
  TEST_ASSERT_TRUE(end <= 1024);
}

void setUp(void) {
  backupEepromCalibrationSlot();
  backupGenericSlot();
}

void tearDown(void) {
  restoreGenericSlot();
  restoreEepromCalibrationSlot();
}

void setup() {
  Serial.begin(SERIAL_MONITOR_BAUD);
  delay(200);
  while (!Serial && millis() < 3000) {
  }

  UNITY_BEGIN();
  RUN_TEST(test_eeprom_defaults_shape);
  RUN_TEST(test_eeprom_load_invalid_magic_returns_false_and_defaults_ram);
  RUN_TEST(test_eeprom_generic_bytes_and_scalars);
  RUN_TEST(test_eeprom_pod_read_write);
  RUN_TEST(test_eeprom_versioned_pod_helpers);
  RUN_TEST(test_eeprom_roundtrip);
  RUN_TEST(test_eeprom_layout_end_is_sane);
  UNITY_END();
}

void loop() {}
