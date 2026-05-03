#pragma once

#include "serial_baud.h"

//
// Firmware MIDI is gated by MIDIOUT_ENABLED (+ exactly one transport). PlatformIO selects this for you:
//
// | Environment        | MCU / board hint     | Transport            | Notes |
// |--------------------|----------------------|----------------------|-------|
// | midi_serial /      | Uno-class ATmega328P | MIDI_TRANSPORT_SERIAL | Host: tools/serial_midi_bridge.py or similar (@ SERIAL_MONITOR_BAUD). |
// |   uno_midi_serial  |                      |                     | Silence ASCII table → clean byte stream |
// | midi_usb / leonardo| ATmega32U4 Leonardo | MIDI_TRANSPORT_USB | Native USB MIDI (MIDIUSB) + CDC; no host bridge needed. |
// | midi_usb_micro /   | ATmega32U4 Micro     | Same as midi_usb | |
// | micro              |                      |                     | |
// | pico_midi_serial   | RP2040 Pico          | MIDI_TRANSPORT_SERIAL | Same host bridge; USB CDC serial on Pico. |
// | pico               | RP2040               | (none)              | Hall scan + table; MIDIOUT off by default. |
// | uno (default)      | Debug / cal only     | (none)              | MIDIOUT_ENABLED=0 |
//
// Swap board on the bench → change only the PIO environment; application source stays the same.
//

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

// Serial CDC MIDI baud (Uno bridge); keep aligned with SERIAL_MONITOR_BAUD / platformio.ini monitor_speed.
#ifndef MIDI_SERIAL_BAUD
#define MIDI_SERIAL_BAUD SERIAL_MONITOR_BAUD
#endif
