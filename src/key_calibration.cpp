#include "key_calibration.h"

#include "config.h"
#include "button.h"
#include "eeprom_store.h"

#include <Arduino.h>

namespace {

constexpr uint16_t kEepromMagic = 0xCA1Bu;
constexpr uint8_t kEepromVersion = 1;

struct CalBlob {
  uint16_t magic;
  uint8_t version;
  uint8_t reserved;
  uint16_t calMin[EEPROM_CAL_KEY_COUNT];
  uint16_t calMax[EEPROM_CAL_KEY_COUNT];
};

const EepromSlot<CalBlob> kCalibrationSlot(EEPROM_CAL_BASE_ADDR);

}  // namespace

void eepromCalibrationDefaults(uint16_t minOut[EEPROM_CAL_KEY_COUNT],
                              uint16_t maxOut[EEPROM_CAL_KEY_COUNT]) {
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    minOut[i] = 0;
    maxOut[i] = 1023;
  }
}

bool eepromCalibrationLoad(uint16_t minOut[EEPROM_CAL_KEY_COUNT],
                          uint16_t maxOut[EEPROM_CAL_KEY_COUNT]) {
  CalBlob b;
  if (!kCalibrationSlot.readIfVersion(&b, kEepromMagic, kEepromVersion)) {
    eepromCalibrationDefaults(minOut, maxOut);
    return false;
  }
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    minOut[i] = b.calMin[i];
    maxOut[i] = b.calMax[i];
  }
  return true;
}

void eepromCalibrationSave(const uint16_t minIn[EEPROM_CAL_KEY_COUNT],
                          const uint16_t maxIn[EEPROM_CAL_KEY_COUNT]) {
  CalBlob b{};
  b.reserved = 0;
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    b.calMin[i] = minIn[i];
    b.calMax[i] = maxIn[i];
  }
  kCalibrationSlot.writeVersioned(b, kEepromMagic, kEepromVersion);
}

uint16_t eepromCalibrationEndExclusive(void) {
  return static_cast<uint16_t>(EEPROM_CAL_BASE_ADDR + sizeof(CalBlob));
}

static uint16_t s_calMin[EEPROM_CAL_KEY_COUNT];
static uint16_t s_calMax[EEPROM_CAL_KEY_COUNT];
static bool s_hadStored;
static bool s_calibrating;
static uint16_t s_sessMin[EEPROM_CAL_KEY_COUNT];
static uint16_t s_sessMax[EEPROM_CAL_KEY_COUNT];

static uint8_t mapRange(uint16_t raw, uint16_t lo, uint16_t hi) {
  if (hi <= lo) {
    return 50;
  }
  const int32_t lo32 = static_cast<int32_t>(lo);
  const int32_t hi32 = static_cast<int32_t>(hi);
  const int32_t span = hi32 - lo32;

  int32_t dzPct = static_cast<int32_t>(KEY_MAP_DEADZONE_PERCENT);
  if (dzPct < 0) {
    dzPct = 0;
  }
  if (dzPct > 49) {
    dzPct = 49;
  }
  const int32_t deadEach = (span * dzPct) / 100;
  int32_t effLo = lo32 + deadEach;
  int32_t effHi = hi32 - deadEach;
  if (effHi <= effLo) {
    effLo = lo32;
    effHi = hi32;
    if (effHi <= effLo) {
      return 50;
    }
  }

  int32_t x = static_cast<int32_t>(raw) - effLo;
  const int32_t effSpan = effHi - effLo;
  int32_t v = (x * 100 + effSpan / 2) / effSpan;
  if (v < 0) {
    return 0;
  }
  if (v > 100) {
    return 100;
  }
  return static_cast<uint8_t>(v);
}

void keyCalibrationInit() {
  s_hadStored = eepromCalibrationLoad(s_calMin, s_calMax);
  s_calibrating = false;
}

void keyCalibrationSetupPins() {
  pinMode(PIN_LED_CALIB, OUTPUT);
  digitalWrite(PIN_LED_CALIB, LOW);
  pinMode(PIN_CALIB_MODE, INPUT);
}

bool keyCalibrationHadStoredCalibration() { return s_hadStored; }

void keyCalibrationBegin() {
  s_calibrating = true;
  const uint16_t seed = static_cast<uint16_t>(KEY_CALIB_SESSION_SEED_ADC);
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    s_sessMin[i] = seed;
    s_sessMax[i] = seed;
  }
}

