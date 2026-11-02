#include "core/bus_HAL.h"
#include "core/powerSave.h"
#include "core/utils.h"
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <interface.h>

// CST816T on the shared I2C bus. Coordinates are the panel's native 240x280
// portrait frame, the same frame Arduino_GFX uses at rotation 0.
#define CST816_ADDR 0x15
#define CST816_REG_DATA 0x01
#define CST816_REG_CHIPID 0xA7

static bool touchReady = false;
static volatile bool cst816Irq = false;

static void IRAM_ATTR cst816Isr() { cst816Irq = true; }

static bool cst816Read(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)CST816_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static void cst816Reset() {
    pinMode(WS_TP_INT, INPUT_PULLUP);
    pinMode(WS_TP_RST, OUTPUT);
    digitalWrite(WS_TP_RST, LOW);
    delay(20);
    digitalWrite(WS_TP_RST, HIGH);
    delay(50);
}

static bool cst816Point(int16_t &x, int16_t &y) {
    uint8_t raw[6] = {0};
    if (!cst816Read(CST816_REG_DATA, raw, sizeof(raw))) return false;
    uint8_t points = raw[1] & 0x0F;
    if (points == 0) return false;
    x = ((raw[2] & 0x0F) << 8) | raw[3];
    y = ((raw[4] & 0x0F) << 8) | raw[5];
    if (x < 0 || y < 0 || x >= TFT_WIDTH || y >= TFT_HEIGHT) return false;
    return true;
}

// Map the panel's rotation-0 coordinates into the TFT_eSPI frame Bruce is using.
static void cst816ToScreen(int16_t rawX, int16_t rawY, int16_t &sx, int16_t &sy) {
    switch (bruceConfigPins.rotation & 3) {
        case 1:
            sx = rawY;
            sy = (TFT_WIDTH - 1) - rawX;
            break;
        case 2:
            sx = (TFT_WIDTH - 1) - rawX;
            sy = (TFT_HEIGHT - 1) - rawY;
            break;
        case 3:
            sx = (TFT_HEIGHT - 1) - rawY;
            sy = rawX;
            break;
        default:
            sx = rawX;
            sy = rawY;
            break;
    }
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // A previous deep sleep may have frozen the battery latch.
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)WS_SYS_EN);
    pinMode(WS_SYS_EN, OUTPUT);
    digitalWrite(WS_SYS_EN, HIGH);

    pinMode(WS_PWR_BTN, INPUT);
    pinMode(BTN_PIN, INPUT_PULLUP);

#if defined(BUZZ_PIN)
    pinMode(BUZZ_PIN, OUTPUT);
    digitalWrite(BUZZ_PIN, LOW);
#endif

    // Light the backlight before TFT init so the long boot is visible.
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    cst816Reset();
    setSysI2CBus(&Wire);
    Wire.begin(SYS_I2C_SDA, SYS_I2C_SCL);
    delay(10);
#if defined(HAS_RTC)
    _rtc.setWire(&Wire);
