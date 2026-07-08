# GPIO budget — ESP32-P4-WIFI6-Touch-LCD-3.4C

Source of truth for which P4 pins are claimed, free, or reserved.
Derived from the vendor schematic
(`waveshare-reference/hardware/schematics/ESP32-P4-WIFI6-Touch-LCD-XC-Schematic.pdf`,
sheet 1 + the layout silkscreen), the BSP headers, and `sdkconfig`.
Update this table whenever a pin gets claimed.

## Claimed by the board / firmware — do not touch

| GPIO | Used by | Notes |
|---|---|---|
| 0, 1 | 32.768 kHz RTC crystal | |
| 6 | ESP32-C6 IO2 strap line | |
| 7, 8 | I2C (GT911 touch + ES8311/ES7210 codecs) | SDA=7, SCL=8; also on the header + J6 |
| 9–13 | I2S audio (codec + mics) | |
| **14–19** | **SDIO link to the ESP32-C6** | **This is BLE/WiFi. Touching these kills the radio.** |
| 23 | GT911 touch reset | |
| 26 | LCD backlight PWM | |
| 27 | LCD reset | |
| 33 | LCD TE (display block) | |
| 35 | BOOT strap + auto-download circuit | Header-exposed; leave alone |
| 37, 38 | UART0 console via CH343P (TXD/RXD) | The header's "RXD/TXD" holes |
| 39–44 | microSD slot | 4-bit SDMMC (CLK 43, CMD 44, D0-3 39-42) on **slot 0**. Used by the ride log (`CONFIG_VROD_RIDE_LOG`). The C6 radio (esp_hosted SDIO) is on slot 1 of the same controller; the mount stubs `host.init` to share it (see the Phase 3 plan ride-log section). |
| 45 | microSD power switch | SD IO power is via the P4 on-chip LDO (channel 4), per the vendor 05_sdmmc example. |
| 53 | Audio PA enable | |
| 54 | ESP32-C6 reset (esp_hosted) | |

## 40-pin header (J8) silkscreen legend

As printed on the board (two rows of 20):

```
GND 47 52 48 32 51 24 GND 50  2  3 3V3 28 20 21 GND 29 SCL SDA 3V3
 30 46 31 GND 34 GND 25 49 36 35 GND  4  5 GND 22 RXD TXD GND 5V  5V
```

## Claimed by the cluster (Phase 3+)

| GPIO | Used by | Where set |
|---|---|---|
| 20 | J1850 RX (divider node) | `CONFIG_VROD_J1850_RX_GPIO` |
| **22** | **RESERVED: fuel-level sender ADC (Phase 6)** | — |

GPIO 21 was the GPS NMEA input; **GPS was dropped, so 21 is free again**
(and, being ADC-capable, is a spare analog pin if ever needed).

Why 22 is reserved: P4 ADC1 channels sit on GPIO 16–23 *only*. 16–19
are SDIO, 23 is touch reset — with 20 on J1850 RX and 21 now free, GPIO
22 is kept for the fuel sender, the one analog signal in the plan;
nothing digital gets to squat on 22.
(Note: the 2009 VRSC sender is **ultrasonic** — powered, but with an
ohmic output emulating a resistive sender, so ADC remains the right
interface; calibration/temp-comp/motion-gating must be reimplemented.
Details in the master plan's Phase 6 fuel-sender caveat.)

> **Resolved trap (July 2026):** linking `esp_adc` used to crash-loop
> this firmware before `app_main`. Root cause was ~2 KB of
> pre-scheduler internal-heap margin on rev<v3 silicon (the SRAM above
> 0x4ff3afc0 joins the heap late; esp_hosted's WiFi-sized SDIO queues
> ate the rest) — fixed by `CONFIG_ESP_HOSTED_SDIO_TX/RX_Q_SIZE=6` in
> `sdkconfig.defaults`. **Phase 6's fuel ADC is unblocked.** Full story
> + regression canary (`CONFIG_VROD_ADC_REPRO`) in the
> `ble-bringup-bisect.md` addendum; re-run the canary after
> IDF/toolchain bumps.

## Free on the header (Phase 6 discrete inputs, etc.)

2, 3, 4, 5, 21, 24, 25, 28, 29, 30, 31, 32, 34, 36, 46, 47, 48, 49, 50,
51, 52 (21 freed when GPS was dropped) — plenty for the six 12V discrete
dividers (turn L/R, high beam, neutral, oil, ignition) plus the VSS
pulse input.

## Physically confirming a pin

Enable the bench wiggle test, flash, and probe the header with a DMM:

```
idf.py menuconfig  ->  V-Rod cluster  ->  Bench pin-wiggle test GPIO
```

The chosen GPIO toggles 0V ↔ 3.3V every 2.5 s (slow enough for a DMM
to settle) and logs each transition to serial. The hole that swings is
your pin. Set it back to -1 afterwards — it's compiled out by default.
