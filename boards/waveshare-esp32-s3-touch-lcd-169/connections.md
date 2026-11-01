# Waveshare ESP32-S3-Touch-LCD-1.69

Stock watch board. Bruce env: `waveshare-esp32-s3-touch-lcd-169`.

MCU is an ESP32-S3R8 (16MB QIO flash, 8MB OPI PSRAM). Partition table is the repo `custom_16Mb.csv`. There is no AXP2101: `pin_config.h` in the reference watch firmware defines `XPOWERS_CHIP_AXP2101`, but the sketch never starts it and the Waveshare schematic uses an ETA6098 charger instead.

## Onboard

| Function | Pin | Notes |
| --- | ---: | --- |
| LCD DC / CS / SCK / MOSI / RST / BL | 4 / 5 / 6 / 7 / 8 / 15 | ST7789, 240x280, no MISO |
| Touch SDA / SCL / RST / INT | 11 / 10 / 13 / 14 | CST816T at 0x15, interrupt on falling INT |
| RTC SDA / SCL | 11 / 10 | PCF85063A at 0x51 (shared bus) |
| IMU SDA / SCL / INT | 11 / 10 / 38 | QMI8658 at 0x6B. Not used by Bruce yet |
| Battery ADC | 1 | 200k/100k divider, VBAT = Vadc x 3 |
| PWR button (`SYS_OUT`) | 40 | Active low. Long press powers off |
| Battery latch (`SYS_EN`) | 41 | Driven high to hold battery power |
| Buzzer | 42 | Passive, `tone()` |
| BOOT | 0 | Select |
| USB D- / D+ | 19 / 20 | Native USB. Serial and BadUSB |
| UART0 TX / RX | 43 / 44 | Extension header |

TFT_eSPI's 240x280 ST7789 path inserts the 20-pixel CGRAM offset (`rowstart = 20` in portrait). That matches the stock `Arduino_ST7789(..., 240, 280, 0, 20, 0, 0)` window at rotation 0, and Waveshare's own example offset of `0, 20, 0, 20` for the other rotations. Color order is RGB and the panel is IPS (inversion on), same as that Arduino_GFX constructor (`ips = true`).

## Not on the board

No SD socket, microphone, I2S speaker, RGB LED, FM radio, CC1101, NRF24, PN532, or LoRa. Those Bruce features are compiled off or their pins are -1. RF, NRF24, LoRa, and Ethernet start hidden in the menu.

The extension header is 5V, 3V3, GND, I2C (GPIO10/11, shared with touch and RTC), UART (GPIO43/44), and GPIO2, GPIO3, GPIO17, GPIO18. That is not enough free pins for a CC1101 (SCK, MOSI, MISO, CS, GDO0) unless UART is given up. Do not use GPIO26-37; they are the flash and octal PSRAM bus.

I2C addresses already taken: `0x15`, `0x6B`, `0x51`.

## Older PCB

Boards with no model string printed on them use a different power/buzzer map (Waveshare FAQ):

| Signal | Current | Legacy |
| --- | ---: | ---: |
| SYS_EN | GPIO41 | GPIO35 |
| SYS_OUT | GPIO40 | GPIO36 |
| Buzzer | GPIO42 | GPIO33 |
| RTC INT | GPIO39 | GPIO41 |

Build the legacy power circuit with `-DWS169_LEGACY_PWR=1`. The legacy buzzer pin is an octal-PSRAM data line, so it stays disabled in that build.

## Flash

Hold BOOT, tap RST, then release BOOT if the port does not show up as a download device. USB CDC is on, so the serial monitor is the same Type-C port at 115200.

```sh
pio run -e waveshare-esp32-s3-touch-lcd-169 -t upload
```

Upload speed in the board definition is 921600. This image was compiled here; it was not flashed to hardware.
