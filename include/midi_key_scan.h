#pragma once

#include <stddef.h>
#include <stdint.h>

void midiKeyScanInit(void);

// Emit note on/off transitions from calibrated per-key hall strength vs config thresholds.
void midiKeyScanPoll(uint16_t const* raw_adc_per_key, size_t key_count);
