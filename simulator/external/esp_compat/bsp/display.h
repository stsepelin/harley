#pragma once
// Desktop stand-in for the Waveshare BSP's display.h. We only need the
// brightness setter for screen_settings.c; everything else the firmware
// uses (lock, lvgl bridge, etc.) is replaced by SDL/LVGL natively in the
// simulator's main loop, so it never gets included here.

int bsp_display_brightness_set(int brightness_percent);
