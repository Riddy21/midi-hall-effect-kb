#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

static constexpr size_t EEPROM_CAL_KEY_COUNT = 10;
static constexpr uint16_t EEPROM_CAL_BASE_ADDR = 0;

void eepromCalibrationDefaults(uint16_t minOut[EEPROM_CAL_KEY_COUNT],
                              uint16_t maxOut[EEPROM_CAL_KEY_COUNT]);
bool eepromCalibrationLoad(uint16_t minOut[EEPROM_CAL_KEY_COUNT],
                          uint16_t maxOut[EEPROM_CAL_KEY_COUNT]);
void eepromCalibrationSave(const uint16_t minIn[EEPROM_CAL_KEY_COUNT],
                          const uint16_t maxIn[EEPROM_CAL_KEY_COUNT]);
uint16_t eepromCalibrationEndExclusive(void);

void keyCalibrationInit();
bool keyCalibrationHadStoredCalibration();

// pinMode for PIN_CALIB_MODE / PIN_LED_CALIB; call once from setup() before keyCalibrationInit().
void keyCalibrationSetupPins();
// Debounced cal switch, begin/end session, EEPROM commit, LED. Call each loop before keyCalibrationFeed().
void keyCalibrationPoll();

void keyCalibrationBegin();
// Copies session min/max into runtime storage and persists to EEPROM.
void keyCalibrationEndCommit();

bool keyCalibrationIsActive();
void keyCalibrationFeed(size_t keyIndex, uint16_t raw);

uint8_t keyCalibrationMap(size_t keyIndex, uint16_t raw);
// During active calibration, maps using the running session min/max (for display).
uint8_t keyCalibrationDisplayPercent(size_t keyIndex, uint16_t raw);

// Serial helpers (session/stored ranges are ADC counts 0..1023 on Uno).
void keyCalibrationPrintSessionAdcRanges(Stream& out);
void keyCalibrationPrintStoredAdcRanges(Stream& out);

uint16_t keyCalibrationStoredLo(size_t keyIndex);
uint16_t keyCalibrationStoredHi(size_t keyIndex);
bool keyCalibrationSessionKeyTouched(size_t keyIndex);
uint16_t keyCalibrationSessionLo(size_t keyIndex);
uint16_t keyCalibrationSessionHi(size_t keyIndex);
