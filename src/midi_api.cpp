#include "midi_api.h"

#include <Arduino.h>

#include "midi_config.h"
#include "usb_midi.h"

namespace {

#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)

constexpr uint8_t kChannelMask = 0x0Fu;

uint8_t channelIndex(uint8_t channel1to16) {
  uint8_t c = channel1to16;
  if (c < 1) {
    c = 1;
  }
  if (c > 16) {
    c = 16;
  }
  return static_cast<uint8_t>((c - 1u) & kChannelMask);
}

void emit3Raw(uint8_t status, uint8_t d1, uint8_t d2) {
#if defined(MIDI_TRANSPORT_USB)
  usbMidiSend3(status, d1, d2);
#elif defined(MIDI_TRANSPORT_SERIAL)
  Serial.write(status);
  Serial.write(d1);
  Serial.write(d2);
#else
  (void)status;
  (void)d1;
  (void)d2;
#endif
}

void emit2Raw(uint8_t status, uint8_t d1) {
#if defined(MIDI_TRANSPORT_USB)
  usbMidiSend2(status, d1);
#elif defined(MIDI_TRANSPORT_SERIAL)
  Serial.write(status);
  Serial.write(d1);
#else
  (void)status;
  (void)d1;
#endif
}

#else

// Empty when MIDI output omitted — keeps TU valid for Uno/tests.

#endif  // MIDIOUT_ENABLED && !PIO_UNIT_TESTING

}  // namespace

void midiApiBegin(void) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
#if defined(MIDI_TRANSPORT_USB)
  usbMidiInit();
#endif
#endif
}

void midiApiPoll(void) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
#if defined(MIDI_TRANSPORT_USB)
  usbMidiPoll();
#elif defined(MIDI_TRANSPORT_SERIAL)
  Serial.flush();
#endif
#endif
}

void midiApiNoteOff(uint8_t channel1to16, uint8_t note, uint8_t velocity) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  const uint8_t ch = channelIndex(channel1to16);
  const uint8_t n = note > 127u ? 127u : note;
  const uint8_t v = velocity > 127u ? 127u : velocity;
  emit3Raw(static_cast<uint8_t>(0x80u | ch), n, v);
#endif
}

void midiApiNoteOn(uint8_t channel1to16, uint8_t note, uint8_t velocity) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  const uint8_t ch = channelIndex(channel1to16);
  const uint8_t n = note > 127u ? 127u : note;
  uint8_t v = velocity > 127u ? 127u : velocity;
  if (v == 0) {
    midiApiNoteOff(channel1to16, n, 64);
    return;
  }
  emit3Raw(static_cast<uint8_t>(0x90u | ch), n, v);
#endif
}

void midiApiControlChange(uint8_t channel1to16, uint8_t cc, uint8_t value) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  const uint8_t ch = channelIndex(channel1to16);
  const uint8_t c = cc > 127u ? 127u : cc;
  const uint8_t val = value > 127u ? 127u : value;
  emit3Raw(static_cast<uint8_t>(0xB0u | ch), c, val);
#endif
}

void midiApiProgramChange(uint8_t channel1to16, uint8_t program0to127) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  const uint8_t ch = channelIndex(channel1to16);
  const uint8_t p = program0to127 > 127u ? 127u : program0to127;
  emit2Raw(static_cast<uint8_t>(0xC0u | ch), p);
#endif
}

void midiApiChannelPressure(uint8_t channel1to16, uint8_t pressure) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  const uint8_t ch = channelIndex(channel1to16);
  const uint8_t pres = pressure > 127u ? 127u : pressure;
  emit2Raw(static_cast<uint8_t>(0xD0u | ch), pres);
#endif
}

void midiApiPitchBend14(uint8_t channel1to16, uint16_t value14center8192) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  const uint8_t ch = channelIndex(channel1to16);
  uint16_t u = value14center8192;
  if (u > 16383u) {
    u = 16383u;
  }
  const uint8_t lsb = static_cast<uint8_t>(u & 0x7Fu);
  const uint8_t msb = static_cast<uint8_t>((u >> 7) & 0x7Fu);
  emit3Raw(static_cast<uint8_t>(0xE0u | ch), lsb, msb);
#endif
}

void midiApiAllNotesOff(uint8_t channel1to16) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  midiApiControlChange(channel1to16, 123, 0);  // MIDI CC123 All Notes Off
#endif
}
