#pragma once

#include <stdint.h>

// High-level outbound MIDI helpers. Transport is picked at compile time — see midi_config.h
// (-DMIDI_TRANSPORT_USB on Leonardo-class, -DMIDI_TRANSPORT_SERIAL raw bytes on Serial).

void midiApiBegin(void);
void midiApiPoll(void);

// channel: MIDI convention 1..16 — notes and values are clamped into valid MIDI ranges.
void midiApiNoteOff(uint8_t channel1to16, uint8_t note, uint8_t velocity);

void midiApiNoteOn(uint8_t channel1to16, uint8_t note, uint8_t velocity);

void midiApiControlChange(uint8_t channel1to16, uint8_t cc, uint8_t value);

void midiApiProgramChange(uint8_t channel1to16, uint8_t program0to127);

void midiApiChannelPressure(uint8_t channel1to16, uint8_t pressure);

// value14: 14-bit bend where 8192 means center (matching host tools).
void midiApiPitchBend14(uint8_t channel1to16, uint16_t value14center8192);

void midiApiAllNotesOff(uint8_t channel1to16);
