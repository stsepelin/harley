# Phase 2: Display & Gauge UI Development
## ESP32-P4-WIFI6-Touch-LCD-3.4C — Building the Speedometer UI

> **Status: ✅ COMPLETE**
>
> Phase 2 shipped. Gauge UI runs against a synthetic driving cycle; every
> stage from "create the project" through "polish" is in. See *Outcome*
> at the bottom of this file for the delta between the original plan and
> what actually got built — mostly the plan got *more* than it called for
> (richer widget set, full host-test harness, desktop SDL2 simulator,
> shift-light, embedded GIF boot animation). This document is
> retained as the historical Phase 2 record; for current architecture see
> the repo's `CLAUDE.md` and `docs/PROJECT-BRIEF.md`.

**Editor**: Zed
**Goal**: Build a working 800×800 round speedometer gauge with simulated data. By the end of this phase you'll have a beautiful gauge UI running on your desk, ready to receive real J1850 data in Phase 3.

---

## Stage 1 — Development Environment Setup (1 hour)

### Install ESP-IDF on macOS

```bash
# Create the IDF directory
mkdir -p ~/esp
cd ~/esp

# Clone ESP-IDF v5.5 (Waveshare requires ≥v5.3.1, newer is fine)
git clone --recursive --branch v5.5 https://github.com/espressif/esp-idf.git

# Install the toolchain for ESP32-P4
cd ~/esp/esp-idf
./install.sh esp32p4
```

### Set up shell shortcut

Add to your `~/.zshrc`:

```bash
# ESP-IDF activation helper
alias get_idf='. ~/esp/esp-idf/export.sh'

# Quick project activation
alias idfgo='. ~/esp/esp-idf/export.sh && cd ~/esp/vrod_gauge'
```

Reload: `source ~/.zshrc`

Now in any new terminal, `get_idf` activates the toolchain.

### Verify installation

```bash
get_idf
idf.py --version
# Should print: ESP-IDF v5.5
```

---

## Stage 2 — Zed Editor Configuration (30 min)

Zed doesn't have an ESP-IDF extension, but it doesn't need one. ESP-IDF works entirely through `idf.py` CLI commands. The trick is making Zed's clangd language server understand ESP-IDF headers.

### Code intelligence (clangd auto-config)

ESP-IDF generates `compile_commands.json` during build — clangd uses this for perfect autocomplete and error highlighting. After your first build, symlink it:

```bash
cd ~/esp/vrod_gauge
ln -s build/compile_commands.json compile_commands.json
```

Zed picks it up automatically. Code completion, jump-to-definition, error highlighting — everything works.

### Optional `.clangd` config (for pre-build intelligence)

Create `.clangd` in your project root:

```yaml
CompileFlags:
  CompilationDatabase: build/
  Add:
    - -DESP_PLATFORM
    - -D__riscv
```

### Zed tasks

Create `.zed/tasks.json` in your project root for one-key build/flash:

```json
[
  {
    "label": "ESP-IDF Build",
    "command": "idf.py build",
    "use_new_terminal": false,
    "reveal": "always"
  },
  {
    "label": "ESP-IDF Flash + Monitor",
    "command": "idf.py -p /dev/cu.usbserial-1430 flash monitor",
    "use_new_terminal": true,
    "reveal": "always"
  },
  {
    "label": "ESP-IDF Monitor only",
    "command": "idf.py -p /dev/cu.usbserial-1430 monitor",
    "use_new_terminal": true
  },
  {
    "label": "ESP-IDF menuconfig",
    "command": "idf.py menuconfig",
    "use_new_terminal": true
  },
  {
    "label": "ESP-IDF Clean",
    "command": "idf.py fullclean",
    "use_new_terminal": false
  }
]
```

**Replace `/dev/cu.usbserial-1430`** with your actual port. Find it with:
```bash
ls /dev/cu.usb*
```

Run tasks via `Cmd+Shift+P` → "task: spawn" → pick from list.

### Daily Zed workflow

```bash
idfgo            # Activate IDF + cd into project
zed .            # Open in Zed
```

