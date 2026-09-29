#pragma once
// Pin-out for the Waveshare ESP32-S3-LCD-2.8C (2.8" round 480x480):
// ST7701S RGB panel, TCA9554 I/O expander on I2C (EXIO1 = LCD reset,
// EXIO3 = LCD SPI chip select), backlight PWM on GPIO6. See
// https://www.waveshare.com/wiki/ESP32-S3-LCD-2.8C
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#define I2C_SDA 15
#define I2C_SCL 7
#define PCA9554_ADDR 0x20

// Expander outputs
#define EXIO_LCD_RST 0  // EXIO1
#define EXIO_TP_RST 1   // EXIO2
#define EXIO_LCD_CS 2   // EXIO3
#define EXIO_SD_D3 3    // EXIO4

// Micro SD card (SD_MMC, 1-bit mode). CLK/CMD are shared with the display's
// set-up bus, which is only used once at boot.
#define SD_CLK 2
#define SD_CMD 1
#define SD_D0 42

// Backlight PWM pin. Set to -1 to dim in software instead.
#ifndef LCD_BL_PIN
#define LCD_BL_PIN 6
#endif

// Start-up sequence from Waveshare's own ESP32-S3-LCD-2.8C demo
// (Display_ST7701.cpp in ESP32-S3-LCD-2.8C-Demo.zip), converted to
// Arduino_GFX init operations.
static const uint8_t waveshare_2_8c_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    WRITE_COMMAND_8, 0xEF,
    WRITE_BYTES, 1, 0x08,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    WRITE_COMMAND_8, 0xC0,
    WRITE_BYTES, 2, 0x3B, 0x00,
    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 2, 0x10, 0x0C,
    WRITE_COMMAND_8, 0xC2,
    WRITE_BYTES, 2, 0x07, 0x0A,
    WRITE_COMMAND_8, 0xC7,
    WRITE_BYTES, 1, 0x00,
    WRITE_COMMAND_8, 0xCC,
    WRITE_BYTES, 1, 0x10,
    WRITE_COMMAND_8, 0xCD,
    WRITE_BYTES, 1, 0x08,
    WRITE_COMMAND_8, 0xB0,
    WRITE_BYTES, 16, 0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09, 0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11,
    WRITE_COMMAND_8, 0xB1,
    WRITE_BYTES, 16, 0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08, 0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
    WRITE_COMMAND_8, 0xB0,
    WRITE_BYTES, 1, 0x5D,
    WRITE_COMMAND_8, 0xB1,
    WRITE_BYTES, 1, 0x3E,
    WRITE_COMMAND_8, 0xB2,
    WRITE_BYTES, 1, 0x81,
    WRITE_COMMAND_8, 0xB3,
    WRITE_BYTES, 1, 0x80,
    WRITE_COMMAND_8, 0xB5,
    WRITE_BYTES, 1, 0x4E,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 1, 0x85,
    WRITE_COMMAND_8, 0xB8,
    WRITE_BYTES, 1, 0x20,
    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 1, 0x78,
    WRITE_COMMAND_8, 0xC2,
    WRITE_BYTES, 1, 0x78,
    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 1, 0x88,
    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x00, 0x02,
    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11, 0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30, 0x00, 0x33, 0x33,
    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12, 0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00, 0xF4, 0x00, 0x00, 0x00,
    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_COMMAND_8, 0xE4,
    WRITE_BYTES, 2, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16, 0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0, 0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0,
    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_COMMAND_8, 0xE7,
    WRITE_BYTES, 2, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16, 0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0, 0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0,
    WRITE_COMMAND_8, 0xE9,
    WRITE_BYTES, 2, 0x36, 0x01,
    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7, 0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40,
    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16, 0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF,
    WRITE_COMMAND_8, 0xEF,
    WRITE_BYTES, 6, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x3A,
    WRITE_BYTES, 1, 0x66,
    WRITE_COMMAND_8, 0x36,
    WRITE_BYTES, 1, 0x00,
    WRITE_COMMAND_8, 0x35,
    WRITE_BYTES, 1, 0x00,
    WRITE_COMMAND_8, 0x29,
    END_WRITE};

// The ST7701 needs a start-up sequence matched to the glass it is bonded to.
// The first entry is Waveshare's for this board; the others are fallbacks for
// similar 480x480 round panels, selectable from the phone page (Wi-Fi & system).
struct PanelType {
    const char *name;
    const uint8_t *init;
    size_t initLen;
    uint16_t hsyncPol, hfp, hpw, hbp;
    uint16_t vsyncPol, vfp, vpw, vbp;
    uint16_t pclkNeg;
};

