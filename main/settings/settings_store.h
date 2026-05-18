#pragma once
#include <stdbool.h>
#include "settings.h"

// One-shot NVS init and load. Call once at boot before any other
// settings_store_* function. Erases NVS if the on-flash format is
// unreadable (page exhaustion, version bump), then loads the current
// snapshot from NVS (or defaults if first boot).
void settings_store_init(void);

// Current in-memory snapshot. Always validated. The pointer is stable
// for the lifetime of the program; the pointed-to struct is updated
// in place by settings_store_apply.
const settings_t *settings_store_current(void);

// Validate, update the in-memory snapshot, persist to NVS. Returns
// false if the NVS write failed (the in-memory snapshot is still
// updated, so the UI keeps responding even when flash is unhappy).
bool settings_store_apply(const settings_t *s);
