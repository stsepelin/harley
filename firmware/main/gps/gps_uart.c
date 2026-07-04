#include "gps_uart.h"
#include "gps_source.h"
#include "nmea.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gps_uart";

#define GPS_UART_NUM ((uart_port_t)CONFIG_VROD_GPS_UART_NUM)
// NEO-6M and M8N both ship at 9600 baud; at ~6 sentences/s that's a
// trickle. 1024 of driver RX buffer absorbs a full second of output.
#define GPS_RX_BUF     1024
#define GPS_READ_CHUNK 64

static void gps_uart_task(void *arg)
{
    (void)arg;
    nmea_framer_t framer;
    nmea_framer_init(&framer);
    uint8_t chunk[GPS_READ_CHUNK];

    for (;;) {
        // 200 ms poll matches the 5 Hz M8N max fix rate; the NEO-6M's
        // 1 Hz stream just means most polls return a partial sentence.
        int n = uart_read_bytes(GPS_UART_NUM, chunk, sizeof(chunk), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; i++) {
            if (!nmea_framer_push(&framer, (char)chunk[i]))
                continue;
            nmea_rmc_t rmc;
            if (!nmea_parse_rmc(framer.buf, &rmc))
                continue;
            gps_source_t g = {
                .lat_e7      = rmc.lat_e7,
                .lon_e7      = rmc.lon_e7,
                .speed_mph   = rmc.speed_mph,
                .heading_deg = rmc.heading_deg,
                .fix_ok      = rmc.valid,
                .time_ms     = rmc.time_utc_ms,
            };
            gps_source_set(&g);
        }
    }
}

void gps_uart_start(void)
{
    const uart_config_t cfg = {
        .baud_rate  = CONFIG_VROD_GPS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, GPS_RX_BUF, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &cfg));
    // TX is optional wiring — only needed if we ever push UBX config
    // (fix rate, constellation) to the module. RX alone gives fixes.
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, CONFIG_VROD_GPS_TX_GPIO, CONFIG_VROD_GPS_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Core 0 with the other producers; prio between the sim (8) and the
    // event watcher (5) — a missed fix beats a missed touch sample.
    xTaskCreatePinnedToCore(gps_uart_task, "gps_uart", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "NMEA reader on UART%d (RX GPIO%d, %d baud)", CONFIG_VROD_GPS_UART_NUM,
             CONFIG_VROD_GPS_RX_GPIO, CONFIG_VROD_GPS_BAUD);
}
