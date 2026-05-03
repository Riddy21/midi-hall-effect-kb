#include "usb_midi.h"

#include "midi_config.h"

#include <Arduino.h>

#if MIDIOUT_ENABLED && defined(MIDI_TRANSPORT_USB) && !defined(PIO_UNIT_TESTING)
#include <MIDIUSB.h>
#endif

void usbMidiInit(void) {}

void usbMidiPoll(void) {
#if MIDIOUT_ENABLED && defined(MIDI_TRANSPORT_USB) && !defined(PIO_UNIT_TESTING)
  MidiUSB.flush();
  while (MidiUSB.available() != 0u) {
    (void)MidiUSB.read();
  }
#endif
}

#if MIDIOUT_ENABLED && defined(MIDI_TRANSPORT_USB) && !defined(PIO_UNIT_TESTING)

static bool sendPacket(uint8_t cin_low_nibble, uint8_t b1, uint8_t b2, uint8_t b3) {
  const midiEventPacket_t evt{(uint8_t)((0u << 4) | (cin_low_nibble & 0x0Fu)),
                              b1,
                              b2,
                              b3};
  MidiUSB.sendMIDI(evt);
  return true;
}

#endif

bool usbMidiSend3(uint8_t status_byte, uint8_t data1, uint8_t data2) {
#if MIDIOUT_ENABLED && defined(MIDI_TRANSPORT_USB) && !defined(PIO_UNIT_TESTING)
  const uint8_t cin = static_cast<uint8_t>((status_byte >> 4) & 0x0Fu);
  return sendPacket(cin, status_byte, data1, data2);
#else
  (void)status_byte;
  (void)data1;
  (void)data2;
  return false;
#endif
}

bool usbMidiSend2(uint8_t status_byte, uint8_t data1) {
#if MIDIOUT_ENABLED && defined(MIDI_TRANSPORT_USB) && !defined(PIO_UNIT_TESTING)
  const uint8_t cin = static_cast<uint8_t>((status_byte >> 4) & 0x0Fu);
  return sendPacket(cin, status_byte, data1, 0);
#else
  (void)status_byte;
  (void)data1;
  return false;
#endif
}
