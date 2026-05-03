#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Byte sink for framed hall strength lines (USB CDC and/or UART).
class HallBytePort {
 public:
  virtual ~HallBytePort() = default;

  virtual void begin(unsigned long baud) = 0;
  virtual size_t writeBytes(const uint8_t* data, size_t len) = 0;
  virtual void flush() = 0;
};

class UsbHall final : public HallBytePort {
 public:
  void begin(unsigned long baud) override;
  size_t writeBytes(const uint8_t* data, size_t len) override;
  void flush() override;
};

class UartHall;

void hallStrengthTxInit(UsbHall& usb, UartHall& uart);
// Binary frame (**24 B**, ver `HALL_TX_FRAME_VER_DEPTH_ONLY`, float 0..1 as uint16/SCALE ×10). **`config.h`**.
void hallStrengthTxPoll(float const* strength_0to1, size_t key_count, uint32_t now_ms);