#endif

    uint8_t chipId = 0;
    touchReady = cst816Read(CST816_REG_CHIPID, &chipId, 1);
    if (touchReady) {
        Serial.printf("CST816T id=0x%02X\n", chipId);
        attachInterrupt(WS_TP_INT, cst816Isr, FALLING);
    } else {
        Serial.println("CST816T not found");
    }

    // Stock watch: no CC1101, NRF24, LoRa, or Ethernet. Users can show these
    // again from the menu and wire a module to the extension header later.
    bruceConfig.disabledMenus.push_back("RF");
    bruceConfig.disabledMenus.push_back("NRF24");
    bruceConfig.disabledMenus.push_back("LoRa");
    bruceConfig.disabledMenus.push_back("Ethernet");
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 255);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    // GPIO1, ADC1. Divider is 200k over 100k, so the cell is 3x the pin voltage.
    // 3.3V..4.2V is the single-cell window. Below ~3.0V there is no pack fitted.
    static bool adcReady = false;
    if (!adcReady) {
        pinMode(WS_BAT_ADC, INPUT);
        analogSetPinAttenuation(WS_BAT_ADC, ADC_11db);
        adcReady = true;
    }
    int vbatMv = (int)analogReadMilliVolts(WS_BAT_ADC) * 3;
    if (vbatMv < 3000) return 0;
    int percent = (vbatMv - 3300) * 100 / (4200 - 3300);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    ledcWrite(TFT_BL, dutyCycle);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    static bool bootWasDown = false;
    static bool pwrWasDown = false;
    static unsigned long pwrDownAt = 0;

    if (millis() - tm > 200 || LongPress) {
        if (touchReady && (cst816Irq || digitalRead(WS_TP_INT) == LOW)) {
            cst816Irq = false;
            int16_t rawX = 0, rawY = 0;
            if (cst816Point(rawX, rawY)) {
                int16_t sx = 0, sy = 0;
                cst816ToScreen(rawX, rawY, sx, sy);
                if (!wakeUpScreen()) AnyKeyPress = true;
                else return;

                touchPoint.x = sx;
                touchPoint.y = sy;
                touchPoint.pressed = true;
                touchHeatMap(touchPoint);
                tm = millis();
            }
        }

        bool bootDown = digitalRead(BTN_PIN) == BTN_ACT;
        if (bootDown && !bootWasDown) {
            if (!wakeUpScreen()) {
                AnyKeyPress = true;
                SelPress = true;
            }
            tm = millis();
        }
        bootWasDown = bootDown;

        // Ignore the press that just turned the board on.
        if (millis() > 1200) {
            bool pwrDown = digitalRead(WS_PWR_BTN) == LOW;
            if (pwrDown && !pwrWasDown) pwrDownAt = millis();
            if (!pwrDown && pwrWasDown) {
                unsigned long held = millis() - pwrDownAt;
                // Long holds belong to checkReboot(), which cuts SYS_EN.
                if (held > 40 && held < 700) {
                    if (!wakeUpScreen()) {
                        AnyKeyPress = true;
                        EscPress = true;
                    }
                    tm = millis();
                }
            }
            pwrWasDown = pwrDown;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    ledcWrite(TFT_BL, 0);
    digitalWrite(TFT_BL, LOW);
    tft.writecommand(0x10);
    // Dropping SYS_EN disconnects the battery path. USB can keep the 3V3 rail
    // up, so halt until BOOT in that case.
    gpio_hold_dis((gpio_num_t)WS_SYS_EN);
    digitalWrite(WS_SYS_EN, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN, BTN_ACT);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: goToDeepSleep
** location: mykeyboard.cpp
** Puts the device into DeepSleep without releasing the battery latch
**********************************************************************/
void goToDeepSleep() {
    ledcWrite(TFT_BL, 0);
    gpio_hold_en((gpio_num_t)WS_SYS_EN);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)DEEPSLEEP_WAKEUP_PIN, DEEPSLEEP_PIN_ACT);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    if (millis() < 1500) return;
    if (digitalRead(WS_PWR_BTN) != LOW) return;

    int countDown = 0;
    uint32_t time_count = millis();
    while (digitalRead(WS_PWR_BTN) == LOW) {
        if (millis() - time_count > 500) {
            if (countDown == 0) {
                int textWidth = tft.textWidth("PWR OFF IN 3/3", 1);
                tft.fillRect(tftWidth / 2 - textWidth / 2, 7, textWidth, 18, bruceConfig.bgColor);
            }
            tft.setCursor(tftWidth / 2 - 48, 12);
            tft.setTextSize(1);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            countDown = (millis() - time_count) / 1000 + 1;
            if (countDown > 3) countDown = 3;
            tft.printf("PWR OFF IN %d/3", countDown);
            if (millis() - time_count > 3000) powerOff();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (millis() - time_count > 500) {
        tft.fillRect(0, 7, tftWidth, 18, bruceConfig.bgColor);
        drawStatusBar();
    }
}

/***************************************************************************************
** Function name: isCharging()
** Description:   Determines if the device is charging
***************************************************************************************/
bool isCharging() {
    // ETA6098 does not bring a charge-status pin out to the ESP32.
    return false;
}
