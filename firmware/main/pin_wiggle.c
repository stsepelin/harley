#include "pin_wiggle.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Bench tool: toggles CONFIG_VROD_PIN_WIGGLE_GPIO between 0V and 3.3V
// every 2.5 s — slow enough for a DMM to settle on each level — so a
// header hole can be matched to a GPIO number with certainty instead
// of by counting silkscreen positions. See docs/PINS.md.

static const char *TAG = "wiggle";

static void wiggle_task(void *arg)
{
    (void)arg;
    const gpio_num_t    pin = (gpio_num_t)CONFIG_VROD_PIN_WIGGLE_GPIO;
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    bool level = false;
    for (;;) {
        level = !level;
        gpio_set_level(pin, level);
        ESP_LOGI(TAG, "GPIO%d -> %s", CONFIG_VROD_PIN_WIGGLE_GPIO, level ? "3.3V" : "0V");
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

void pin_wiggle_start(void)
{
    xTaskCreate(wiggle_task, "wiggle", 2048, NULL, 2, NULL);
}
