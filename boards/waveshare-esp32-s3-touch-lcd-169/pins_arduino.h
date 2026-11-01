#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// Official Arduino core variant for this module uses the same VID/PID.
#define USB_VID 0x303a
#define USB_PID 0x821e

#define USB_MANUFACTURER "Waveshare"
#define USB_PRODUCT "ESP32-S3-Touch-LCD-1.69"

// UART0 is broken out on the extension header. With USB CDC enabled, Serial
// is the native USB port and Serial0 remains on these pins.
static const uint8_t TX = 43;
static const uint8_t RX = 44;

// Shared I2C: CST816T (0x15), QMI8658 (0x6B), PCF85063 (0x51), and the header.
static const uint8_t SDA = 11;
static const uint8_t SCL = 10;

// Default SPI object must not claim the LCD bus (GPIO6/7) or the octal-PSRAM
// pins. GPIO26-37 are the flash and octal-PSRAM bus on this ESP32-S3R8.
// Older Waveshare boards route SYS_EN/SYS_OUT to GPIO35/GPIO36; those are
// PSRAM lines, so this target does not support that revision.
static const uint8_t SS = 17;
static const uint8_t MOSI = 18;
static const uint8_t MISO = 2;
static const uint8_t SCK = 3;

#endif /* Pins_Arduino_h */
