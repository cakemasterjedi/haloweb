#pragma once
#include <stdint.h>
#include <string.h>

// Display modes. Keep in sync with MODES in web/index.html.
enum Mode : uint8_t {
    MODE_ROUNDEL = 0,  // static roundel
    MODE_SPIN,         // roundel with rotating quadrants
    MODE_STRIPES,      // M-style tri-colour stripes
    MODE_SOLID,        // solid colour (pulses when speed != 0)
    MODE_IMAGE,        // uploaded picture
    MODE_TEXT,         // custom text in the middle
    MODE_COUNT
};

static const uint8_t IMAGE_SLOTS = 5;
static const uint32_t SETTINGS_VERSION = 4;

struct Settings {
    uint32_t version;

    // General
    uint8_t mode;
    uint8_t brightness;  // 5..100 %
    int8_t speed;        // -100..100, meaning depends on mode
    uint8_t imageSlot;   // 0..IMAGE_SLOTS-1
    int16_t angle;       // rotation offset in degrees, to line the emblem up on the car
    uint8_t panel;       // index into PANEL_TYPES (display_config.h)

    // Roundel (colours are 0xRRGGBB)
    uint32_t quadA;      // top-left + bottom-right quadrants
    uint32_t quadB;      // top-right + bottom-left quadrants
    uint32_t ring;       // outer ring
    uint32_t rim;        // chrome rims
    uint32_t label;      // ring lettering
    char labelText[17];  // ring lettering, e.g. "BMW"
    uint8_t spacing;     // extra letter spacing on the ring (font units)

    // Stripes
    uint32_t stripe1, stripe2, stripe3, stripeBg;

    // Solid
    uint32_t solid;

    // Text
    uint32_t textBg, textFg;
    char text[33];  // '|' starts a new line

    // Wi-Fi (not used by the renderer)
    char apSsid[33];
    char apPass[65];
    char staSsid[33];
    char staPass[65];
};

inline void settingsDefaults(Settings &s) {
    memset(&s, 0, sizeof(s));
    s.version = SETTINGS_VERSION;
    s.mode = MODE_ROUNDEL;
    s.brightness = 80;
    s.speed = 30;
    s.imageSlot = 0;
    s.angle = 0;
    s.panel = 0;

    s.quadA = 0x1C69D4;
    s.quadB = 0xFFFFFF;
    s.ring = 0x000000;
    s.rim = 0xB8BCC2;
    s.label = 0xFFFFFF;
    strcpy(s.labelText, "BMW");
    s.spacing = 12;

    s.stripe1 = 0x3FA9F5;
    s.stripe2 = 0x1B3D8F;
    s.stripe3 = 0xE22718;
    s.stripeBg = 0x101214;

    s.solid = 0x1C69D4;

    s.textBg = 0x000000;
    s.textFg = 0xFFFFFF;
    strcpy(s.text, "M|POWER");

    strcpy(s.apSsid, "BMW-Emblem");
    strcpy(s.apPass, "emblem123");
}
