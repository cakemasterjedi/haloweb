#pragma once
#include <stdint.h>
#include <string.h>

// Display modes. Keep in sync with MODES in web/index.html.
enum Mode : uint8_t {
    MODE_ROUNDEL = 0,  // static roundel
    MODE_SPIN,         // roundel with rotating quadrants
    MODE_STRIPES,      // M-style tri-colour stripes
    MODE_IMAGE,        // uploaded picture / animation
    MODE_TEXT,         // custom text in the middle
    MODE_COUNT
};

// Power behaviour.
enum PowerMode : uint8_t {
    POWER_NORMAL = 0,  // turns itself off after autoOffMin
    POWER_CAR_SHOW,    // stays on (optionally for showHours), own brightness
};

// Where the battery voltage comes from.
enum VoltSource : uint8_t {
    VOLT_NONE = 0,
    VOLT_BOARD,   // the board's LiPo connector (GPIO4 through a 1:3 divider)
    VOLT_INA219,  // INA219 module on the I2C connector (0x40), e.g. the car battery
};

static const uint8_t IMAGE_SLOTS = 10;
static const uint32_t SETTINGS_VERSION = 5;

struct Settings {
    uint32_t version;

    // General
    uint8_t mode;
    uint8_t brightness;  // 5..100 %
    int8_t speed;        // -100..100, meaning depends on mode
    uint8_t imageSlot;   // 0..IMAGE_SLOTS-1
    int16_t angle;       // rotation offset in degrees, to line the emblem up on the car
    uint8_t panel;       // index into PANEL_TYPES (display_config.h)
    uint8_t startupAnim; // play the start-up animation at power-on
    uint8_t showIp;      // show the hotspot address on screen when it connects

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

    // Text
    uint32_t textBg, textFg;
    char text[33];  // '|' starts a new line

    // Power
    uint8_t powerMode;       // PowerMode
    uint16_t autoOffMin;     // normal mode: off after this many minutes (0 = never)
    uint8_t showHours;       // car show: off after this many hours (0 = until the battery says so)
    uint8_t showBrightness;  // car show brightness, %
    uint8_t lowVoltOn;       // shut down on low voltage
    uint16_t cutoffCV;       // low-voltage cutoff, centivolts
    uint8_t voltSource;      // VoltSource
    uint16_t voltCal;        // calibration factor x1000

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
    s.startupAnim = 1;
    s.showIp = 1;

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
    s.stripeBg = 0x16181B;

    s.textBg = 0x000000;
    s.textFg = 0xFFFFFF;
    strcpy(s.text, "M|POWER");

    s.powerMode = POWER_NORMAL;
    s.autoOffMin = 0;
    s.showHours = 0;
    s.showBrightness = 60;
    s.lowVoltOn = 0;
    s.cutoffCV = 1200;  // 12.00 V (car battery via INA219)
    s.voltSource = VOLT_NONE;
    s.voltCal = 1000;

    strcpy(s.apSsid, "BMW-Emblem");
    strcpy(s.apPass, "emblem123");
}
