#include "uart.h"

UartHall::UartHall()
    : ss_(static_cast<uint8_t>(HALL_UART_RX_PIN), static_cast<uint8_t>(HALL_UART_TX_PIN)) {}

void UartHall::begin(unsigned long baud) { ss_.begin(baud); }

size_t UartHall::writeBytes(const uint8_t* data, size_t len) { return ss_.write(data, len); }

void UartHall::flush() { ss_.flush(); }
