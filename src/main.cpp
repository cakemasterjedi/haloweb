#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#define I2C_SDA 15
#define I2C_SCL 7
#define PCA9554_ADDR 0x20

// ST7701S RGB parallel bus
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40 /* DE */, 39 /* VSYNC */, 38 /* HSYNC */, 41 /* PCLK */,
    46 /* R1 */, 3  /* R2 */, 8  /* R3 */, 18 /* R4 */, 17 /* R5 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    5  /* B1 */, 45 /* B2 */, 48 /* B3 */, 47 /* B4 */, 21 /* B5 */,
    0 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    0 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 12000000L /* prefer_speed */
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    new Arduino_SWSPI(
        GFX_NOT_DEFINED /* CS */, 2 /* SCLK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */
    ),
    GFX_NOT_DEFINED /* RST */,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations)
);

void writeExpander(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9554_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void setup() {
    Serial.begin(115200);

    // 1. Configure I2C bus
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50);

    // 2. Set expander to output mode
    writeExpander(0x03, 0x00);

    // 3. Reset pulse for ST7701S (Buzzer held LOW)
    writeExpander(0x01, 0b00000100); // Backlight on, RST low, Buzzer low
    delay(30);
    writeExpander(0x01, 0b00000110); // Backlight on, RST high, Buzzer low
    delay(100);

    // 4. Initialize display
    gfx->begin();
    gfx->fillScreen(RGB565_RED);
    gfx->drawCircle(240, 240, 220, RGB565_WHITE);
    gfx->drawCircle(240, 240, 100, RGB565_WHITE);
    gfx->fillCircle(240, 240, 20, RGB565_WHITE);
}

void loop() {
    delay(1000);
}