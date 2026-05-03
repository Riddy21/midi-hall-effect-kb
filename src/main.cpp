#include <Arduino.h>

#include "config.h"
#include "key_calibration.h"
#include "mux.h"
#include "serial_baud.h"

#if !defined(PIO_UNIT_TESTING)
#include "usb.h"
#include "uart.h"
#endif

// Omitted in unit-test builds (PIO defines PIO_UNIT_TESTING); test/*/tests.cpp
// supplies setup() / loop().
#if !defined(PIO_UNIT_TESTING)

// Mux instance and wiring for this firmware (override muxDefaultConfig() or pass a custom MuxConfig).
static Mux g_mux(muxDefaultConfig());

// One mux: keys 0..KEY_COUNT-1 on the same address bus (see MUX_CHANNEL_COUNT in config.h).
static constexpr size_t KEY_COUNT = EEPROM_CAL_KEY_COUNT;
// Key table print rate (polling stays every loop).
static constexpr uint32_t SERIAL_REPORT_INTERVAL_MS = 50;
// During calibration: USB Serial prints live session min/max ADC (readable in monitor).
static constexpr uint32_t CAL_SESSION_SERIAL_INTERVAL_MS = 200;
static uint16_t keyValues[KEY_COUNT];

static UsbHall g_usb;
static UartHall g_uart;
static float g_key_strength[KEY_COUNT];

static void pollAllKeys() {
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    g_mux.selectChannel(i);
    delayMicroseconds(10);
    const uint16_t v = analogRead(PIN_MUX_ANALOG_IN);
    keyValues[i] = v;
    keyCalibrationFeed(i, v);
  }
}

// Poll table: key row + pct row (same calibrated mapping as TX; TX sends 0..1 as fixed-point on wire).
#if !defined(HALL_SILENCE_POLL_TABLE)
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
}
#endif

void setup() {
  keyCalibrationSetupPins();
  hallStrengthTxInit(g_usb, g_uart);
  g_mux.init();
  keyCalibrationInit();
#if !defined(HALL_SILENCE_POLL_TABLE)
  Serial.print(F("hall_kb_poll "));
  Serial.print(SERIAL_REPORT_INTERVAL_MS);
  Serial.println(F(" ms row | columns: key / pct"));
#endif
}

void loop() {
  keyCalibrationPoll();

  pollAllKeys();

  const uint32_t now = millis();

  for (size_t i = 0; i < KEY_COUNT; i++) {
    g_key_strength[i] = keyCalibrationStrength(i, keyValues[i]);
  }
  // Stream silence during calibration so host bridges never see motion → no MIDI notes.
  if (!keyCalibrationIsActive()) {
    hallStrengthTxPoll(g_key_strength, KEY_COUNT, now);
  } else {
    static float silent_strength[KEY_COUNT]{};
    hallStrengthTxPoll(silent_strength, KEY_COUNT, now);
  }

  // ASCII session/stored ADC ranges on USB Serial (binary hall frames may still appear; host tools sync on magic).
  {
    static bool s_prevCalActive = false;
    static uint32_t s_lastCalSerialMs = 0;
    const bool calNow = keyCalibrationIsActive();
    if (calNow && !s_prevCalActive) {
      Serial.println(F("cal: session ADC ranges — cal_adc_min / cal_adc_max rows (tabs k0..k9); -- = not sampled yet"));
      s_lastCalSerialMs = 0;
    }
    if (!calNow && s_prevCalActive) {
      keyCalibrationPrintStoredAdcRanges(Serial);
      Serial.flush();
    }
    s_prevCalActive = calNow;
    if (calNow) {
      if (now - s_lastCalSerialMs >= CAL_SESSION_SERIAL_INTERVAL_MS) {
        s_lastCalSerialMs = now;
        keyCalibrationPrintSessionAdcRanges(Serial);
        Serial.flush();
      }
    }
  }

#if !defined(HALL_SILENCE_POLL_TABLE)
  static uint32_t lastReportMs = 0;
  if (now - lastReportMs >= SERIAL_REPORT_INTERVAL_MS) {
    lastReportMs = now;
    printKeyTableColumns(now);
    Serial.flush();
  }
#endif
}

#endif  // !PIO_UNIT_TESTING
