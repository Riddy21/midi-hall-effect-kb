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
