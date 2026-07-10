#pragma once

// Self-contained on-device map demo (CONFIG_VROD_MAP_DEMO). Loads the embedded
// corridor tileset, builds the map screen, and animates the baked Tallinn route.
// Idempotent + lazy: called by ui_manager the first time the map layout is
// shown, so a classic setting never pays for the tileset or the anim task.
void map_demo_load(void);