static const PanelType PANEL_TYPES[] = {
    // Timings from the same demo: HPW 8, HBP 10, HFP 50, VPW 2, VBP 18, VFP 8.
    {"Waveshare ESP32-S3-LCD-2.8C", waveshare_2_8c_init_operations, sizeof(waveshare_2_8c_init_operations),
     1, 50, 8, 10, 1, 8, 2, 18, 0},
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

// Sends a panel's set-up commands the way Waveshare's demo does: hardware SPI,
// mode 0, every byte as a 9-bit word (1 D/C bit - 0 command, 1 data - then 8
// bits), chip select held low by the expander. Bit-banging this was too fast
// for the panel. The bus is released afterwards so the SD card can use the pins.
// Returns 0 on success, otherwise a code saying what failed.
inline int panelSendInit(const uint8_t *ops, size_t len, uint32_t hz = 10000000) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = 1;
    bus.miso_io_num = -1;
    bus.sclk_io_num = 2;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 64;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return 1;

    spi_device_interface_config_t dev = {};
    dev.command_bits = 1;
    dev.address_bits = 8;
    dev.mode = 0;
    dev.clock_speed_hz = hz;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    spi_device_handle_t handle;
    if (spi_bus_add_device(SPI2_HOST, &dev, &handle) != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        return 2;
    }

    auto send = [&](uint8_t dc, uint8_t value) {
        spi_transaction_t t = {};
        t.cmd = dc;
        t.addr = value;
        spi_device_polling_transmit(handle, &t);
    };

    int result = 0;
    for (size_t i = 0; i < len && result == 0;) {
        switch (ops[i++]) {
            case BEGIN_WRITE:
            case END_WRITE:
                break;
            case WRITE_COMMAND_8:
                send(0, ops[i++]);
                break;
            case WRITE_DATA_8:
                send(1, ops[i++]);
                break;
            case WRITE_BYTES: {
                uint8_t n = ops[i++];
                while (n--) send(1, ops[i++]);
                break;
            }
            case WRITE_C8_D8:
                send(0, ops[i++]);
                send(1, ops[i++]);
                break;
            case WRITE_C8_D16:
                send(0, ops[i++]);
                send(1, ops[i++]);
                send(1, ops[i++]);
                break;
            case DELAY:
                delay(ops[i++]);
                break;
            default:
                result = 3;  // an operation these tables don't use
        }
    }
    spi_bus_remove_device(handle);
    spi_bus_free(SPI2_HOST);
    return result;
}

inline const PanelType &panelType(uint8_t type) {
    return PANEL_TYPES[type < PANEL_TYPE_COUNT ? type : 0];
}

// RGB pixel bus, driven with ESP-IDF's esp_lcd driver directly (as in
// Waveshare's demo): frame buffer in PSRAM, frames pushed with
// esp_lcd_panel_draw_bitmap(). The panel must already have its set-up
// commands (panelSendInit).
inline esp_lcd_panel_handle_t createPanel(uint8_t type, uint32_t pclkHz = 16000000) {
    const PanelType &p = panelType(type);
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_PLL160M;
    cfg.timings.pclk_hz = pclkHz;
    cfg.timings.h_res = 480;
    cfg.timings.v_res = 480;
    cfg.timings.hsync_pulse_width = p.hpw;
    cfg.timings.hsync_back_porch = p.hbp;
    cfg.timings.hsync_front_porch = p.hfp;
    cfg.timings.vsync_pulse_width = p.vpw;
    cfg.timings.vsync_back_porch = p.vbp;
    cfg.timings.vsync_front_porch = p.vfp;
    cfg.timings.flags.hsync_idle_low = p.hsyncPol == 0;
    cfg.timings.flags.vsync_idle_low = p.vsyncPol == 0;
    cfg.timings.flags.pclk_active_neg = p.pclkNeg;
    cfg.data_width = 16;
    cfg.sram_trans_align = 8;
    cfg.psram_trans_align = 64;
    cfg.hsync_gpio_num = 38;
    cfg.vsync_gpio_num = 39;
    cfg.de_gpio_num = 40;
    cfg.pclk_gpio_num = 41;
    const int data[16] = {5, 45, 48, 47, 21,       // B1..B5
                          14, 13, 12, 11, 10, 9,   // G0..G5
                          46, 3, 8, 18, 17};       // R1..R5
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
    cfg.disp_gpio_num = -1;
    cfg.flags.fb_in_psram = 1;

    esp_lcd_panel_handle_t panel = nullptr;
    if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return nullptr;
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    return panel;
}
