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

// ── MIDI transport (compile-time; PlatformIO env in `platformio.ini`) ────────
//
// PlatformIO env / transport quick reference:
//
// | Env / board                     | MCU            | Transport                       |
// |----------------------------------|----------------|---------------------------------|
// | midi_serial / uno_midi_serial   | 328P Uno-class | MIDI_TRANSPORT_SERIAL (raw on Serial; pair with tools/serial_midi_bridge.py) |
// | midi_usb / midi_usb_micro / leonardo / micro | 32U4 | MIDI_TRANSPORT_USB (class-compliant USB-MIDI via MIDIUSB) |
// | pico_midi_serial                | RP2040         | MIDI_TRANSPORT_SERIAL           |
// | pico                            | RP2040         | MIDIOUT off by default          |
// | uno (default)                   | —              | MIDIOUT_ENABLED 0 (table only)  |

#include "serial_baud.h"

#if defined(PIO_UNIT_TESTING)
#undef MIDIOUT_ENABLED
#define MIDIOUT_ENABLED 0
#endif

#ifndef MIDIOUT_ENABLED
#define MIDIOUT_ENABLED 0
#endif

#if MIDIOUT_ENABLED && !defined(MIDI_TRANSPORT_USB) && !defined(MIDI_TRANSPORT_SERIAL)
#error "MIDIOUT_ENABLED requires MIDI_TRANSPORT_USB or MIDI_TRANSPORT_SERIAL — check platformio.ini"
#endif

#if defined(MIDI_TRANSPORT_USB) && defined(MIDI_TRANSPORT_SERIAL)
#error "Choose only one: MIDI_TRANSPORT_USB or MIDI_TRANSPORT_SERIAL"
#endif

#ifndef MIDI_SERIAL_BAUD
#define MIDI_SERIAL_BAUD SERIAL_MONITOR_BAUD
#endif

// ── MIDI: physical multiplex keys k0..k9 → MIDI note numbers ─────────────────
//
// Tune per-column MIDI behavior: **`MIDI_MAP_PHYSICAL_KEY*_NOTE`** selects the note sent
// for each hall column (`k0..k9`); **`MIDI_VOICE_CHANNEL`** 1–16. Note On fires once the
// calibrated 0–100 % strength leaves 0 (the rest deadzone is **`KEY_MAP_DEADZONE_PERCENT`**
// above), Note Off when it returns to 0.

#ifndef MIDI_VOICE_CHANNEL
// Host-visible MIDI channel 1–16.
#define MIDI_VOICE_CHANNEL 1
#endif

#ifndef MIDI_KEY_NOTE_BASE
// Reference when picking note numbers (defaults below are white keys ascending from “C near” 60).
#define MIDI_KEY_NOTE_BASE 60
#endif

// Hardware column order mux k0→k9. Defaults: ascending piano white keys starting at MIDI 60 (C4).

#ifndef MIDI_MAP_PHYSICAL_KEY00_NOTE
#define MIDI_MAP_PHYSICAL_KEY00_NOTE 60  // C4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY01_NOTE
#define MIDI_MAP_PHYSICAL_KEY01_NOTE 62  // D4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY02_NOTE
#define MIDI_MAP_PHYSICAL_KEY02_NOTE 64  // E4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY03_NOTE
#define MIDI_MAP_PHYSICAL_KEY03_NOTE 65  // F4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY04_NOTE
#define MIDI_MAP_PHYSICAL_KEY04_NOTE 67  // G4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY05_NOTE
#define MIDI_MAP_PHYSICAL_KEY05_NOTE 69  // A4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY06_NOTE
#define MIDI_MAP_PHYSICAL_KEY06_NOTE 71  // B4
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY07_NOTE
#define MIDI_MAP_PHYSICAL_KEY07_NOTE 72  // C5
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY08_NOTE
#define MIDI_MAP_PHYSICAL_KEY08_NOTE 74  // D5
#endif
#ifndef MIDI_MAP_PHYSICAL_KEY09_NOTE
#define MIDI_MAP_PHYSICAL_KEY09_NOTE 76  // E5
#endif

#if defined(ARDUINO_ARCH_RP2040)
#include "config_rp2040.h"
#endif
