#pragma once
#include <Arduino_GFX_Library.h>

#define LCD_BL 38

inline Arduino_DataBus *create_display_bus() {
    return new Arduino_ESP32RGBPanel(
        39 /* DE */, 40 /* VSYNC */, 41 /* HSYNC */, 42 /* PCLK */,
        15 /* R0 */, 16 /* R1 */, 17 /* R2 */, 18 /* R3 */, 8  /* R4 */,
        9  /* G0 */, 10 /* G1 */, 11 /* G2 */, 12 /* G3 */, 13 /* G4 */, 14 /* G5 */,
        21 /* B0 */, 47 /* B1 */, 48 /* B2 */, 45 /* B3 */, 3  /* B4 */,
        0 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
        0 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
        1 /* pclk_active_neg */, 12000000L /* prefer_speed */
    );
}

inline Arduino_GFX *create_display(Arduino_DataBus *bus) {
    return new Arduino_ST7701_RGBPanel(
        bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
        true /* IPS */, 480 /* width */, 480 /* height */,
        st7701_type1_init_operations, sizeof(st7701_type1_init_operations)
    );
}