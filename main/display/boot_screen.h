#pragma once

// Draws the boot splash on the active LVGL screen. Caller must hold the LVGL
// lock. Replaced by the gauge screen once the rest of the UI is ready.
void boot_screen_show(void);
