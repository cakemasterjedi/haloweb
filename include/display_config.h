#pragma once
// Pin-out for the ESP32-S3 2.8" round 480x480 board (ST7701S RGB panel with a
// PCA9554/TCA9554 I/O expander on I2C that drives the LCD reset line).
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#define I2C_SDA 15
#define I2C_SCL 7
#define PCA9554_ADDR 0x20

// Backlight PWM pin. Leave at -1 to dim in software instead (works on every
// board revision). If your board routes the backlight to a GPIO (commonly 6 on
// these Waveshare boards), set it here for real hardware dimming.
#ifndef LCD_BL_PIN
#define LCD_BL_PIN -1
#endif

inline void expanderWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9554_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Powers the panel up and pulses its reset line through the expander.
inline void displayPowerOn() {
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50);
    expanderWrite(0x03, 0x00);        // all expander pins are outputs
    expanderWrite(0x01, 0b00000100);  // backlight on, RST low, buzzer off
    delay(30);
    expanderWrite(0x01, 0b00000110);  // backlight on, RST high, buzzer off
    delay(100);
}

inline Arduino_RGB_Display *createDisplay() {
    Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
        40 /* DE */, 39 /* VSYNC */, 38 /* HSYNC */, 41 /* PCLK */,
        46 /* R1 */, 3 /* R2 */, 8 /* R3 */, 18 /* R4 */, 17 /* R5 */,
        14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
        5 /* B1 */, 45 /* B2 */, 48 /* B3 */, 47 /* B4 */, 21 /* B5 */,
        0 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
        0 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
        1 /* pclk_active_neg */, 12000000L /* prefer_speed */
    );

    return new Arduino_RGB_Display(
        480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
        new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, GFX_NOT_DEFINED /* CS */, 2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */),
        GFX_NOT_DEFINED /* RST */,
        st7701_type1_init_operations, sizeof(st7701_type1_init_operations)
    );
}
