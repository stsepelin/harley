#pragma once

// Shows the Lottie boot animation on the active LVGL screen, then hands off
// to the gauge UI by calling ui_manager_init() once the animation completes.
// Caller must hold the LVGL lock.
void boot_screen_show(void);
