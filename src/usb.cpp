#include "usb.h"

#include "uart.h"

#include "config.h"
#include "key_calibration.h"
#include "serial_baud.h"

#include <Arduino.h>

namespace {

UsbHall* s_usb = nullptr;
UartHall* s_uart = nullptr;

constexpr size_t kHallBinaryFrameBytes = 24;
// XOR covers bytes [2..22] inclusive (version + 10×uint16)

void writeBothBytes(const uint8_t* data, size_t len) {
  if (s_usb) {
    s_usb->writeBytes(data, len);
  }
  if (s_uart) {
    s_uart->writeBytes(data, len);
  }
}

static void putU16Le(uint8_t* dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFFu);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

}  // namespace

void UsbHall::begin(unsigned long baud) { Serial.begin(baud); }

size_t UsbHall::writeBytes(const uint8_t* data, size_t len) { return Serial.write(data, len); }

void UsbHall::flush() { Serial.flush(); }

void hallStrengthTxInit(UsbHall& usb, UartHall& uart) {
  s_usb = &usb;
  s_uart = &uart;
  const unsigned long baud = static_cast<unsigned long>(SERIAL_MONITOR_BAUD);
  usb.begin(baud);
  uart.begin(baud);
}

void hallStrengthTxPoll(float const* strength_0to1, size_t key_count, uint32_t now_ms) {
  if (!s_usb || !s_uart || !strength_0to1 || key_count == 0) {
    return;
  }

  static uint32_t s_last_ms = 0;
  if (static_cast<uint32_t>(now_ms - s_last_ms) < static_cast<uint32_t>(HALL_TX_INTERVAL_MS)) {
    return;
  }
  s_last_ms = now_ms;

  uint8_t pkt[kHallBinaryFrameBytes];
  pkt[0] = static_cast<uint8_t>(HALL_TX_MAGIC0);
  pkt[1] = static_cast<uint8_t>(HALL_TX_MAGIC1);
  pkt[2] = static_cast<uint8_t>(HALL_TX_FRAME_VER_DEPTH_ONLY);

  const size_t n = key_count > EEPROM_CAL_KEY_COUNT ? EEPROM_CAL_KEY_COUNT : key_count;
  const float scaleF = static_cast<float>(HALL_TX_STRENGTH_SCALE);

  for (size_t i = 0; i < n; i++) {
    float s = strength_0to1[i];
    if (s < 0.0f) {
      s = 0.0f;
    }
    if (s > 1.0f) {
      s = 1.0f;
    }
    uint32_t q = static_cast<uint32_t>(s * scaleF + 0.5f);
    if (q > static_cast<uint32_t>(HALL_TX_STRENGTH_SCALE)) {
      q = static_cast<uint32_t>(HALL_TX_STRENGTH_SCALE);
    }
    putU16Le(&pkt[3 + 2 * i], static_cast<uint16_t>(q));
  }
  for (size_t i = n; i < EEPROM_CAL_KEY_COUNT; i++) {
    putU16Le(&pkt[3 + 2 * i], 0);
  }

  uint8_t x = 0;
  for (size_t i = 2; i < 23; i++) {
    x ^= pkt[i];
  }
  pkt[23] = x;

  writeBothBytes(pkt, kHallBinaryFrameBytes);
  s_usb->flush();
  s_uart->flush();
}