void keyCalibrationEndCommit() {
  if (!s_calibrating) {
    return;
  }
  const uint16_t seed = static_cast<uint16_t>(KEY_CALIB_SESSION_SEED_ADC);
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    if (s_sessMin[i] == seed && s_sessMax[i] == seed) {
      s_calMin[i] = 0;
      s_calMax[i] = 1023;
    } else {
      s_calMin[i] = s_sessMin[i];
      s_calMax[i] = s_sessMax[i];
    }
  }
  eepromCalibrationSave(s_calMin, s_calMax);
  s_hadStored = true;
  s_calibrating = false;
}

bool keyCalibrationIsActive() { return s_calibrating; }

static void syncCalHwLed() { digitalWrite(PIN_LED_CALIB, s_calibrating ? HIGH : LOW); }

static Button s_calModeButton(PIN_CALIB_MODE, PIN_CALIB_CALIBRATING_LEVEL);

void keyCalibrationPoll() {
  static bool s_calPinLogicReady = false;
  static bool s_prevCalPin = false;

  if (!s_calPinLogicReady) {
    if (millis() < static_cast<uint32_t>(CAL_PIN_BOOT_SETTLE_MS)) {
      syncCalHwLed();
      return;
    }
    s_calPinLogicReady = true;
    s_calModeButton.bootAlign();
    const bool wantCal = s_calModeButton.isPressed();
    s_prevCalPin = wantCal;
    if (wantCal) {
      keyCalibrationBegin();
    }
    syncCalHwLed();
    return;
  }

  const bool wantCal = s_calModeButton.poll(keyCalibrationIsActive());
  if (wantCal && !s_prevCalPin) {
    keyCalibrationBegin();
  } else if (!wantCal && s_prevCalPin) {
    if (keyCalibrationIsActive()) {
      keyCalibrationEndCommit();
    }
  }
  s_prevCalPin = wantCal;
  syncCalHwLed();
}

void keyCalibrationFeed(size_t keyIndex, uint16_t raw) {
  if (!s_calibrating || keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return;
  }
  if (raw < s_sessMin[keyIndex]) {
    s_sessMin[keyIndex] = raw;
  }
  if (raw > s_sessMax[keyIndex]) {
    s_sessMax[keyIndex] = raw;
  }
}

uint8_t keyCalibrationMap(size_t keyIndex, uint16_t raw) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }
  return mapRange(raw, s_calMin[keyIndex], s_calMax[keyIndex]);
}

uint8_t keyCalibrationDisplayPercent(size_t keyIndex, uint16_t raw) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }
  if (s_calibrating) {
    return mapRange(raw, s_sessMin[keyIndex], s_sessMax[keyIndex]);
  }
  return keyCalibrationMap(keyIndex, raw);
}

static bool sessionKeyHasSamples(size_t i) {
  const uint16_t seed = static_cast<uint16_t>(KEY_CALIB_SESSION_SEED_ADC);
  return !(s_sessMin[i] == seed && s_sessMax[i] == seed);
}

void keyCalibrationPrintSessionAdcRanges(Stream& out) {
  if (!s_calibrating) {
    return;
  }
  out.print(F("cal_adc_min"));
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    out.print('\t');
    if (sessionKeyHasSamples(i)) {
      out.print(s_sessMin[i]);
    } else {
      out.print(F("--"));
    }
  }
  out.println();
  out.print(F("cal_adc_max"));
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    out.print('\t');
    if (sessionKeyHasSamples(i)) {
      out.print(s_sessMax[i]);
    } else {
      out.print(F("--"));
    }
  }
  out.println();
}

void keyCalibrationPrintStoredAdcRanges(Stream& out) {
  out.println(F("cal: saved ADC (low .. high) per key:"));
  for (size_t i = 0; i < EEPROM_CAL_KEY_COUNT; i++) {
    out.print(F("  k"));
    out.print(i);
    out.print(F("  "));
    out.print(s_calMin[i]);
    out.print(F(" .. "));
    out.println(s_calMax[i]);
  }
}

uint16_t keyCalibrationStoredLo(size_t keyIndex) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }
  return s_calMin[keyIndex];
}

uint16_t keyCalibrationStoredHi(size_t keyIndex) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }
  return s_calMax[keyIndex];
}

bool keyCalibrationSessionKeyTouched(size_t keyIndex) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT || !s_calibrating) {
    return false;
  }
  return sessionKeyHasSamples(keyIndex);
}

uint16_t keyCalibrationSessionLo(size_t keyIndex) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }
  return s_sessMin[keyIndex];
}

uint16_t keyCalibrationSessionHi(size_t keyIndex) {
  if (keyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }
  return s_sessMax[keyIndex];
}
