#pragma once

#include <stdint.h>

// Low-level outbound USB-MIDI via Arduino MIDIUSB (PluggableUSB, native USB board only — Leonardo-class).
// Same library as Arduino tutorial https://docs.arduino.cc/tutorials/generic/midi-device/
// Reference https://www.arduino.cc/reference/en/libraries/midiusb/
void usbMidiInit(void);
void usbMidiPoll(void);

// Full channel-voice or common 3-byte MIDI messages — status carries channel in low nibble.
bool usbMidiSend3(uint8_t status_byte, uint8_t data1, uint8_t data2);

// Program change / channel pressure — 2 data bytes sent on the wire as status + data1 (+ pad).
bool usbMidiSend2(uint8_t status_byte, uint8_t data1);
