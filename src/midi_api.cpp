#include "midi.h"

#include <Arduino.h>

// Single TU for outbound MIDI: same `midiApi*(channel, …)` API regardless of transport.
// `MIDI_TRANSPORT_USB` (Leonardo / Micro / class-compliant USB-MIDI) vs `MIDI_TRANSPORT_SERIAL`
// (Uno / Pico CDC raw MIDI bytes consumed by `tools/serial_midi_bridge.py`) chosen at compile time
// in `platformio.ini` (see `include/config.h` "MIDI transport"). MIDIUSB lib is only pulled when needed.

#if MIDIOUT_ENABLED && defined(MIDI_TRANSPORT_USB) && !defined(PIO_UNIT_TESTING)
#include <MIDIUSB.h>
#endif

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
  // USB-MIDI Cable 0; CIN nibble matches the high nibble of the status byte for channel-voice messages.
  const uint8_t cin = static_cast<uint8_t>((status >> 4) & 0x0Fu);
  midiEventPacket_t evt{cin, status, d1, d2};
  MidiUSB.sendMIDI(evt);
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
  const uint8_t cin = static_cast<uint8_t>((status >> 4) & 0x0Fu);
  midiEventPacket_t evt{cin, status, d1, 0};
  MidiUSB.sendMIDI(evt);
#elif defined(MIDI_TRANSPORT_SERIAL)
  Serial.write(status);
  Serial.write(d1);
#else
  (void)status;
  (void)d1;
#endif
}

#endif  // MIDIOUT_ENABLED && !PIO_UNIT_TESTING

}  // namespace

void midiApiBegin(void) {
  // USB-MIDI enumerates via Arduino USBCore automatically; Serial.begin lives in main.cpp setup().
}

void midiApiPoll(void) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
#if defined(MIDI_TRANSPORT_USB)
  MidiUSB.flush();
  while (MidiUSB.available() != 0u) {
    (void)MidiUSB.read();
  }
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
  midiApiControlChange(channel1to16, 123, 0);  // CC123 All Notes Off
#endif
}
