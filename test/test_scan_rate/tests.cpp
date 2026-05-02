#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <unity.h>

#include "config.h"
#include "mux.h"
#include "serial_baud.h"

#ifndef SCAN_TEST_KEY_COUNT
#define SCAN_TEST_KEY_COUNT 10
#endif

static_assert(SCAN_TEST_KEY_COUNT >= 1 && SCAN_TEST_KEY_COUNT <= 16,
              "Single mux only: SCAN_TEST_KEY_COUNT must be 1..16 (one 4-bit address bus).");

#ifndef SCAN_TEST_ANALOG_PIN
#define SCAN_TEST_ANALOG_PIN PIN_MUX_ANALOG_IN
#endif

namespace {

constexpr size_t kNumKeys = SCAN_TEST_KEY_COUNT;
constexpr uint8_t kAnalogPin = SCAN_TEST_ANALOG_PIN;

Mux g_mux(muxDefaultConfig());

constexpr int kStableMaxDelta = 12;
constexpr uint16_t kBenchRounds = 80;
constexpr uint16_t kMinStableSearchMaxUs = 120;

void settleDelayUs(const uint16_t us) {
  if (us) {
    delayMicroseconds(us);
  }
}

bool stabilityOk(const uint16_t settleUs) {
  for (size_t i = 0; i < kNumKeys; i++) {
    g_mux.selectChannel(static_cast<uint8_t>(i));
    settleDelayUs(settleUs);
    (void)analogRead(kAnalogPin);
    const int a = analogRead(kAnalogPin);
    const int b = analogRead(kAnalogPin);
    if (abs(a - b) > kStableMaxDelta) {
      return false;
    }
  }
  return true;
}

float measureFullScanHz(const uint16_t settleUs) {
  const uint32_t t0 = micros();
  for (uint16_t r = 0; r < kBenchRounds; r++) {
    for (size_t i = 0; i < kNumKeys; i++) {
      g_mux.selectChannel(static_cast<uint8_t>(i));
      settleDelayUs(settleUs);
      (void)analogRead(kAnalogPin);
    }
  }
  const uint32_t dt = micros() - t0;
  const float scanUs = static_cast<float>(dt) / static_cast<float>(kBenchRounds);
  return 1000000.0f / scanUs;
}

void printBenchmarkRow(const uint16_t settleUs, const bool stable) {
  const float scansPerSec = measureFullScanHz(settleUs);
  const float scanUs = 1000000.0f / scansPerSec;
  const float samplesPerSec = scansPerSec * static_cast<float>(kNumKeys);

  Serial.print(F("settle "));
  Serial.print(settleUs);
  Serial.print(F(" us | "));
  Serial.print(scansPerSec, 1);
  Serial.print(F(" Hz scan | "));
  Serial.print(samplesPerSec, 0);
  Serial.print(F(" smpl/s | "));
  Serial.print(scanUs, 0);
  Serial.print(F(" us/scan | "));
  Serial.println(stable ? F("STAB OK") : F("STAB FAIL"));
}

uint16_t findMinStableSettleUs(const uint16_t maxUs) {
  for (uint16_t u = 0; u <= maxUs; u++) {
    if (stabilityOk(u)) {
      return u;
    }
  }
  return UINT16_MAX;
}

void runScanRateBenchmark() {
  g_mux.init();

  Serial.println();
  Serial.println(F("=== scan rate test — one mux only ==="));
  Serial.print(F("channels 0.."));
  Serial.print(static_cast<unsigned>(kNumKeys - 1));
  Serial.print(F(" | ADC "));
  Serial.print(kAnalogPin);
  Serial.print(F(" | stability: two reads same ch must match within "));
  Serial.print(kStableMaxDelta);
  Serial.println(F(" counts (after 1 discard read)"));

  constexpr uint16_t kSettles[] = {0, 1, 2, 5, 10, 20, 50};
  for (const uint16_t us : kSettles) {
    const bool ok = stabilityOk(us);
    printBenchmarkRow(us, ok);
  }

  const uint16_t minOk = findMinStableSettleUs(kMinStableSearchMaxUs);
  Serial.println();
  if (minOk == UINT16_MAX) {
    Serial.print(F("No stable settle in 0.."));
    Serial.print(kMinStableSearchMaxUs);
    Serial.println(F(" us — increase search max or kStableMaxDelta, check wiring."));
  } else {
    const float hz = measureFullScanHz(minOk);
    const float scanUs = 1000000.0f / hz;
    const float smpl = hz * static_cast<float>(kNumKeys);
    Serial.println(F("--- fastest stable (min settle that passes) ---"));
    Serial.print(F("settle "));
    Serial.print(minOk);
    Serial.print(F(" us | "));
    Serial.print(hz, 1);
    Serial.print(F(" Hz full scan | "));
    Serial.print(smpl, 0);
    Serial.print(F(" smpl/s | "));
    Serial.print(scanUs, 0);
    Serial.println(F(" us/scan"));
    Serial.print(F("main.cpp: delayMicroseconds("));
    Serial.print(minOk);
    Serial.println(F(");  // use this (or slightly higher) after each mux change"));
    Serial.println(
        F("If keys still look wrong, add a discard analogRead before storing."));
  }
  Serial.println();
}

void runExtendedSettleSweep() {
  g_mux.init();
  Serial.println();
  Serial.println(F("=== extended settle sweep (coarse table) ==="));
  for (uint16_t us = 0; us <= 100; us += 10) {
    const bool ok = stabilityOk(us);
    Serial.print(F("settle "));
    Serial.print(us);
    Serial.print(F(" us -> "));
    Serial.println(ok ? F("STAB OK") : F("STAB FAIL"));
  }
  Serial.println();
}

}  // namespace

void test_scan_rate_mux(void) {
  runScanRateBenchmark();
  TEST_PASS_MESSAGE("benchmark printed to Serial");
}

void test_scan_rate_min_stable_found(void) {
  g_mux.init();
  const uint16_t m = findMinStableSettleUs(kMinStableSearchMaxUs);
  if (m == UINT16_MAX) {
    TEST_FAIL_MESSAGE(
        "no stable settle in search window — wiring, rail, or increase "
        "kMinStableSearchMaxUs in test_scan_rate/tests.cpp");
    return;
  }
  TEST_ASSERT_TRUE_MESSAGE(stabilityOk(m), "min settle must pass stability check");
  if (m > 0) {
    TEST_ASSERT_FALSE_MESSAGE(stabilityOk(static_cast<uint16_t>(m - 1U)),
                              "min settle should be first passing us");
  }
}

void test_scan_rate_hz_sane_at_10us_settle(void) {
  g_mux.init();
  constexpr uint16_t kSettleUs = 10;
  const float hz = measureFullScanHz(kSettleUs);
  TEST_ASSERT_GREATER_THAN_FLOAT_MESSAGE(25.0f, hz,
                                         "full-scan Hz at 10us settle unexpectedly low");
}

void test_scan_rate_extended_settle_sweep(void) {
  runExtendedSettleSweep();
  TEST_PASS_MESSAGE("extended sweep printed");
}

void setUp(void) {
  g_mux.init();
}

void tearDown(void) {}

void setup() {
  Serial.begin(SERIAL_MONITOR_BAUD);
  delay(500);
  while (!Serial && millis() < 3000) {
  }

  UNITY_BEGIN();
  RUN_TEST(test_scan_rate_mux);
  RUN_TEST(test_scan_rate_min_stable_found);
  RUN_TEST(test_scan_rate_hz_sane_at_10us_settle);
  RUN_TEST(test_scan_rate_extended_settle_sweep);
  UNITY_END();
}

void loop() {}
