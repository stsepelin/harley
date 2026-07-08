#include "j1850_driver.h"
#include "gear_calc.h"
#include "j1850_parse.h"
#include "vehicle_data.h"

// Running aggregate: each J1850 broadcast updates one field, so we hold
// the last-known full picture and re-publish it on every recognised
// frame. RPM alone arrives ~15/s, well within what vehicle_data + the UI
// absorb, so no extra rate-limiting is needed here.
static vehicle_data_t s_vd;

void j1850_driver_init(void)
{
    vehicle_data_get(&s_vd);  // seed from whatever the boot state left
}

void j1850_driver_feed(const j1850_frame_t *f)
{
    if (j1850_parse(f, &s_vd)) {
        // The bus carries no gear position (no sensor on this bike), so derive
        // it from the latest RPM:speed ratio. Recomputed on every republish;
        // RPM/speed frames keep both inputs fresh.
        s_vd.gear = gear_calc(s_vd.rpm, s_vd.speed_mph, s_vd.gear);
        vehicle_data_set(&s_vd);
    }
}
