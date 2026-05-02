#if defined(PIO_UNIT_TESTING)

#include <Arduino.h>

#include "unity_config.h"

extern "C" void unity_output_char(const int c) {
  Serial.write(static_cast<uint8_t>(c));
}

extern "C" void unityOutputChar(const unsigned int c) {
  Serial.write(static_cast<uint8_t>(c));
}

extern "C" void unityOutputFlush(void) {}

extern "C" void unityOutputStart(const unsigned long baud) {
  (void)baud;
}

extern "C" void unityOutputComplete(void) {}

#endif
