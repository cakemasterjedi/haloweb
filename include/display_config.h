#pragma once
// Pin-out for the Waveshare-style ESP32-S3 2.8" round 480x480 board: ST7701S
// RGB panel, TCA9554/PCA9554 I/O expander on I2C (LCD reset + SPI chip
// select), backlight on a GPIO.
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#define I2C_SDA 15
#define I2C_SCL 7
#define PCA9554_ADDR 0x20

// Expander outputs
#define EXIO_LCD_RST 0  // EXIO1
#define EXIO_TP_RST 1   // EXIO2
#define EXIO_LCD_CS 2   // EXIO3

// Backlight PWM pin. Set to -1 to dim in software instead.
#ifndef LCD_BL_PIN
#define LCD_BL_PIN 6
#endif

// The ST7701 needs a start-up sequence matched to the glass it is bonded to,
// and sellers don't say which one they used. These are the candidates for
// 480x480 round panels; pick one from the phone page (Wi-Fi & system) if the
// screen stays black or the colours look wrong.
struct PanelType {
    const char *name;
    const uint8_t *init;
    size_t initLen;
    uint16_t hsyncPol, hfp, hpw, hbp;
    uint16_t vsyncPol, vfp, vpw, vbp;
    uint16_t pclkNeg;
};

static const PanelType PANEL_TYPES[] = {
    {"2.8in round (TL028WVC01)", TL028WVC01_init_operations, sizeof(TL028WVC01_init_operations),
     1, 50, 1, 30, 1, 20, 1, 30, 0},
    {"2.8in round (ST7701 type 6)", st7701_type6_init_operations, sizeof(st7701_type6_init_operations),
     1, 50, 1, 30, 1, 20, 1, 30, 0},
    {"2.1in round (TL021WVC02)", TL021WVC02_init_operations, sizeof(TL021WVC02_init_operations),
     1, 10, 8, 50, 1, 10, 8, 20, 0},
    {"ST7701 type 5", st7701_type5_init_operations, sizeof(st7701_type5_init_operations),
     1, 10, 8, 50, 1, 10, 8, 20, 0},
    {"ST7701 type 1 (original sketch)", st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
     0, 10, 8, 50, 0, 10, 8, 20, 1},
    {"ST7701 type 9", st7701_type9_init_operations, sizeof(st7701_type9_init_operations),
     1, 10, 8, 50, 1, 10, 8, 20, 0},
};
static const uint8_t PANEL_TYPE_COUNT = sizeof(PANEL_TYPES) / sizeof(PANEL_TYPES[0]);

static uint8_t expanderOut = 0;

inline bool expanderWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9554_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

inline void expanderSet(uint8_t bit, bool high) {
    expanderOut = high ? (expanderOut | (1 << bit)) : (expanderOut & ~(1 << bit));
    expanderWrite(0x01, expanderOut);
}

// Resets the panel and selects it for the SPI set-up commands. Returns false
// if the expander doesn't answer.
inline bool displayPowerOn() {
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(20);
    expanderOut = (1 << EXIO_TP_RST) | (1 << EXIO_LCD_CS);  // reset low, CS high, buzzer off
    bool ok = expanderWrite(0x01, expanderOut);
    ok = expanderWrite(0x03, 0x00) && ok;  // all expander pins are outputs
    delay(20);
    expanderSet(EXIO_LCD_RST, true);
    delay(120);
    expanderSet(EXIO_LCD_CS, false);
    return ok;
}

// Call once the panel has its set-up commands.
inline void displayDeselect() {
    expanderSet(EXIO_LCD_CS, true);
}

inline Arduino_RGB_Display *createDisplay(uint8_t type) {
    const PanelType &p = PANEL_TYPES[type < PANEL_TYPE_COUNT ? type : 0];
    Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
        40 /* DE */, 39 /* VSYNC */, 38 /* HSYNC */, 41 /* PCLK */,
        46 /* R1 */, 3 /* R2 */, 8 /* R3 */, 18 /* R4 */, 17 /* R5 */,
        14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
        5 /* B1 */, 45 /* B2 */, 48 /* B3 */, 47 /* B4 */, 21 /* B5 */,
        p.hsyncPol, p.hfp, p.hpw, p.hbp,
        p.vsyncPol, p.vfp, p.vpw, p.vbp,
        p.pclkNeg, 12000000L /* prefer_speed */
    );

    return new Arduino_RGB_Display(
        480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
        new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, GFX_NOT_DEFINED /* CS */, 2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */),
        GFX_NOT_DEFINED /* RST */, p.init, p.initLen
    );
}
