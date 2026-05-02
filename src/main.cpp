#include <Arduino.h>

#include "config.h"
#include "key_calibration.h"
#include "mux.h"
#include "serial_baud.h"

// Omitted in unit-test builds (PIO defines PIO_UNIT_TESTING); test/*/tests.cpp
// supplies setup() / loop().
#if !defined(PIO_UNIT_TESTING)

// Mux instance and wiring for this firmware (override muxDefaultConfig() or pass a custom MuxConfig).
static Mux g_mux(muxDefaultConfig());

// One mux: keys 0..KEY_COUNT-1 on the same address bus (see MUX_CHANNEL_COUNT in config.h).
static constexpr size_t KEY_COUNT = EEPROM_CAL_KEY_COUNT;
// Key table print rate (polling stays every loop).
static constexpr uint32_t SERIAL_REPORT_INTERVAL_MS = 50;
static uint16_t keyValues[KEY_COUNT];

static void pollAllKeys() {
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    g_mux.selectChannel(i);
    delayMicroseconds(10);
    const uint16_t v = analogRead(PIN_MUX_ANALOG_IN);
    keyValues[i] = v;
    keyCalibrationFeed(i, v);
  }
}

// One row of column headers (k0..k9), then pct row, then adc row (tab-separated).
static void printKeyTableColumns(uint32_t nowMs) {
  Serial.print(nowMs);
  Serial.println(F(" ms"));
  Serial.print(F("key"));
  for (size_t i = 0; i < KEY_COUNT; i++) {
    Serial.print('\t');
    Serial.print('k');
    Serial.print(i);
  }
  Serial.println();

  Serial.print(F("pct"));
  for (size_t i = 0; i < KEY_COUNT; i++) {
    Serial.print('\t');
    const uint8_t p = keyCalibrationDisplayPercent(i, keyValues[i]);
    if (p < 100) {
      Serial.print(' ');
    }
    if (p < 10) {
      Serial.print(' ');
    }
    Serial.print(p);
  }
  Serial.println();

  Serial.print(F("adc"));
  for (size_t i = 0; i < KEY_COUNT; i++) {
    Serial.print('\t');
    const uint16_t a = keyValues[i];
    if (a < 1000) {
      Serial.print(' ');
    }
    if (a < 100) {
      Serial.print(' ');
    }
    if (a < 10) {
      Serial.print(' ');
    }
    Serial.print(a);
  }
  Serial.println();
}

void setup() {
  keyCalibrationSetupPins();
  Serial.begin(SERIAL_MONITOR_BAUD);
  g_mux.init();
  keyCalibrationInit();
  Serial.print(F("hall_kb_poll "));
  Serial.print(SERIAL_REPORT_INTERVAL_MS);
  Serial.println(F(" ms row | columns: key / pct / adc"));
}

void loop() {
  static uint32_t lastReportMs = 0;

  keyCalibrationPoll();

  pollAllKeys();

  const uint32_t now = millis();
  if (now - lastReportMs >= SERIAL_REPORT_INTERVAL_MS) {
    lastReportMs = now;
    printKeyTableColumns(now);
    Serial.flush();
  }
}

#endif  // !PIO_UNIT_TESTING
