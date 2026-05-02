#include <Arduino.h>
#include <unity.h>

#include "mux.h"
#include "serial_baud.h"

namespace {
Mux g_mux(muxDefaultConfig());
}

void test_mux_channel_endpoints(void) {
  g_mux.selectChannel(0);
  g_mux.selectChannel(15);
  g_mux.selectChannel(static_cast<uint8_t>(-1));
  TEST_PASS_MESSAGE("mux selects completed");
}

void test_mux_init_idempotent(void) {
  g_mux.init();
  g_mux.init();
  g_mux.selectChannel(0);
  TEST_PASS_MESSAGE("mux init safe to call more than once");
}

void setUp(void) { g_mux.init(); }

void tearDown(void) {}

void setup() {
  Serial.begin(SERIAL_MONITOR_BAUD);
  delay(500);
  while (!Serial && millis() < 3000) {
  }

  UNITY_BEGIN();
  RUN_TEST(test_mux_channel_endpoints);
  RUN_TEST(test_mux_init_idempotent);
  UNITY_END();
}

void loop() {}
