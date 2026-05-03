#pragma once

#include "config.h"
#include "usb.h"

#include <Arduino.h>
#include <SoftwareSerial.h>

class UartHall final : public HallBytePort {
 public:
  UartHall();

  void begin(unsigned long baud) override;
  size_t writeBytes(const uint8_t* data, size_t len) override;
  void flush() override;

 private:
  SoftwareSerial ss_;
};
