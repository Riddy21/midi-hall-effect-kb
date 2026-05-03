#pragma once

//
// Hardware / tuning settings for this keyboard — pins, mux, calibration, etc.
//
// Mux defaults: MuxConfig / muxDefaultConfig() in mux.h reads these macros.
//

#include <Arduino.h>

#ifndef PIN_MUX_ENABLE
#define PIN_MUX_ENABLE 2
#endif

#ifndef MUX_CHANNEL_COUNT
#define MUX_CHANNEL_COUNT 16
#endif
#ifndef MUX_ADDRESS_BIT_COUNT
#define MUX_ADDRESS_BIT_COUNT 4
#endif

#ifndef PIN_MUX_ADDRESS_0
#define PIN_MUX_ADDRESS_0 3
#endif

#ifndef PIN_MUX_ADDRESS_1
#define PIN_MUX_ADDRESS_1 4
#endif

#ifndef PIN_MUX_ADDRESS_2
#define PIN_MUX_ADDRESS_2 5
#endif

#ifndef PIN_MUX_ADDRESS_3
#define PIN_MUX_ADDRESS_3 6
#endif

// ADC channel after MUX output (MCU analog input).
#ifndef PIN_MUX_ANALOG_IN
#define PIN_MUX_ANALOG_IN A0
#endif

// Calibration (pin 7 default): HIGH = calibrate, LOW = run / use EEPROM map.
// A plain INPUT floats; add ~10k from this pin to GND so LOW is a solid idle (run).
// Drive HIGH (tie to VCC or MCU-output partner) only while calibrating.
#ifndef PIN_CALIB_MODE
#define PIN_CALIB_MODE 7
#endif
#ifndef PIN_CALIB_CALIBRATING_LEVEL
#define PIN_CALIB_CALIBRATING_LEVEL HIGH
#endif

// Ignore cal-pin edges for this long after boot (ms).
#ifndef CAL_PIN_BOOT_SETTLE_MS
#define CAL_PIN_BOOT_SETTLE_MS 400
#endif

// Lit while calibration mode is active (hall scan / min-max capture).
#ifndef PIN_LED_CALIB
#define PIN_LED_CALIB LED_BUILTIN
#endif

// Each end of the calibrated ADC span (per-key min..max): this percent is "dead" before the
// mapped value leaves 0 (low end) or reaches 100 (high end). 15 → inner 70% of span → full 0..100.
// Use 0 for no deadzone. Keep <= 49 so an effective range remains.
#ifndef KEY_MAP_DEADZONE_PERCENT
#define KEY_MAP_DEADZONE_PERCENT 15
#endif

// When entering calibration, each key’s running min/max start here (ADC counts) until
// movement expands the range. Keys still min==max==seed at commit use 0..1023 defaults.
#ifndef KEY_CALIB_SESSION_SEED_ADC
#define KEY_CALIB_SESSION_SEED_ADC 700
#endif

// ── Hall strength TX (binary; host maps depth → MIDI / note logic / etc.) ───────
//
// Fixed frame (**24 bytes**, XOR checksum). Sent on USB CDC `Serial`
// **and** `SoftwareSerial` (`HALL_UART_*`), rate **`HALL_TX_INTERVAL_MS`**.
//
//   [0..1]   magic `HALL_TX_MAGIC0`, `HALL_TX_MAGIC1`
//   [2]      frame version **`HALL_TX_FRAME_VER_DEPTH_ONLY`** (calibrated depth only)
//   [3..22]  strength_q[0..9] uint16 LE each in **0 … `HALL_TX_STRENGTH_SCALE`**
//            → float depth = strength_q / **`HALL_TX_STRENGTH_SCALE`** (≈ 0.0 .. 1.0).
//            Scale defaults to **1023** on AVR (10-bit ADC class) and **4095** on RP2040 (12-bit).
//   [23]     XOR of bytes [2..22] inclusive
//
// Legacy ver **2** frames were **28 B** (included `millis()` before the same 10×uint16 payload).
//
// ASCII poll table (optional) may appear on USB only; parsers should scan for the magic pair.

#ifndef HALL_TX_MAGIC0
#define HALL_TX_MAGIC0 0xA5
#endif
#ifndef HALL_TX_MAGIC1
#define HALL_TX_MAGIC1 0x5A
#endif
#ifndef HALL_TX_FRAME_VER_STRENGTH_Q
#define HALL_TX_FRAME_VER_STRENGTH_Q 2 /* legacy: 28-byte frame with millis */
#endif
#ifndef HALL_TX_FRAME_VER_DEPTH_ONLY
#define HALL_TX_FRAME_VER_DEPTH_ONLY 3 /* current: 24-byte depth-only frame */
#endif

#if defined(ARDUINO_ARCH_RP2040)
#ifndef HALL_TX_STRENGTH_SCALE
#define HALL_TX_STRENGTH_SCALE 4095u
#endif
#else
#ifndef HALL_TX_STRENGTH_SCALE
#define HALL_TX_STRENGTH_SCALE 1023u
#endif
#endif

#ifndef HALL_TX_INTERVAL_MS
#define HALL_TX_INTERVAL_MS 10
#endif

#ifndef HALL_UART_RX_PIN
#define HALL_UART_RX_PIN 0
#endif

#ifndef HALL_UART_TX_PIN
#define HALL_UART_TX_PIN 1
#endif
