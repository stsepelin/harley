#pragma once

// Bench-only header-pin identification: slow 0V/3.3V square wave on
// CONFIG_VROD_PIN_WIGGLE_GPIO, DMM-probeable. Compiled out at -1.
void pin_wiggle_start(void);
