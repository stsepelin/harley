#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    GEAR_NEUTRAL = 0,
    GEAR_1, GEAR_2, GEAR_3, GEAR_4, GEAR_5, GEAR_6,
    GEAR_UNKNOWN
} gear_t;

typedef struct {
    // Canonical speed unit is mph: this is a US-market bike whose real source
    // (the J1850 ECM) is mph-native, so mph is stored raw and the display
    // converts to km/h only when the user selects metric. Distance stays in
    // metres (odometer_m/trip_m) - metres is already unit-neutral.
    uint16_t speed_mph;
    uint16_t rpm;
    gear_t   gear;
    int8_t   engine_temp_c;
    uint8_t  fuel_level;          // 0..6, matches J1850 encoding
    bool     turn_left;
    bool     turn_right;
    bool     low_beam;
    bool     high_beam;
    bool     neutral;
    bool     oil_pressure_warning;
    bool     check_engine;
    bool     abs_warning;
    bool     battery_warning;
    bool     immobiliser_warning;
    uint32_t odometer_m;
    uint32_t trip1_m;
    uint32_t trip2_m;
    // Cumulative fuel-consumption ticks off the bus (A8 83 10). Raw units until
    // a fill-up calibrates mL/tick (see firmware/docs/ride-1-findings.md);
    // economy = fuel_ticks / distance is computed downstream, not here.
    uint32_t fuel_ticks;
    // Mock time-of-day driven by the sim until we have an RTC/SNTP source.
    uint8_t  clock_hours;
    uint8_t  clock_minutes;
} vehicle_data_t;

void vehicle_data_init(void);
void vehicle_data_set(const vehicle_data_t *new_data);
void vehicle_data_get(vehicle_data_t *out_data);
