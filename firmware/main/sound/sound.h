#pragma once
#include <stdbool.h>
#include <stdint.h>

// Initialise the I2S codec and spawn a small audio worker task. Safe to
// call once at boot; later calls are no-ops. If the codec hardware is
// missing or fails to initialise, all sound_play_* become no-ops so the
// caller never needs to error-check.
void sound_init(void);

// Master mute. When disabled, sound_play_* drop events without writing
// to the codec — saves the I2S DMA round-trip and stops the speaker
// hissing on the next click after a silence gap.
void sound_set_enabled(bool enabled);

// Codec output volume, 0..100. Applied immediately; subsequent calls
// override. Out-of-range values are clamped.
void sound_set_volume(uint8_t pct);

// Non-blocking — posts a "turn signal click" event to the audio task.
// Drops the event if the queue is full (real relay clicks are short and
// frequent; a brief overlap is fine to skip).
void sound_play_turn_click(void);
