#include "midi_key_scan.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "key_calibration.h"
#include "midi_config.h"

#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
#include "midi_api.h"
#endif

static_assert(static_cast<unsigned>(MIDI_NOTE_OFF_PERCENT) <
                  static_cast<unsigned>(MIDI_NOTE_ON_PERCENT),
              "midi_key_scan hysteresis expects MIDI_NOTE_OFF_PERCENT < MIDI_NOTE_ON_PERCENT");

#if MIDIOUT_ENABLED

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

uint8_t velocityFromStrength(uint8_t strength0to100) {
  uint16_t v = (static_cast<uint16_t>(strength0to100) * 127u + 50u) / 100u;
  if (v < 1u) {
    return 1;
  }
  if (v > 127u) {
    return 127;
  }
  return static_cast<uint8_t>(v);
}

uint8_t noteForKey(size_t keyIndex) {
  const unsigned base = static_cast<unsigned>(MIDI_KEY_NOTE_BASE);
  unsigned n = base + static_cast<unsigned>(keyIndex);
  if (n > 127u) {
    n = 127u;
  }
  return static_cast<uint8_t>(n);
}

}  // namespace

#endif  // MIDIOUT_ENABLED

void midiKeyScanInit(void) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  memset(s_active, 0, sizeof(s_active));
#endif
}

void midiKeyScanPoll(uint16_t const* raw_adc_per_key, size_t key_count) {
#if MIDIOUT_ENABLED && !defined(PIO_UNIT_TESTING)
  if (!raw_adc_per_key) {
    return;
  }

  const uint8_t ch = channel1to16Clamped();
  const size_t n = key_count > kKeyCap ? kKeyCap : key_count;

  if (keyCalibrationIsActive()) {
    return;
  }

  for (size_t i = 0; i < n; i++) {
    const uint8_t strength = keyCalibrationMap(i, raw_adc_per_key[i]);
    const uint8_t note = noteForKey(i);
    const bool held = s_active[i];

    if (!held && strength >= static_cast<uint8_t>(MIDI_NOTE_ON_PERCENT)) {
      midiApiNoteOn(ch, note, velocityFromStrength(strength));
      s_active[i] = true;
    } else if (held && strength <= static_cast<uint8_t>(MIDI_NOTE_OFF_PERCENT)) {
      midiApiNoteOff(ch, note, 0);
      s_active[i] = false;
    }
  }

#else

  (void)raw_adc_per_key;
  (void)key_count;

#endif
}
