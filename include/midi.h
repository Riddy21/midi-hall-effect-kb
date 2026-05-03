#pragma once

//
// ── MIDI for this sketch (API + keyboard map). Transport is fixed at compile time. ────────────
//
// Compile-time switches and the per-key note map live in `include/config.h`.

#include "config.h"
#include "key_calibration.h"

// Map multiplex column k0..k(N−1) to MIDI notes (edit **`MIDI_MAP_PHYSICAL_KEY*_NOTE`** macros in **`config.h`**).

inline uint8_t midiKeyboardPhysicalNote(size_t hardwareKeyIndex) {
  static constexpr uint8_t kNotes[EEPROM_CAL_KEY_COUNT] = {
      MIDI_MAP_PHYSICAL_KEY00_NOTE, MIDI_MAP_PHYSICAL_KEY01_NOTE, MIDI_MAP_PHYSICAL_KEY02_NOTE,
      MIDI_MAP_PHYSICAL_KEY03_NOTE, MIDI_MAP_PHYSICAL_KEY04_NOTE, MIDI_MAP_PHYSICAL_KEY05_NOTE,
      MIDI_MAP_PHYSICAL_KEY06_NOTE, MIDI_MAP_PHYSICAL_KEY07_NOTE, MIDI_MAP_PHYSICAL_KEY08_NOTE,
      MIDI_MAP_PHYSICAL_KEY09_NOTE,
  };

  static_assert(sizeof(kNotes) / sizeof(kNotes[0]) == EEPROM_CAL_KEY_COUNT,
                "midi: note map row count must match EEPROM_CAL_KEY_COUNT");

  if (hardwareKeyIndex >= EEPROM_CAL_KEY_COUNT) {
    return 0;
  }

  uint8_t n = kNotes[hardwareKeyIndex];
  if (n > 127u) {
    n = 127;
  }
  return n;
}

void midiApiBegin(void);
void midiApiPoll(void);

void midiApiNoteOff(uint8_t channel1to16, uint8_t note, uint8_t velocity);
void midiApiNoteOn(uint8_t channel1to16, uint8_t note, uint8_t velocity);
void midiApiControlChange(uint8_t channel1to16, uint8_t cc, uint8_t value);
void midiApiProgramChange(uint8_t channel1to16, uint8_t program0to127);
void midiApiChannelPressure(uint8_t channel1to16, uint8_t pressure);
void midiApiPitchBend14(uint8_t channel1to16, uint16_t value14center8192);
void midiApiAllNotesOff(uint8_t channel1to16);

void midiKeyScanInit(void);

// Caller passes pre-calibrated [0.0, 1.0] strengths (e.g. via `keyCalibrationMap`).
void midiKeyScanPoll(float const* strength_per_key_0to1, size_t key_count);
