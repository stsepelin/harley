#pragma once

// NEO-6M / M8N producer: reads NMEA off a UART, parses RMC sentences,
// publishes fixes into gps_source. Compiled in when CONFIG_VROD_GPS_UART
// is set; pin/UART/baud come from the same Kconfig menu.
void gps_uart_start(void);
