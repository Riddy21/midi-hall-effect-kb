#include "midi.h"

#include <Arduino.h>
#include <string.h>

#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)

namespace {

constexpr size_t kKeyCap = EEPROM_CAL_KEY_COUNT;

static bool s_active[kKeyCap];

uint8_t channel1to16Clamped(void) {
  unsigned c = static_cast<unsigned>(MIDI_VOICE_CHANNEL);
  if (c < 1u) {
    c = 1u;
  }
  if (c > 16u) {
    c = 16u;
  }
  return static_cast<uint8_t>(c);
}

uint8_t velocityFromStrength(float strength0to1) {
  if (strength0to1 < 0.0f) {
    strength0to1 = 0.0f;
  }
  if (strength0to1 > 1.0f) {
    strength0to1 = 1.0f;
  }
  int v = static_cast<int>(strength0to1 * 127.0f + 0.5f);
  // Note On with velocity 0 means Note Off — caller already gated strength > 0, so floor at 1.
  if (v < 1) {
    return 1;
  }
  if (v > 127) {
    return 127;
  }
  return static_cast<uint8_t>(v);
}

}  // namespace

#endif  // MIDIOUT_ENABLED && !PIO_UNIT_TESTING

void midiKeyScanInit(void) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  memset(s_active, 0, sizeof(s_active));
#endif
}

void midiKeyScanPoll(float const* strength_per_key_0to1, size_t key_count) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  if (!strength_per_key_0to1) {
    return;
  }

  const uint8_t ch = channel1to16Clamped();
  const size_t n = key_count > kKeyCap ? kKeyCap : key_count;

  if (keyCalibrationIsActive()) {
    return;
  }

  for (size_t i = 0; i < n; i++) {
    const float strength = strength_per_key_0to1[i];
    const uint8_t note = midiKeyboardPhysicalNote(i);
    const bool held = s_active[i];

    // Calibration already applies KEY_MAP_DEADZONE_PERCENT at both ends of the span,
    // so mapRange() returns exact 0.0f at rest and >0 once the key leaves the deadzone.
    if (!held && strength > 0.0f) {
      midiApiNoteOn(ch, note, velocityFromStrength(strength));
      s_active[i] = true;
    } else if (held && strength == 0.0f) {
      midiApiNoteOff(ch, note, 0);
      s_active[i] = false;
    }
  }

#else

  (void)strength_per_key_0to1;
  (void)key_count;

#endif
}