Then inside Zed: `Ctrl+`` opens terminal, `Cmd+Shift+P` → spawn task for build/flash.

---

## Stage 3 — Create the Project (1-2 hours)

### Get Waveshare's BSP base

Waveshare provides the board support package (BSP) and demo projects. We'll fork their LVGL demo as a starting point — it has the display, touch, and PSRAM all pre-configured correctly.

```bash
cd ~/esp
git clone https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC.git
cd ESP32-P4-WIFI6-Touch-LCD-XC
ls examples/
```

Look for an LVGL demo (path may vary — Waveshare reorganizes occasionally). Common names: `08_lvgl_demo`, `lvgl_v9_porting`, or `09_LVGL_HMI`.

### Copy to your project folder

```bash
cd ~/esp
mkdir vrod_gauge
cd vrod_gauge

# Copy the demo as your starting base
cp -r ~/esp/ESP32-P4-WIFI6-Touch-LCD-XC/examples/08_lvgl_demo/* ./

# Optional: rename project in CMakeLists.txt
sed -i '' 's/project(.*)/project(vrod_gauge)/' CMakeLists.txt
```

### Verify the base builds

```bash
get_idf
idf.py set-target esp32p4
idf.py build
```

If this builds cleanly, you have a working LVGL base for the P4. **Flash it now to confirm**:

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

You should see the demo LVGL screen on your display. **Now we replace its UI with the gauge.**

Exit monitor with `Ctrl+]`.

### Target project structure

```
vrod_gauge/
├── CMakeLists.txt
├── sdkconfig.defaults
├── compile_commands.json -> build/compile_commands.json
├── .clangd
├── .zed/
│   └── tasks.json
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml          # Component dependencies (LVGL, BSP)
│   ├── main.c                     # Entry point
│   ├── vehicle/
│   │   ├── vehicle_data.h         # Shared state (mutex-protected)
│   │   └── vehicle_data.c
│   ├── display/
│   │   ├── ui_manager.h
│   │   ├── ui_manager.c
│   │   ├── screen_ride.h          # Main gauge screen
│   │   ├── screen_ride.c
│   │   └── widgets/
│   │       ├── speedo_arc.h
│   │       ├── speedo_arc.c
│   │       ├── rpm_bar.h
│   │       ├── rpm_bar.c
│   │       ├── gear_indicator.h
│   │       └── gear_indicator.c
│   └── simulator/
│       ├── sim_engine.h           # Phase 2 only — fake data
│       └── sim_engine.c
└── components/                     # Future: J1850, BLE
```

### Update main/CMakeLists.txt

```cmake
idf_component_register(
    SRCS
        "main.c"
        "vehicle/vehicle_data.c"
        "display/ui_manager.c"
        "display/screen_ride.c"
        "display/widgets/speedo_arc.c"
        "display/widgets/rpm_bar.c"
        "display/widgets/gear_indicator.c"
        "simulator/sim_engine.c"
    INCLUDE_DIRS
        "."
        "vehicle"
        "display"
        "display/widgets"
        "simulator"
    REQUIRES
        esp_lcd
        esp_lvgl_port
)
```

### main/idf_component.yml

```yaml
dependencies:
  idf:
    version: ">=5.3.1"
  lvgl/lvgl: "~9.2.0"
  espressif/esp_lvgl_port: "*"
  waveshare/esp32_p4_wifi6_touch_lcd_xc: "*"
```

---

## Stage 4 — Code the Project (3-4 hours)

### main.c — Entry point

```c
// main/main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "vehicle_data.h"
#include "ui_manager.h"
#include "sim_engine.h"

static const char *TAG = "vrod_gauge";

void app_main(void)
{
    ESP_LOGI(TAG, "VRSCF Gauge starting...");

    // Initialize the Waveshare BSP — display, touch, audio
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES / 10,
        .double_buffer = 1,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,    // Use PSRAM for the large frame buffer
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display ready: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    // Initialize shared state
    vehicle_data_init();

    // Build the UI (must hold LVGL lock)
    bsp_display_lock(0);
    ui_manager_init();
    bsp_display_unlock();

    // Start the simulator (Phase 2 only - fake data)
    sim_engine_start();

    ESP_LOGI(TAG, "Boot complete - simulator running");

    // Heartbeat
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Speed=%d km/h  RPM=%d  Gear=%d",
                 vehicle_data_get_speed(),
                 vehicle_data_get_rpm(),
                 vehicle_data_get_gear());
    }
}
```

### vehicle_data.h — Shared, thread-safe state

```c
// main/vehicle/vehicle_data.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    GEAR_NEUTRAL = 0,
    GEAR_1, GEAR_2, GEAR_3, GEAR_4, GEAR_5, GEAR_6,
    GEAR_UNKNOWN
} gear_t;

typedef struct {
    // From J1850 bus (Phase 3)
    uint16_t speed_kmh;
    uint16_t rpm;
    gear_t   gear;
    int8_t   engine_temp_c;
    uint8_t  fuel_level;       // 0-6 (matches J1850 encoding)

    // Indicator lights
    bool turn_left;
    bool turn_right;
    bool high_beam;
    bool neutral;
    bool oil_pressure_warning;
    bool check_engine;

    // Calculated
    uint32_t odometer_m;
    uint32_t trip_m;
} vehicle_data_t;

void vehicle_data_init(void);

// Thread-safe setter/getter (whole struct)
void vehicle_data_set(const vehicle_data_t *new_data);
void vehicle_data_get(vehicle_data_t *out_data);

// Quick accessors
uint16_t vehicle_data_get_speed(void);
uint16_t vehicle_data_get_rpm(void);
gear_t   vehicle_data_get_gear(void);
```

### vehicle_data.c

```c
// main/vehicle/vehicle_data.c
#include "vehicle_data.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static vehicle_data_t s_data;
static SemaphoreHandle_t s_mutex;

void vehicle_data_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_data.gear = GEAR_NEUTRAL;
    s_mutex = xSemaphoreCreateMutex();
}

void vehicle_data_set(const vehicle_data_t *new_data)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&s_data, new_data, sizeof(s_data));
        xSemaphoreGive(s_mutex);
    }
}

void vehicle_data_get(vehicle_data_t *out_data)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(out_data, &s_data, sizeof(s_data));
        xSemaphoreGive(s_mutex);
    }
}

uint16_t vehicle_data_get_speed(void) { return s_data.speed_kmh; }
uint16_t vehicle_data_get_rpm(void)   { return s_data.rpm; }
gear_t   vehicle_data_get_gear(void)  { return s_data.gear; }
```

### simulator/sim_engine.c — Fake data for Phase 2

```c
// main/simulator/sim_engine.c
#include "sim_engine.h"
#include "vehicle_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "sim";

static void sim_task(void *arg)
{
    float t = 0.0f;
    vehicle_data_t data = {0};
    data.engine_temp_c = 92;
    data.fuel_level = 4;
    data.odometer_m = 12847000;

    ESP_LOGI(TAG, "Simulator running");

    while (1) {
        t += 0.05f;

        // Sinusoidal speed profile: 30 to 130 km/h
        float speed = 80.0f + 50.0f * sinf(t * 0.3f);
        float rpm = 2000.0f + speed * 30.0f;

        data.speed_kmh = (uint16_t)speed;
        data.rpm = (uint16_t)rpm;

        // Gear follows speed (rough approximation)
        if (speed < 20)       data.gear = GEAR_1;
        else if (speed < 40)  data.gear = GEAR_2;
        else if (speed < 60)  data.gear = GEAR_3;
        else if (speed < 90)  data.gear = GEAR_4;
        else if (speed < 120) data.gear = GEAR_5;
        else                  data.gear = GEAR_6;

        // Turn signals blinking
        data.turn_left = ((int)(t * 2) % 6) < 2;

        vehicle_data_set(&data);

        vTaskDelay(pdMS_TO_TICKS(50));   // 20 Hz update
    }
}

void sim_engine_start(void)
{
    xTaskCreatePinnedToCore(sim_task, "sim", 4096, NULL, 5, NULL, 0);
}
```

```c
// main/simulator/sim_engine.h
#pragma once
void sim_engine_start(void);
```

### display/ui_manager.c

```c
// main/display/ui_manager.c
#include "ui_manager.h"
#include "screen_ride.h"
#include "vehicle_data.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static lv_obj_t *s_screen_ride;

static void update_task(void *arg)
{
    vehicle_data_t data;
    while (1) {
        vehicle_data_get(&data);

        bsp_display_lock(0);
        screen_ride_update(&data);
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(33));   // ~30 FPS
    }
}

void ui_manager_init(void)
{
    s_screen_ride = screen_ride_create();
    lv_scr_load(s_screen_ride);

    xTaskCreatePinnedToCore(update_task, "ui_upd", 8192, NULL, 4, NULL, 1);
}
```

```c
// main/display/ui_manager.h
#pragma once
void ui_manager_init(void);
```

### display/screen_ride.c — Main gauge screen

```c
// main/display/screen_ride.c
#include "screen_ride.h"
#include "speedo_arc.h"
#include "rpm_bar.h"
#include "gear_indicator.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *s_screen;
static lv_obj_t *s_speedo;
static lv_obj_t *s_rpm_bar;
static lv_obj_t *s_gear;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_fuel_label;
static lv_obj_t *s_odo_label;

lv_obj_t *screen_ride_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // RPM bar across top arc
    s_rpm_bar = rpm_bar_create(s_screen);
    lv_obj_align(s_rpm_bar, LV_ALIGN_TOP_MID, 0, 60);

    // Speedometer arc — main element, centered
    s_speedo = speedo_arc_create(s_screen);
    lv_obj_align(s_speedo, LV_ALIGN_CENTER, 0, 0);

    // Gear indicator — right of speed
    s_gear = gear_indicator_create(s_screen);
    lv_obj_align(s_gear, LV_ALIGN_CENTER, 150, 0);

    // Bottom info row — temp, fuel, odo
    s_temp_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_temp_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_temp_label, LV_ALIGN_CENTER, -180, 200);
    lv_label_set_text(s_temp_label, "92°C");

    s_fuel_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_fuel_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_fuel_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_fuel_label, LV_ALIGN_CENTER, 0, 200);
    lv_label_set_text(s_fuel_label, "FUEL ████░░");

    s_odo_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_odo_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_odo_label, &lv_font_montserrat_22, 0);
    lv_obj_align(s_odo_label, LV_ALIGN_CENTER, 180, 200);
    lv_label_set_text(s_odo_label, "12847 km");

    return s_screen;
}

void screen_ride_update(const vehicle_data_t *data)
{
    speedo_arc_set_value(s_speedo, data->speed_kmh);
    rpm_bar_set_value(s_rpm_bar, data->rpm);
    gear_indicator_set(s_gear, data->gear);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d°C", data->engine_temp_c);
    lv_label_set_text(s_temp_label, buf);

    // Fuel: 0-6 → bar representation
    const char *fuel_bars[] = {
        "FUEL ░░░░░░", "FUEL █░░░░░", "FUEL ██░░░░",
        "FUEL ███░░░", "FUEL ████░░", "FUEL █████░",
        "FUEL ██████"
    };
    if (data->fuel_level <= 6) {
        lv_label_set_text(s_fuel_label, fuel_bars[data->fuel_level]);
    }

    snprintf(buf, sizeof(buf), "%lu km", data->odometer_m / 1000);
    lv_label_set_text(s_odo_label, buf);
}
```

```c
// main/display/screen_ride.h
#pragma once
#include "lvgl.h"
#include "vehicle_data.h"

lv_obj_t *screen_ride_create(void);
void screen_ride_update(const vehicle_data_t *data);
```

### display/widgets/speedo_arc.c — The main gauge

```c
// main/display/widgets/speedo_arc.c
#include "speedo_arc.h"
#include "lvgl.h"
#include <stdio.h>

typedef struct {
    lv_obj_t *arc_fg;
    lv_obj_t *value_label;
} speedo_data_t;

lv_obj_t *speedo_arc_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 400, 400);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Background track arc
    lv_obj_t *arc_bg = lv_arc_create(cont);
    lv_obj_set_size(arc_bg, 380, 380);
    lv_arc_set_range(arc_bg, 0, 300);
    lv_arc_set_bg_angles(arc_bg, 135, 405);
    lv_arc_set_value(arc_bg, 300);
    lv_obj_remove_style(arc_bg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_bg, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_bg, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_bg, lv_color_hex(0x222222), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_bg, 10, LV_PART_INDICATOR);
    lv_obj_center(arc_bg);

    // Foreground value arc (orange)
    lv_obj_t *arc_fg = lv_arc_create(cont);
    lv_obj_set_size(arc_fg, 380, 380);
    lv_arc_set_range(arc_fg, 0, 300);
    lv_arc_set_bg_angles(arc_fg, 135, 405);
    lv_arc_set_value(arc_fg, 0);
    lv_obj_remove_style(arc_fg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_fg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(arc_fg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_fg, lv_color_hex(0xFF6600), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_fg, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_fg, true, LV_PART_INDICATOR);
    lv_obj_center(arc_fg);

    // Big speed value
    lv_obj_t *value = lv_label_create(cont);
    lv_obj_set_style_text_color(value, lv_color_white(), 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_48, 0);
    lv_label_set_text(value, "0");
    lv_obj_align(value, LV_ALIGN_CENTER, 0, -10);

    // Unit
    lv_obj_t *unit = lv_label_create(cont);
    lv_obj_set_style_text_color(unit, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_18, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 30);

    speedo_data_t *sd = lv_malloc(sizeof(speedo_data_t));
    sd->arc_fg = arc_fg;
    sd->value_label = value;
    lv_obj_set_user_data(cont, sd);

    return cont;
}

void speedo_arc_set_value(lv_obj_t *cont, uint16_t kmh)
{
    speedo_data_t *sd = lv_obj_get_user_data(cont);
    if (!sd) return;

    // Smooth animation to new value
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sd->arc_fg);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_arc_set_value);
    lv_anim_set_values(&a, lv_arc_get_value(sd->arc_fg), kmh);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", kmh);
    lv_label_set_text(sd->value_label, buf);
}
```

```c
// main/display/widgets/speedo_arc.h
#pragma once
#include "lvgl.h"
#include <stdint.h>

lv_obj_t *speedo_arc_create(lv_obj_t *parent);
void speedo_arc_set_value(lv_obj_t *cont, uint16_t kmh);
```

### display/widgets/rpm_bar.c

```c
// main/display/widgets/rpm_bar.c
#include "rpm_bar.h"
#include "lvgl.h"

lv_obj_t *rpm_bar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 500, 16);
    lv_bar_set_range(bar, 0, 10000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(bar, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF6600), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

    return bar;
}

void rpm_bar_set_value(lv_obj_t *bar, uint16_t rpm)
{
    // Animate value
    lv_bar_set_value(bar, rpm, LV_ANIM_ON);

    // Color shift at redline (>8000 RPM)
    if (rpm > 8000) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF6600), LV_PART_INDICATOR);
    }
}
```

```c
// main/display/widgets/rpm_bar.h
#pragma once
#include "lvgl.h"
#include <stdint.h>

lv_obj_t *rpm_bar_create(lv_obj_t *parent);
void rpm_bar_set_value(lv_obj_t *bar, uint16_t rpm);
```

### display/widgets/gear_indicator.c

```c
// main/display/widgets/gear_indicator.c
#include "gear_indicator.h"
#include "lvgl.h"

lv_obj_t *gear_indicator_create(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 80, 80);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_radius(cont, 8, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0xFF6600), 0);
    lv_obj_set_style_border_width(cont, 2, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(cont);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_label_set_text(label, "N");
    lv_obj_center(label);

    lv_obj_set_user_data(cont, label);
    return cont;
}

void gear_indicator_set(lv_obj_t *cont, gear_t gear)
{
    lv_obj_t *label = lv_obj_get_user_data(cont);
    if (!label) return;

    const char *text;
    switch (gear) {
        case GEAR_NEUTRAL: text = "N"; break;
        case GEAR_1: text = "1"; break;
        case GEAR_2: text = "2"; break;
        case GEAR_3: text = "3"; break;
        case GEAR_4: text = "4"; break;
        case GEAR_5: text = "5"; break;
        case GEAR_6: text = "6"; break;
        default:     text = "-"; break;
    }
    lv_label_set_text(label, text);
}
```

```c
// main/display/widgets/gear_indicator.h
#pragma once
#include "lvgl.h"
#include "vehicle_data.h"

lv_obj_t *gear_indicator_create(lv_obj_t *parent);
void gear_indicator_set(lv_obj_t *cont, gear_t gear);
```

---

## Stage 5 — Build, Flash, Test (30 min)

```bash
idfgo                    # Activate IDF + cd to project
idf.py set-target esp32p4
idf.py build             # First build takes 5-10 min
```

If build succeeds:

```bash
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

**Expected output**:
- Display: black background
- Orange speedometer arc smoothly sweeping
- Big white digits showing current km/h
- "km/h" label below
- RPM bar pulsing across the top
- Gear box on the right cycling through 1-6
- "92°C" left, "FUEL ████░░" center, "12847 km" right at the bottom
- Serial monitor: heartbeat logs every 5 seconds

Exit monitor with `Ctrl+]`.

### Symlink compile_commands.json for Zed

```bash
ln -s build/compile_commands.json compile_commands.json
```

Restart Zed — clangd should now provide full autocomplete and error checking.

---

## Stage 6 — Iterate and Polish

### Performance tuning

If FPS feels low or animations stutter:

```bash
idf.py menuconfig
```

Navigate to:
- **Component config → LVGL configuration → Memory** → set buffer to PSRAM
- **Component config → LVGL configuration → Tick interval** → 5 ms
- **Component config → ESP-BSP → Display config** → enable double buffering

### Visual polish ideas

| Feature | Implementation hint |
|---|---|
| Custom large font for speed | LVGL Font Converter at lvgl.io/tools/fontconverter — generate Montserrat 96px |
| Tach redline | Already implemented in `rpm_bar.c` (color change at 8000+ RPM) |
| Shift light flash | Add `lv_obj_set_style_bg_color` toggle at >9000 RPM with `lv_timer` |
| Animated startup | Use `lv_anim` on screen load — sweep speedo from 0 to max and back |
| Turn signal arrows | Add `lv_label` with `LV_SYMBOL_LEFT` / `LV_SYMBOL_RIGHT` |
| Round-screen mask | All elements within 360px radius from center to avoid corner clipping |

### Color theme — match V-Rod stock cluster

```c
// V-Rod color palette
#define VROD_ORANGE     0xFF6600    // Main accent
#define VROD_RED        0xFF0000    // Warnings, redline
#define VROD_BLACK      0x000000    // Background
#define VROD_DARK_GRAY  0x1A1A1A    // Panels
#define VROD_LIGHT_GRAY 0x888888    // Secondary text
#define VROD_WHITE      0xFFFFFF    // Primary text
```

---

## Stage 7 — Hand-off to Phase 3

When the gauge UI looks good with simulated data, Phase 3 begins:

1. **Wire the J1850 bidirectional circuit** (IRLZ44N + 2N2222 + zener + resistors) to two P4 GPIOs on the 40-pin header
2. **Create `components/j1850/`** — VPW timing decoder, message parser
3. **Replace `sim_engine`** with the J1850 driver that writes the same `vehicle_data` struct
4. **UI keeps working unchanged** — that's the value of the data abstraction layer

The simulator stays in the codebase as a fallback — useful for UI iteration without the bike connected.

---

## Resources

| Resource | URL |
|---|---|
| Waveshare board docs | https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-XC |
| Waveshare GitHub | https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC |
| ESP-IDF P4 guide | https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/ |
| LVGL docs | https://docs.lvgl.io/master/ |
| LVGL Font Converter | https://lvgl.io/tools/fontconverter |
| Zed editor docs | https://zed.dev/docs |
| ESP Components Registry | https://components.espressif.com/components?q=namespace:waveshare |

---

## Time Estimate

| Stage | Time | Cumulative |
|---|---|---|
| 1. ESP-IDF installation | 1 hr | 1 hr |
| 2. Zed setup | 30 min | 1.5 hr |
| 3. Project creation | 1-2 hr | 3.5 hr |
| 4. Code the gauge UI | 3-4 hr | 7.5 hr |
| 5. Build + flash + test | 30 min | 8 hr |
| 6. Polish | optional | ongoing |

**Total: ~8 hours over 2 weekends to have a beautiful working gauge.**

When you see your first orange needle sweep across the round display, you'll know it's all worth it. 🏍️⚡

---

## Outcome — what actually shipped vs the plan

Phase 2 delivered everything in the plan plus a few things that weren't.
Brief delta — for the full file map see `docs/PROJECT-BRIEF.md`.

### As planned, shipped

- **Project scaffold** based on the Waveshare `08_lvgl_demo_v9` example,
  with the BSP patched up to ESP-IDF v6.0.1.
- **Thread-safe `vehicle_data_t`** as the single source of truth between
  the producer (sim now, J1850 driver in Phase 3) and the UI thread.
- **Dual-core split**: sim task on core 0 at prio 8, LVGL pthreads on
  core 1 (via `CONFIG_PTHREAD_DEFAULT_CORE_1=y`).
- **Synthetic 32-second driving cycle** exercising every phase:
  idle/hazard, accel through gears, WOT redline pull, cruise with
  high-beam + signal, deceleration.
- **Widgets**: speedometer, tach, gear, fuel, turn signals, plus the
  ones in the polish list (custom large font, tach redline, turn
  arrows, round-screen mask, V-Rod colour theme).
- **Performance tuning**: PSRAM framebuffers (triple-partial), RGB565,
  baked-sprite render path. (PPA hardware accel was tried and later
  dropped — it caused banding on this BSP; see `ARCHITECTURE.md`.)
- **Shift light** at >9000 RPM. (Shipped first as a red bezel ring
  flashing ~5 Hz; that and four other approaches were abandoned for
  performance — the final form blinks the gear digit. The full
  iteration story is in `ARCHITECTURE.md`.)
- **Round-screen mask**: all elements live within the inscribed circle.

### Beyond the plan

- **Tach** is more elaborate than "needle pointer": pre-baked Gaussian
  glow ring as the arc brush, orange→red split at REDLINE_RPM, a baked
  cursor sprite with rounded pill ends, zoom-on-cursor labels.
- **Engine "breathing"** in the sim — slow swell + fast chatter + LCG
  noise — so the needle is never dead-still.
- **Per-widget skip-if-unchanged caches** on every setter; UI work
  collapses to near-zero when readings hold steady.
- **Full warning-lamp set** (oil, engine, ABS, battery, immobiliser,
  low + high beam) in two chevron groups, with the beam slot rotating
  low↔high every 5 s. Low-fuel and high-temp also trip icons inside
  the existing widgets.
- **Rotating info slot** above the fuel bar: clock → odometer → trip A
  → trip B, cycled every 5 s. Updates only the visible widget.
- **Boot animation**: an embedded GIF (600×600 native — 800×800 was
  tried but wasn't smooth once PPA was dropped), software-blitted and
  handed off to the ride screen on `LV_EVENT_READY`. (Lottie/ThorVG
  was tried first — vector rasterisation at that size isn't real-time
  on the P4.)
- **Host unit-test harness** under `test_apps/host/` — Unity + FreeRTOS
  stub + LVGL stub, 6 suites covering pure-logic modules (`gear_table`,
  `sim_math`, `format`, `smooth`, `vehicle_data`) at 100 % line + branch
  coverage, plus a widget cache-regression suite.
- **Desktop SDL2 simulator** at `simulator/` — the same widget code
  running in an 800×800 SDL window driven by the sim engine on a real
  pthread. Iterate on layout/colours/animation timing without flashing.
- **CI**: `host-tests.yml` (coverage gate on every push), `firmware-build.yml`
  (espressif/idf container builds artifacts).

### Hand-off to Phase 3

The data-abstraction layer made the migration shape trivial:
`main/simulator/sim_engine.c` becomes `components/j1850/j1850_driver.c`,
writing the same `vehicle_data_t` struct. The UI, tests, and simulator
stay unchanged.
