// Digital BMW-style emblem for the ESP32-S3 2.8" round display.
//
// The board starts its own Wi-Fi network ("BMW-Emblem" / "emblem123" by
// default). Join it from your phone and a control page opens automatically
// (or browse to http://192.168.4.1 or http://emblem.local). Optionally it can
// also join an existing network such as your phone's hotspot.
#ifndef EMBLEM_DIAG
#include <Arduino.h>
#include <DNSServer.h>
#include <JPEGDEC.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_cache.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <esp_sntp.h>

#include "display_config.h"
#include "motion.h"
#include "renderer.h"
#include "rtc_clock.h"
#include "settings.h"
#include "web_ui.h"  // generated from web/index.html by tools/embed_web.py

static const size_t IMAGE_BYTES = size_t(Renderer::W) * Renderer::H * 2;
static const uint32_t FRAME_MS = 20;
static const uint32_t SAVE_DELAY_MS = 2000;
static const uint64_t ANIM_CAP_FLASH = 8ull << 20;  // biggest animation kept in internal flash
static const uint64_t ANIM_CAP_SD = 32ull << 20;    // ... and on a micro SD card

static esp_lcd_panel_handle_t panel;
static Renderer renderer;
static Settings settings;
static Preferences prefs;
static WebServer server(80);
static DNSServer dns;

static uint8_t lut5[32], lut6[64];
static bool softDim = LCD_BL_PIN < 0;
static bool forceFull = true;
static bool displayOk = false;
static uint32_t saveAt = 0;
static uint32_t restartAt = 0;
static uint32_t shutdownAt = 0;

// Power management state.
enum SleepReason : uint8_t { SLEEP_NONE = 0, SLEEP_OFF, SLEEP_LOWV };
RTC_DATA_ATTR static uint8_t sleepReason = SLEEP_NONE;  // survives deep sleep
static float volts = NAN;          // smoothed battery voltage, NAN if unknown
static uint32_t lastActivity = 0;  // last change made from the phone
static uint32_t showStart = 0;     // when car show mode started
static uint32_t lowSince = 0;      // when the voltage first dropped below the cutoff
static const uint32_t LOW_VOLT_GRACE_MS = 30000;  // ignore dips (e.g. engine cranking)
static const uint64_t LOW_VOLT_RECHECK_US = 10ull * 60 * 1000000;  // asleep: re-check every 10 min
static const float RESUME_MARGIN_V = 0.3f;

// What's on screen: a mode, and for MODE_IMAGE which slot. Usually the
// selected one, but the start-up clip and auto-cycle show others.
struct Item {
    uint8_t mode, slot;
};
static Item shown = {0xFF, 0};
static bool bootPlaying = false;   // start-up clip from a picture slot
static uint32_t bootStart = 0;
static bool cycleOn = false;       // auto-cycle has at least two designs
static bool cycleOverride = false; // showing cycleItem rather than the selection
static Item cycleItem = {0, 0};
static uint32_t cycleAt = 0;       // next change
static uint32_t cycleWaitLoops = UINT32_MAX;
static float motionFactor = 1;     // effects speed-up from acceleration
static bool nightNow = false;
static float brightNow = -1;       // backlight level being shown, %
static volatile bool ntpSynced = false;
static float uprightTilt = NAN;    // motion sensor set-up, step 1

// Crossfade between designs.
static const uint32_t FADE_MS = 450;
static uint16_t *fadeFrom = nullptr;  // what was on screen when the fade started
static uint32_t fadeStart = 0;

// ---------------------------------------------------------------------------
// Display output

// Logs to native USB and to UART0, so output shows up whichever port is used.
static void logf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.print(buf);
#endif
}

static void *psramAlloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// Car show mode has its own brightness; night dimming overrides both.
static uint8_t effectiveBrightness() {
    if (nightNow) return settings.nightBrightness;
    return settings.powerMode == POWER_CAR_SHOW ? settings.showBrightness : settings.brightness;
}

// Night by the emblem's clock (local time from the phone's time zone).
static bool nightTime() {
    if (!settings.autoDim || !clockValid()) return false;
    const int64_t local = int64_t(time(nullptr)) + settings.tzMin * 60;
    const int m = int((local % 86400 + 86400) % 86400 / 60);
    const int from = settings.nightFrom, to = settings.nightTo;
    return from <= to ? (m >= from && m < to) : (m >= from || m < to);
}

static void writeBacklight(float pct) {
#if LCD_BL_PIN >= 0
    ledcWrite(LCD_BL_PIN, uint32_t(pct * 1023 / 100 + 0.5f));
#endif
}

// The backlight follows effectiveBrightness() smoothly (see rampBrightness);
// software dimming (no backlight pin) switches straight away.
static void applyBrightness() {
    if (!displayOk) return;
    uint8_t pct = constrain(effectiveBrightness(), 5, 100);
    if (brightNow < 0) {
        brightNow = pct;
        writeBacklight(pct);
    }
    for (int i = 0; i < 32; i++) lut5[i] = i * pct / 100;
    for (int i = 0; i < 64; i++) lut6[i] = i * pct / 100;
    if (softDim) forceFull = true;
}

static void rampBrightness() {
    if (!displayOk || brightNow < 0) return;
    const float target = constrain(effectiveBrightness(), 5, 100);
    const float diff = target - brightNow;
    if (diff == 0) return;
    const float step = fmaxf(0.5f, fabsf(diff) * 0.08f);
    brightNow = fabsf(diff) <= step ? target : brightNow + (diff > 0 ? step : -step);
    writeBacklight(brightNow);
}

// Double buffering: the panel shows one frame buffer while we fill the other,
// then we switch at the start of the next refresh, so a frame is never shown
// half-drawn (that was the tearing on the spinning roundel).
static uint16_t *fbs[2];
static int backFb = 1;
static Rect lastDirty = {0, 0, Renderer::W, Renderer::H};  // changed area of the previous frame
static SemaphoreHandle_t vsyncSem;

// With bounce buffers the driver switches to a new frame buffer when it has
// finished copying a whole frame out, so that's the moment to wait for.
static bool IRAM_ATTR onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(vsyncSem, &woken);
    return woken == pdTRUE;
}

static bool setupFrameBuffers() {
    void *a = nullptr, *b = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b) != ESP_OK || !a || !b) return false;
    fbs[0] = static_cast<uint16_t *>(a);
    fbs[1] = static_cast<uint16_t *>(b);
    vsyncSem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_bounce_frame_finish = onFrameDone;
    esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);
    return true;
}

// The S3 can fall behind feeding the panel when PSRAM or flash is busy (e.g.
// saving settings), which leaves the picture shifted sideways ("drift").
// A restart re-syncs it at the next frame boundary, so it's invisible when
// nothing is wrong; call it after flash writes and now and then.
static void resyncDisplay() {
    if (panel) esp_lcd_rgb_panel_restart(panel);
}

// Shows the back buffer: flush it from the CPU cache, hand it to the driver
// (which switches buffers at the next vsync) and wait for that to happen, so
// the buffer we draw into next is no longer on screen.
static void swapBuffers() {
    uint16_t *fb = fbs[backFb];
    esp_cache_msync(fb, IMAGE_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    xSemaphoreTake(vsyncSem, 0);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, Renderer::W, Renderer::H, fb);
    xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(60));
    backFb ^= 1;
}

// Full-screen red, green, blue, white. If these don't appear, the panel isn't
// taking pixels at all (set-up commands or timing); if they do, the problem is
// in drawing the emblem.
static void showTestColours() {
    if (!panel || !fbs[0]) return;
    const uint16_t colours[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    const char *names[] = {"red", "green", "blue", "white"};
    for (int i = 0; i < 4; i++) {
        logf("Test colour: %s\n", names[i]);
        uint16_t *fb = fbs[backFb];
        for (int p = 0; p < Renderer::W * Renderer::H; p++) fb[p] = colours[i];
        swapBuffers();
        delay(600);
    }
    forceFull = true;  // redraw the emblem afterwards
    lastDirty = {0, 0, Renderer::W, Renderer::H};
}

static uint32_t framesShown = 0;

static Rect unite(Rect a, Rect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    int x0 = min(a.x, b.x), y0 = min(a.y, b.y);
    int x1 = max(a.x + a.w, b.x + b.w), y1 = max(a.y + a.h, b.y + b.h);
    return {int16_t(x0), int16_t(y0), int16_t(x1 - x0), int16_t(y1 - y0)};
}

// Copies the changed part of the rendered frame (brightness-scaled if dimming
// in software) into the back buffer and shows it. The back buffer last held
// the frame before the previous one, so it also needs the previous frame's
// changes; copying only that area keeps PSRAM traffic (and drift) down.
// During a crossfade the whole frame is mixed with the one it replaces.
static inline uint16_t mix565(uint16_t a, uint16_t b, int t) {
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int r = ar + ((((b >> 11) - ar) * t) >> 8);
    int g = ag + (((((b >> 5) & 63) - ag) * t) >> 8);
    int bl = ab + ((((b & 31) - ab) * t) >> 8);
    return uint16_t((r << 11) | (g << 5) | bl);
}

static void present(Rect r) {
    if (framesShown++ == 0) logf("First emblem frame sent to the display\n");
    const Rect full = {0, 0, Renderer::W, Renderer::H};
    const bool fading = fadeStart != 0;
    const int t = fading ? int(min<uint32_t>(256, (millis() - fadeStart) * 256 / FADE_MS)) : 256;
    const Rect area = fading ? full : unite(r, lastDirty);
    lastDirty = fading ? full : r;
    const uint16_t *src = renderer.frame();
    uint16_t *dst = fbs[backFb];
    const bool scale = softDim && effectiveBrightness() < 100;
    for (int y = area.y; y < area.y + area.h; y++) {
        const size_t off = size_t(y) * Renderer::W + area.x;
        if (!scale && !fading) {
            memcpy(dst + off, src + off, area.w * 2);
            continue;
        }
        for (int x = 0; x < area.w; x++) {
            uint16_t p = fading ? mix565(fadeFrom[off + x], src[off + x], t) : src[off + x];
            if (scale) p = (lut5[p >> 11] << 11) | (lut6[(p >> 5) & 63] << 5) | lut5[p & 31];
            dst[off + x] = p;
        }
    }
    swapBuffers();
    if (fading && t >= 256) {
        fadeStart = 0;
        forceFull = true;  // the other buffer still holds a mixed frame
    }
}

// Starts a crossfade from what's on screen now.
static void startFade() {
    if (!fadeFrom) fadeFrom = static_cast<uint16_t *>(psramAlloc(IMAGE_BYTES));
    if (!fadeFrom) return;
    // The front buffer is what's showing (already dimmed when dimming in software).
    memcpy(fadeFrom, softDim ? renderer.frame() : fbs[backFb ^ 1], IMAGE_BYTES);
    fadeStart = millis() | 1;
}

// ---------------------------------------------------------------------------
// Storage

// Pictures and animations go on the micro SD card when one is fitted at
// start-up, otherwise in the internal flash (LittleFS).
static bool sdOk = false;

static fs::FS &media() {
    return sdOk ? static_cast<fs::FS &>(SD_MMC) : static_cast<fs::FS &>(LittleFS);
}

static uint64_t mediaTotal() {
    return sdOk ? SD_MMC.totalBytes() : LittleFS.totalBytes();
}

static uint64_t mediaUsed() {
    return sdOk ? SD_MMC.usedBytes() : LittleFS.usedBytes();
}

static String mediaDir() {
    return sdOk ? "/emblem" : "";
}

static String imagePath(int slot) {
    return mediaDir() + "/img" + String(slot) + ".bin";
}

static String animPath(int slot) {
    return mediaDir() + "/anim" + String(slot) + ".mjp";
}

static String uploadPath() {
    return mediaDir() + "/upload.tmp";
}

// Largest animation that fits in the free space right now.
static size_t animMaxBytes() {
    const uint64_t margin = 256 * 1024;
    uint64_t total = mediaTotal(), used = mediaUsed();
    uint64_t space = total > used + margin ? total - used - margin : 0;
    uint64_t cap = sdOk ? ANIM_CAP_SD : ANIM_CAP_FLASH;
    return size_t(space < cap ? space : cap);
}

// Animations are a sequence of 480x480 JPEG frames, built by the phone page:
//   "EMJ1", uint16 frame count, uint16 reserved,
//   then per frame: uint16 delay (ms), uint16 reserved, uint32 length, JPEG data.
// Frames are read from storage one at a time as they play, so the size is
// limited by free storage rather than RAM.
struct Animation {
    File file;
    uint8_t *buf = nullptr;  // one JPEG frame
    uint32_t *offset = nullptr;
    uint32_t *length = nullptr;
    uint16_t *delay = nullptr;
    uint16_t count = 0;
    uint16_t index = 0;
    uint32_t nextAt = 0;
    uint32_t loops = 0;  // times it has played through
};
static Animation anim;
static JPEGDEC jpeg;

static void freeAnimation() {
    if (anim.file) anim.file.close();
    free(anim.buf);
    free(anim.offset);
    free(anim.length);
    free(anim.delay);
    anim = Animation();
}

// Reads the frame table. Returns the size of the largest frame, 0 if invalid.
static uint32_t parseAnimation(File &f) {
    uint8_t h[8];
    const size_t size = f.size();
    if (f.read(h, 8) != 8 || memcmp(h, "EMJ1", 4) != 0) return 0;
    uint16_t count = h[4] | (h[5] << 8);
    if (count == 0) return 0;
    anim.offset = static_cast<uint32_t *>(malloc(count * sizeof(uint32_t)));
    anim.length = static_cast<uint32_t *>(malloc(count * sizeof(uint32_t)));
    anim.delay = static_cast<uint16_t *>(malloc(count * sizeof(uint16_t)));
    if (!anim.offset || !anim.length || !anim.delay) return 0;
    size_t pos = 8;
    uint32_t largest = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (pos + 8 > size || !f.seek(pos) || f.read(h, 8) != 8) return 0;
        anim.delay[i] = h[0] | (h[1] << 8);
        uint32_t len = h[4] | (h[5] << 8) | (h[6] << 16) | (uint32_t(h[7]) << 24);
        pos += 8;
        if (len == 0 || pos + len > size) return 0;
        anim.offset[i] = pos;
        anim.length[i] = len;
        if (len > largest) largest = len;
        pos += len;
    }
    anim.count = count;
    return largest;
}

static int jpegDraw(JPEGDRAW *p) {
    uint16_t *dst = renderer.imageBuffer();
    int w = p->iWidthUsed;
    if (p->x + w > Renderer::W) w = Renderer::W - p->x;
    if (w <= 0) return 1;
    for (int r = 0; r < p->iHeight && p->y + r < Renderer::H; r++) {
        memcpy(dst + (p->y + r) * Renderer::W + p->x, p->pPixels + r * p->iWidth, w * 2);
    }
    return 1;
}

static bool decodeFrame(uint16_t i) {
    const uint32_t len = anim.length[i];
    if (!anim.file.seek(anim.offset[i]) || anim.file.read(anim.buf, len) != len) return false;
    if (!jpeg.openRAM(anim.buf, len, jpegDraw)) return false;
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    bool ok = jpeg.decode(0, 0, 0) == 1;
    jpeg.close();
    return ok;
}

static bool loadAnimation(const String &path) {
    anim.file = media().open(path, "r");
    uint32_t largest = anim.file ? parseAnimation(anim.file) : 0;
    if (largest) anim.buf = static_cast<uint8_t *>(psramAlloc(largest));
    if (!anim.buf || !decodeFrame(0)) {
        logf("Animation %s could not be loaded\n", path.c_str());
        freeAnimation();
        return false;
    }
    anim.nextAt = millis() + anim.delay[0];
    logf("Animation %s: %u frames, %u KB\n", path.c_str(), anim.count, unsigned(anim.file.size() / 1024));
    return true;
}

// Loads the picture or animation in a slot into the renderer.
static void loadImage(int slot) {
    if (!displayOk) return;
    freeAnimation();
    if (media().exists(animPath(slot))) {
        renderer.imageChanged(loadAnimation(animPath(slot)));
        resyncDisplay();
        return;
    }
    bool ok = false;
    File f = media().open(imagePath(slot), "r");
    if (f && f.size() == IMAGE_BYTES) {
        ok = f.read(reinterpret_cast<uint8_t *>(renderer.imageBuffer()), IMAGE_BYTES) == IMAGE_BYTES;
    }
    if (f) f.close();
    renderer.imageChanged(ok);
    resyncDisplay();  // reading flash can leave the picture shifted
}

// Shows the next animation frame when it's due. The speed setting and the
// motion effect scale the frame times.
static void stepAnimation(uint32_t now) {
    if (anim.count < 2 || shown.mode != MODE_IMAGE || int32_t(now - anim.nextAt) < 0) return;
    anim.index = (anim.index + 1) % anim.count;
    if (anim.index == 0) anim.loops++;
    if (decodeFrame(anim.index)) renderer.imageChanged(true);
    resyncDisplay();  // each frame is read from flash / SD
    const float speed = settings.animSpeed / 100.0f * motionFactor;
    anim.nextAt = now + max<uint32_t>(uint32_t(anim.delay[anim.index] / speed), 20);
}

static void loadSettings() {
    settingsDefaults(settings);
    prefs.begin("emblem", false);
    // Versions 5 and 6 are the first part of the current layout: the fields
    // added since keep their defaults.
    Settings stored;
    settingsDefaults(stored);
    const size_t len = prefs.getBytesLength("s");
    if ((len == sizeof(Settings) || len == SETTINGS_V6_SIZE) && prefs.getBytes("s", &stored, len) == len) {
        const bool current = len == sizeof(Settings) && stored.version == SETTINGS_VERSION;
        const bool older = len == SETTINGS_V6_SIZE && (stored.version == 5 || stored.version == 6);
        if (current || older) {
            // Version 5's default blue was darker.
            if (stored.version == 5 && stored.quadA == 0x1C69D4) stored.quadA = CLASSIC_BLUE;
            stored.version = SETTINGS_VERSION;
            stored.unused = 0;
            settings = stored;
        }
    }
}

static void saveSettings() {
    prefs.putBytes("s", &settings, sizeof(settings));
    saveAt = 0;
    resyncDisplay();
}

// ---------------------------------------------------------------------------
// Power: battery voltage, auto-off, car show mode, deep sleep

// Uncalibrated reading from the selected source, NAN if there's none.
static float readRawVolts() {
    switch (settings.voltSource) {
        case VOLT_BOARD: {
            uint32_t mv = 0;
            for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BAT_ADC_PIN);
            return mv / 16 / 1000.0f * BAT_DIVIDER;
        }
        case VOLT_INA219: {
            Wire.beginTransmission(INA219_ADDR);
            Wire.write(0x02);  // bus voltage register
            if (Wire.endTransmission(false) != 0 || Wire.requestFrom(INA219_ADDR, 2) != 2) return NAN;
            uint16_t raw = (Wire.read() << 8) | Wire.read();
            return (raw >> 3) * 0.004f;
        }
        default:
            return NAN;
    }
}

// Calibrated voltage; readings under 1 V mean nothing is connected.
static float readVolts() {
    float v = readRawVolts() * settings.voltCal / 1000.0f;
    return (isnan(v) || v < 1.0f) ? NAN : v;
}

// Milliseconds until the emblem turns itself off (never below 0), or -1 if
// no timer is set.
static int64_t offInMs(uint32_t now) {
    int64_t left;
    if (settings.powerMode == POWER_CAR_SHOW) {
        if (!settings.showHours) return -1;
        left = int64_t(settings.showHours) * 3600000 - int64_t(now - showStart);
    } else {
        if (!settings.autoOffMin) return -1;
        left = int64_t(settings.autoOffMin) * 60000 - int64_t(now - lastActivity);
    }
    return left < 0 ? 0 : left;
}

// Deep sleep: backlight held off, panel held in reset, Wi-Fi off. Wakes on
// the BOOT button, a power cycle, or (after a low-voltage shutdown) every
// 10 minutes to see whether the battery has recovered.
static void enterSleep(uint8_t reason) {
    logf("Sleeping (%s)\n", reason == SLEEP_LOWV ? "low voltage" : "turned off");
    Serial.flush();
    sleepReason = reason;
#if LCD_BL_PIN >= 0
    ledcDetach(LCD_BL_PIN);
    pinMode(LCD_BL_PIN, OUTPUT);
    digitalWrite(LCD_BL_PIN, LOW);
    gpio_hold_en(gpio_num_t(LCD_BL_PIN));
    gpio_deep_sleep_hold_en();
#endif
    expanderSet(EXIO_LCD_RST, false);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // BOOT button
    if (reason == SLEEP_LOWV && settings.voltSource != VOLT_NONE) esp_sleep_enable_timer_wakeup(LOW_VOLT_RECHECK_US);
    esp_deep_sleep_start();
}

// Says goodbye on the screen, fades out and goes to sleep.
static void shutdownNow(uint8_t reason) {
    if (saveAt) saveSettings();
    if (displayOk) {
        present(renderer.showMessage(reason == SLEEP_LOWV ? "LOW|VOLTAGE" : "GOOD|BYE",
                                     reason == SLEEP_LOWV ? 0xE22718 : 0xC8CCD2));
        delay(1600);
#if LCD_BL_PIN >= 0
        for (int i = 20; i >= 0; i--) {
            writeBacklight(fmaxf(brightNow, 0) * i / 20);
            delay(30);
        }
#endif
    }
    enterSleep(reason);
}

// Called once a second.
static void powerTick(uint32_t now) {
    float v = readVolts();
    volts = isnan(v) ? NAN : (isnan(volts) ? v : volts * 0.7f + v * 0.3f);

    const float cutoff = settings.cutoffCV / 100.0f;
    if (settings.lowVoltOn && !isnan(volts) && volts < cutoff) {
        if (!lowSince) lowSince = now ? now : 1;
        if (now - lowSince >= LOW_VOLT_GRACE_MS) shutdownNow(SLEEP_LOWV);
    } else {
        lowSince = 0;
    }
    const bool night = nightTime();
    if (night != nightNow) {
        nightNow = night;
        logf("Night dimming %s\n", night ? "on" : "off");
        applyBrightness();
    }
    const bool timed = settings.powerMode == POWER_CAR_SHOW ? settings.showHours : settings.autoOffMin;
    if (timed && offInMs(now) <= 0) shutdownNow(SLEEP_OFF);
}

// After waking from a low-voltage shutdown: go straight back to sleep unless
// the battery has recovered (e.g. the engine is running / it was charged).
static void checkLowVoltageWake() {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER || sleepReason != SLEEP_LOWV) return;
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(20);
    float v = readVolts();
    float resume = settings.cutoffCV / 100.0f + RESUME_MARGIN_V;
    logf("Low-voltage check: %.2f V (resume at %.2f V)\n", v, resume);
    if (isnan(v) || v < resume) {
        sleepReason = SLEEP_LOWV;
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
        esp_sleep_enable_timer_wakeup(LOW_VOLT_RECHECK_US);
        esp_deep_sleep_start();
    }
}

// ---------------------------------------------------------------------------
// Web API

static String colorHex(uint32_t c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", unsigned(c & 0xFFFFFF));
    return buf;
}

static String jsonString(const char *s) {
    String out = "\"";
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') out += '\\';
        if (uint8_t(*s) < 0x20) continue;
        out += *s;
    }
    return out + "\"";
}

// What each slot holds (0 empty, 1 picture, 2 animation), from one directory
// listing rather than probing every file name.
static void listSlots(uint8_t *slots) {
    memset(slots, 0, IMAGE_SLOTS);
    File dir = media().open(sdOk ? "/emblem" : "/");
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String name = f.name();
        name = name.substring(name.lastIndexOf('/') + 1);
        int slot = -1;
        uint8_t kind = 0;
        if (name.startsWith("anim") && name.endsWith(".mjp")) {
            slot = name.substring(4, name.length() - 4).toInt();
            kind = 2;
        } else if (name.startsWith("img") && name.endsWith(".bin")) {
            slot = name.substring(3, name.length() - 4).toInt();
            kind = 1;
        }
        if (slot >= 0 && slot < IMAGE_SLOTS && kind > slots[slot]) slots[slot] = kind;
    }
}

// ---------------------------------------------------------------------------
// What's on screen: selection, auto-cycle, start-up clip

// Designs in the auto-cycle list (empty picture slots are skipped).
static int cycleList(Item *out) {
    uint8_t slots[IMAGE_SLOTS];
    listSlots(slots);
    int n = 0;
    for (uint8_t m : {MODE_ROUNDEL, MODE_SPIN, MODE_STRIPES, MODE_TEXT}) {
        if (settings.cycleItems & (1u << m)) out[n++] = {m, settings.imageSlot};
    }
    for (uint8_t i = 0; i < IMAGE_SLOTS; i++) {
        if ((settings.cycleItems & (1u << (8 + i))) && slots[i]) out[n++] = {MODE_IMAGE, i};
    }
    return n;
}

static bool sameItem(Item a, Item b) {
    return a.mode == b.mode && (a.mode != MODE_IMAGE || a.slot == b.slot);
}

static void updateCycleOn() {
    Item list[IMAGE_SLOTS + 5];
    cycleOn = settings.cycleSec && cycleList(list) >= 2;
    if (!cycleOn) cycleOverride = false;
}

static Item wantedItem() {
    if (bootPlaying) return {MODE_IMAGE, uint8_t(settings.bootSlot - 1)};
    if (cycleOn && cycleOverride) return cycleItem;
    return {settings.mode, settings.imageSlot};
}

// Shows wantedItem(): loads its picture if needed, crossfades when it changes.
// reload: the slot's file changed.
static void refreshDisplay(bool fade, bool reload = false) {
    if (!displayOk) return;
    const Item it = wantedItem();
    const bool changed = shown.mode == 0xFF || !sameItem(it, shown);
    if (changed && fade && settings.fades && shown.mode != 0xFF && !renderer.introRunning()) startFade();
    if (it.mode == MODE_IMAGE && (changed || reload)) {
        loadImage(it.slot);
    } else if (changed && it.mode != MODE_IMAGE) {
        freeAnimation();
    }
    Settings s = settings;
    s.mode = it.mode;
    s.imageSlot = it.slot;
    renderer.apply(s);
    shown = it;
}

// Next design: the next one in the auto-cycle list, or, without auto-cycle,
// the next mode / filled picture slot (which becomes the selection).
static void nextItem() {
    Item list[IMAGE_SLOTS + 5];
    int n = 0;
    if (cycleOn) {
        n = cycleList(list);
    } else {
        uint8_t slots[IMAGE_SLOTS];
        listSlots(slots);
        for (uint8_t m : {MODE_ROUNDEL, MODE_SPIN, MODE_STRIPES, MODE_TEXT}) list[n++] = {m, settings.imageSlot};
        for (uint8_t i = 0; i < IMAGE_SLOTS; i++) {
            if (slots[i]) list[n++] = {MODE_IMAGE, i};
        }
    }
    int at = -1;
    for (int i = 0; i < n; i++) {
        if (sameItem(list[i], shown)) at = i;
    }
    const Item next = list[(at + 1) % n];
    if (cycleOn) {
        cycleItem = next;
        cycleOverride = true;
    } else {
        settings.mode = next.mode;
        settings.imageSlot = next.slot;
        saveAt = millis() + SAVE_DELAY_MS;
    }
    cycleAt = millis() + settings.cycleSec * 1000u;
    cycleWaitLoops = UINT32_MAX;
    refreshDisplay(true);
}

// Auto-cycle: change design when it's time, letting an animation finish its
// current loop first (up to a minute).
static void cycleTick(uint32_t now) {
    if (!cycleOn || bootPlaying || renderer.introRunning() || int32_t(now - cycleAt) < 0) return;
    if (shown.mode == MODE_IMAGE && anim.count > 1 && now - cycleAt < 60000) {
        if (cycleWaitLoops == UINT32_MAX) cycleWaitLoops = anim.loops;
        if (anim.loops == cycleWaitLoops) return;
    }
    nextItem();
}

// Start-up clip: an animation plays through once, a picture shows for 3 s.
static void bootTick(uint32_t now) {
    if (!bootPlaying) return;
    const uint32_t t = now - bootStart;
    const bool done = anim.count > 1 ? anim.loops >= 1 : t >= 3000;
    if (!done && t < 20000) return;
    bootPlaying = false;
    cycleAt = now + settings.cycleSec * 1000u;
    refreshDisplay(true);
}

static void sendState() {
    String j;
    j.reserve(1024);
    j += "{\"mode\":" + String(settings.mode);
    j += ",\"brightness\":" + String(settings.brightness);
    j += ",\"speed\":" + String(settings.speed);
    j += ",\"imageSlot\":" + String(settings.imageSlot);
    j += ",\"angle\":" + String(settings.angle);
    j += ",\"quadA\":\"" + colorHex(settings.quadA) + "\"";
    j += ",\"quadB\":\"" + colorHex(settings.quadB) + "\"";
    j += ",\"ring\":\"" + colorHex(settings.ring) + "\"";
    j += ",\"rim\":\"" + colorHex(settings.rim) + "\"";
    j += ",\"label\":\"" + colorHex(settings.label) + "\"";
    j += ",\"labelText\":" + jsonString(settings.labelText);
    j += ",\"spacing\":" + String(settings.spacing);
    j += ",\"stripe1\":\"" + colorHex(settings.stripe1) + "\"";
    j += ",\"stripe2\":\"" + colorHex(settings.stripe2) + "\"";
    j += ",\"stripe3\":\"" + colorHex(settings.stripe3) + "\"";
    j += ",\"stripeBg\":\"" + colorHex(settings.stripeBg) + "\"";
    j += ",\"textBg\":\"" + colorHex(settings.textBg) + "\"";
    j += ",\"textFg\":\"" + colorHex(settings.textFg) + "\"";
    j += ",\"text\":" + jsonString(settings.text);
    j += ",\"apSsid\":" + jsonString(settings.apSsid);
    j += ",\"staSsid\":" + jsonString(settings.staSsid);
    j += ",\"staIp\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String()) + "\"";
    j += ",\"panel\":" + String(settings.panel);
    j += ",\"panels\":[";
    for (int i = 0; i < PANEL_TYPE_COUNT; i++) {
        if (i) j += ",";
        j += jsonString(PANEL_TYPES[i].name);
    }
    j += "],\"display\":" + String(displayOk ? "true" : "false");
    j += ",\"slots\":[";
    uint8_t slots[IMAGE_SLOTS];
    listSlots(slots);
    for (int i = 0; i < IMAGE_SLOTS; i++) {
        if (i) j += ",";
        j += String(slots[i]);
    }
    j += "],\"storage\":\"" + String(sdOk ? "sd" : "flash") + "\"";
    j += ",\"fsUsedKB\":" + String(unsigned(mediaUsed() / 1024));
    j += ",\"fsTotalKB\":" + String(unsigned(mediaTotal() / 1024));
    j += ",\"animMax\":" + String(unsigned(animMaxBytes()));
    // Power
    const uint32_t now = millis();
    const int64_t offIn = offInMs(now);
    j += ",\"startupAnim\":" + String(settings.startupAnim);
    j += ",\"powerMode\":" + String(settings.powerMode);
    j += ",\"autoOffMin\":" + String(settings.autoOffMin);
    j += ",\"showHours\":" + String(settings.showHours);
    j += ",\"showBrightness\":" + String(settings.showBrightness);
    j += ",\"lowVoltOn\":" + String(settings.lowVoltOn);
    j += ",\"cutoff\":" + String(settings.cutoffCV / 100.0f, 2);
    j += ",\"voltSource\":" + String(settings.voltSource);
    j += ",\"volts\":" + (isnan(volts) ? String("null") : String(volts, 2));
    j += ",\"offIn\":" + String(offIn < 0 ? -1 : long(offIn / 1000));
    j += ",\"lowFor\":" + String(lowSince ? long((now - lowSince) / 1000) : 0L);
    // Designs: auto-cycle, animations, start-up, transitions
    j += ",\"cycleItems\":" + String(settings.cycleItems);
    j += ",\"cycleSec\":" + String(settings.cycleSec);
    j += ",\"cycling\":" + String(cycleOn ? "true" : "false");
    j += ",\"showMode\":" + String(shown.mode == 0xFF ? settings.mode : shown.mode);
    j += ",\"showSlot\":" + String(shown.mode == 0xFF ? settings.imageSlot : shown.slot);
    j += ",\"animSpeed\":" + String(settings.animSpeed);
    j += ",\"bootSlot\":" + String(settings.bootSlot);
    j += ",\"fades\":" + String(settings.fades);
    // Clock and night dimming
    j += ",\"autoDim\":" + String(settings.autoDim);
    j += ",\"nightBrightness\":" + String(settings.nightBrightness);
    j += ",\"nightFrom\":" + String(settings.nightFrom);
    j += ",\"nightTo\":" + String(settings.nightTo);
    j += ",\"night\":" + String(nightNow ? "true" : "false");
    j += ",\"rtc\":" + String(rtcPresent() ? "true" : "false");
    if (clockValid()) {
        const int64_t local = int64_t(time(nullptr)) + settings.tzMin * 60;
        const int m = int((local % 86400 + 86400) % 86400 / 60);
        char hm[8];
        snprintf(hm, sizeof(hm), "%02d:%02d", m / 60, m % 60);
        j += ",\"clock\":\"" + String(hm) + "\"";
    } else {
        j += ",\"clock\":null";
    }
    // Motion sensor
    j += ",\"imu\":" + String(motionPresent() ? "true" : "false");
    j += ",\"motionReact\":" + String(settings.motionReact);
    j += ",\"doubleTap\":" + String(settings.doubleTap);
    j += ",\"levelSet\":" + String(settings.levelSign ? "true" : "false");
    if (motionPresent()) {
        j += ",\"moving\":" + String(motionMoving() ? "true" : "false");
        j += ",\"jolt\":" + String(motionPeakJolt(), 2);
        j += ",\"boost\":" + String(motionBoost(), 2);
    }
    j += "}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", j);
}

static bool argInt(const char *name, long lo, long hi, long &out) {
    if (!server.hasArg(name)) return false;
    out = constrain(server.arg(name).toInt(), lo, hi);
    return true;
}

static void argColor(const char *name, uint32_t &out) {
    if (!server.hasArg(name)) return;
    String v = server.arg(name);
    if (v.startsWith("#")) v.remove(0, 1);
    if (v.length() == 6) out = strtoul(v.c_str(), nullptr, 16);
}

static void argText(const char *name, char *out, size_t size) {
    if (!server.hasArg(name)) return;
    strlcpy(out, server.arg(name).c_str(), size);
}

// Only setting the clock (the page does that whenever it opens): nothing to redraw.
static bool clockOnly() {
    for (int i = 0; i < server.args(); i++) {
        const String n = server.argName(i);
        if (n != "clock" && n != "tz" && n != "plain") return false;
    }
    return true;
}

static void handleSet() {
    long v;
    if (clockOnly()) {
        if (argInt("tz", -840, 840, v) && settings.tzMin != v) {
            settings.tzMin = v;
            saveAt = millis() + SAVE_DELAY_MS;
        }
        const time_t t = time_t(strtoll(server.arg("clock").c_str(), nullptr, 10));
        if (t > 1700000000) rtcSet(t);
        const bool night = nightTime();
        if (night != nightNow) {
            nightNow = night;
            applyBrightness();
        }
        sendState();
        return;
    }
    const uint8_t oldMode = settings.mode;
    const uint8_t oldSlot = settings.imageSlot;
    const uint8_t oldBrightness = settings.brightness;
    (void)oldBrightness;

    if (argInt("mode", 0, MODE_COUNT - 1, v)) settings.mode = v;
    if (argInt("brightness", 5, 100, v)) settings.brightness = v;
    if (argInt("speed", -100, 100, v)) settings.speed = v;
    if (argInt("imageSlot", 0, IMAGE_SLOTS - 1, v)) settings.imageSlot = v;
    if (argInt("angle", -180, 180, v)) settings.angle = v;
    if (argInt("spacing", 0, 30, v)) settings.spacing = v;
    argColor("quadA", settings.quadA);
    argColor("quadB", settings.quadB);
    argColor("ring", settings.ring);
    argColor("rim", settings.rim);
    argColor("label", settings.label);
    argColor("stripe1", settings.stripe1);
    argColor("stripe2", settings.stripe2);
    argColor("stripe3", settings.stripe3);
    argColor("stripeBg", settings.stripeBg);
    argColor("textBg", settings.textBg);
    argColor("textFg", settings.textFg);
    argText("labelText", settings.labelText, sizeof(settings.labelText));
    argText("text", settings.text, sizeof(settings.text));

    // Power
    const uint8_t oldPower = settings.powerMode;
    const uint8_t oldShowBrightness = settings.showBrightness;
    if (argInt("startupAnim", 0, 1, v)) settings.startupAnim = v;
    if (argInt("powerMode", 0, 1, v)) settings.powerMode = v;
    if (argInt("autoOffMin", 0, 720, v)) settings.autoOffMin = v;
    if (argInt("showHours", 0, 48, v)) settings.showHours = v;
    if (argInt("showBrightness", 5, 100, v)) settings.showBrightness = v;
    if (argInt("lowVoltOn", 0, 1, v)) settings.lowVoltOn = v;
    if (argInt("voltSource", 0, 2, v)) {
        if (settings.voltSource != v) volts = NAN;
        settings.voltSource = v;
    }
    if (server.hasArg("cutoff")) settings.cutoffCV = constrain(lroundf(server.arg("cutoff").toFloat() * 100), 250, 1600);
    if (server.hasArg("calibrateTo")) {
        // Match a multimeter: scale so the current reading equals what was entered.
        float raw = readRawVolts(), actual = server.arg("calibrateTo").toFloat();
        if (!isnan(raw) && raw > 0.5f && actual > 0.5f) {
            settings.voltCal = constrain(lroundf(actual / raw * 1000), 500, 2000);
            volts = NAN;
        }
    }
    if (server.hasArg("calibrateReset")) settings.voltCal = 1000;

    // Designs, clock, motion
    if (argInt("cycleItems", 0, 0x3FFFF, v)) settings.cycleItems = v;
    if (argInt("cycleSec", 0, 3600, v)) settings.cycleSec = v;
    if (argInt("animSpeed", 25, 300, v)) settings.animSpeed = v;
    if (argInt("bootSlot", 0, IMAGE_SLOTS, v)) settings.bootSlot = v;
    if (argInt("fades", 0, 1, v)) settings.fades = v;
    if (argInt("autoDim", 0, 1, v)) settings.autoDim = v;
    if (argInt("nightBrightness", 5, 100, v)) settings.nightBrightness = v;
    if (argInt("nightFrom", 0, 1439, v)) settings.nightFrom = v;
    if (argInt("nightTo", 0, 1439, v)) settings.nightTo = v;
    if (argInt("tz", -840, 840, v)) settings.tzMin = v;
    if (server.hasArg("clock")) {
        const time_t t = time_t(strtoll(server.arg("clock").c_str(), nullptr, 10));
        if (t > 1700000000) rtcSet(t);
    }
    if (argInt("motionReact", 0, 100, v)) settings.motionReact = v;
    if (argInt("doubleTap", 0, 3, v)) settings.doubleTap = v;
    motionSetTapSensitivity(settings.doubleTap);
    if (settings.powerMode != oldPower && settings.powerMode == POWER_CAR_SHOW) showStart = millis();
    lastActivity = millis();

    // Picking a design shows it straight away; auto-cycle carries on from there.
    if (settings.mode != oldMode || settings.imageSlot != oldSlot) {
        cycleOverride = false;
        cycleAt = millis() + settings.cycleSec * 1000u;
        cycleWaitLoops = UINT32_MAX;
    }
    const bool wasCycling = cycleOn;
    updateCycleOn();
    if (cycleOn && !wasCycling) cycleAt = millis() + settings.cycleSec * 1000u;
    nightNow = nightTime();
    if (settings.brightness != oldBrightness || settings.showBrightness != oldShowBrightness ||
        settings.powerMode != oldPower || server.hasArg("autoDim") || server.hasArg("nightBrightness")) {
        applyBrightness();
    }
    refreshDisplay(true);
    saveAt = millis() + SAVE_DELAY_MS;
    sendState();
}

static File uploadFile;
static size_t uploadBytes;
static size_t uploadLimit;

// Streams an upload to /upload.tmp; stops writing once it passes uploadLimit.
static void handleUpload(size_t limit) {
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        uploadBytes = 0;
        uploadLimit = limit;
        media().remove(uploadPath());
        uploadFile = media().open(uploadPath(), "w");
    } else if (up.status == UPLOAD_FILE_WRITE) {
        uploadBytes += up.currentSize;
        if (uploadFile && uploadBytes <= uploadLimit && uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
            uploadFile.close();  // storage full
        }
    } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) uploadFile.close();
    }
}

// Moves the finished upload into a slot, replacing whatever the slot held.
static bool storeUpload(long slot, const String &path) {
    File f = media().open(uploadPath(), "r");
    size_t stored = f ? f.size() : 0;
    if (f) f.close();
    if (stored != uploadBytes) {
        media().remove(uploadPath());
        server.send(507, "text/plain", "Storage is full. Delete a picture or animation and try again.");
        return false;
    }
    // Stop playing the slot before replacing its file.
    if (shown.mode == MODE_IMAGE && slot == shown.slot) freeAnimation();
    media().remove(imagePath(slot));
    media().remove(animPath(slot));
    if (!media().rename(uploadPath(), path)) {
        server.send(500, "text/plain", "Could not save the file");
        return false;
    }
    settings.mode = MODE_IMAGE;
    settings.imageSlot = slot;
    lastActivity = millis();
    cycleOverride = false;
    updateCycleOn();
    refreshDisplay(true, true);
    saveAt = millis() + SAVE_DELAY_MS;
    sendState();
    return true;
}

static void handleImageDone() {
    resyncDisplay();
    long slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= IMAGE_SLOTS || uploadBytes != IMAGE_BYTES) {
        media().remove(uploadPath());
        server.send(400, "text/plain", "Expected a 480x480 RGB565 image (" + String(IMAGE_BYTES) + " bytes)");
        return;
    }
    storeUpload(slot, imagePath(slot));
}

static void handleAnimDone() {
    resyncDisplay();
    long slot = server.arg("slot").toInt();
    char magic[4] = {0};
    File f = media().open(uploadPath(), "r");
    if (f) {
        f.read(reinterpret_cast<uint8_t *>(magic), 4);
        f.close();
    }
    if (slot < 0 || slot >= IMAGE_SLOTS || uploadBytes > uploadLimit || memcmp(magic, "EMJ1", 4) != 0) {
        media().remove(uploadPath());
        server.send(400, "text/plain", "Not a valid animation, or bigger than the free space (" + String(uploadLimit / 1024) + " KB)");
        return;
    }
    storeUpload(slot, animPath(slot));
}

static void handleImageDelete() {
    long slot = server.arg("slot").toInt();
    if (slot >= 0 && slot < IMAGE_SLOTS) {
        const bool onScreen = shown.mode == MODE_IMAGE && shown.slot == slot;
        if (onScreen) freeAnimation();
        media().remove(imagePath(slot));
        media().remove(animPath(slot));
        updateCycleOn();
        if (onScreen) refreshDisplay(false, true);
    }
    sendState();
}

// Angle difference a - b wrapped to -180..180.
static float angleDiff(float a, float b) {
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
    return d;
}

// Motion sensor set-up (upright, then turned clockwise) and "level now".
static void handleMotion() {
    if (!motionPresent()) {
        server.send(503, "text/plain", "No motion sensor found on this board");
        return;
    }
    const String what = server.arg("do");
    if (what == "upright") {
        uprightTilt = motionTiltDeg();
        server.send(200, "text/plain", "Got it. Now turn the emblem clockwise (about a quarter turn) and tap Step 2.");
    } else if (what == "clockwise") {
        if (isnan(uprightTilt)) {
            server.send(400, "text/plain", "Do step 1 first");
            return;
        }
        const float d = angleDiff(motionTiltDeg(), uprightTilt);
        if (fabsf(d) < 10) {
            server.send(400, "text/plain", "Turn it further (at least 20 degrees), then tap Step 2 again");
            return;
        }
        settings.levelRef = int16_t(lroundf(uprightTilt * 10));
        settings.levelSign = d > 0 ? 1 : -1;
        uprightTilt = NAN;
        saveSettings();
        server.send(200, "text/plain", "Motion sensor set up. Once it's fitted, park on level ground and tap Level now.");
    } else if (what == "level") {
        if (!settings.levelSign) {
            server.send(400, "text/plain", "Set up the motion sensor first (steps 1 and 2)");
            return;
        }
        if (motionMoving()) {
            server.send(400, "text/plain", "Wait until the car is still");
            return;
        }
        // How far the badge is turned clockwise from upright; turn the design back.
        const float tilt = settings.levelSign * angleDiff(motionTiltDeg(), settings.levelRef / 10.0f);
        settings.angle = constrain(lroundf(-tilt), -180, 180);
        lastActivity = millis();
        refreshDisplay(false);
        saveSettings();
        server.send(200, "text/plain", "Levelled: design turned " + String(settings.angle) + " degrees");
    } else {
        server.send(400, "text/plain", "Unknown action");
    }
}

// Changing the panel type needs a restart because the panel is set up once at boot.
static void handlePanel() {
    long v;
    if (!argInt("panel", 0, PANEL_TYPE_COUNT - 1, v)) {
        server.send(400, "text/plain", "Missing panel");
        return;
    }
    settings.panel = v;
    saveSettings();
    server.send(200, "text/plain", String("Using \"") + PANEL_TYPES[v].name + "\". Restarting...");
    restartAt = millis() + 800;
}

static void handleWifi() {
    argText("apSsid", settings.apSsid, sizeof(settings.apSsid));
    if (server.hasArg("apPass")) {
        String p = server.arg("apPass");
        if (p.length() != 0 && p.length() < 8) {
            server.send(400, "text/plain", "Wi-Fi password must be at least 8 characters (or empty for an open network)");
            return;
        }
        strlcpy(settings.apPass, p.c_str(), sizeof(settings.apPass));
    }
    argText("staSsid", settings.staSsid, sizeof(settings.staSsid));
    argText("staPass", settings.staPass, sizeof(settings.staPass));
    if (!settings.apSsid[0]) strcpy(settings.apSsid, "BMW-Emblem");
    saveSettings();
    server.send(200, "text/plain", "Saved. Restarting...");
    restartAt = millis() + 800;
}

static void handleOtaUpload() {
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        Update.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        Update.end(true);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
    }
}

static void handleOtaDone() {
    if (Update.hasError()) {
        server.send(500, "text/plain", String("Update failed: ") + Update.errorString());
        return;
    }
    server.send(200, "text/plain", "Update OK. Restarting...");
    restartAt = millis() + 800;
}

static void handleRoot() {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", reinterpret_cast<const char *>(INDEX_HTML_GZ), INDEX_HTML_GZ_LEN);
}

// HEVC decoder for the page (only fetched when a phone video needs it).
static void sendGz(const char *type, const uint8_t *data, size_t len) {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, type, reinterpret_cast<const char *>(data), len);
}

// Any unknown URL (including phones' "is there internet?" checks) goes to the
// control page, which makes the phone pop it up as a sign-in page.
static void handleNotFound() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
}

static void setupWeb() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/hevc.js", HTTP_GET, [] { sendGz("text/javascript", HEVC_JS_GZ, HEVC_JS_GZ_LEN); });
    server.on("/hevc.wasm", HTTP_GET, [] { sendGz("application/wasm", HEVC_WASM_GZ, HEVC_WASM_GZ_LEN); });
    server.on("/api/state", HTTP_GET, sendState);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/image", HTTP_POST, handleImageDone, [] { handleUpload(IMAGE_BYTES); });
    server.on("/api/anim", HTTP_POST, handleAnimDone, [] { handleUpload(animMaxBytes()); });
    server.on("/api/image/delete", HTTP_POST, handleImageDelete);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.on("/api/panel", HTTP_POST, handlePanel);
    server.on("/api/motion", HTTP_POST, handleMotion);
    server.on("/api/test", HTTP_POST, [] {
        server.send(200, "text/plain", "Showing red, green, blue, white");
        showTestColours();
    });
    server.on("/api/power/off", HTTP_POST, [] {
        server.send(200, "text/plain", "Turning off. Press the BOOT button or power-cycle to wake it.");
        shutdownAt = millis() + 600;
    });
    server.on("/api/reboot", HTTP_POST, [] {
        server.send(200, "text/plain", "Restarting...");
        restartAt = millis() + 500;
    });
    server.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
    server.onNotFound(handleNotFound);
    server.begin();
}

static void setupWifi() {
    WiFi.persistent(false);
    // Shows up as "emblem" in the phone's list of hotspot devices.
    WiFi.setHostname("emblem");
    // On the hotspot the emblem can also fetch the time from the internet.
    sntp_set_time_sync_notification_cb([](struct timeval *) { ntpSynced = true; });
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        logf("Joined \"%s\" as %s\n", settings.staSsid, WiFi.localIP().toString().c_str());
        configTime(0, 0, "pool.ntp.org", "time.google.com");
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.mode(settings.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(settings.apSsid, strlen(settings.apPass) >= 8 ? settings.apPass : nullptr);
    if (settings.staSsid[0]) WiFi.begin(settings.staSsid, settings.staPass);
    dns.start(53, "*", WiFi.softAPIP());
    if (MDNS.begin("emblem")) MDNS.addService("http", "tcp", 80);
    logf("Wi-Fi \"%s\" up, open http://%s or http://emblem.local\n", settings.apSsid,
                  WiFi.softAPIP().toString().c_str());
}

// ---------------------------------------------------------------------------

// Brings up the panel. Any failure is logged and leaves displayOk false, so
// Wi-Fi and the phone page keep working and the panel type can be changed.
static void setupDisplay() {
    logf("PSRAM: %s, %u bytes free\n", psramFound() ? "found" : "NOT FOUND", unsigned(ESP.getFreePsram()));
    if (!psramFound()) {
        logf("Display needs PSRAM; check board_build.arduino.memory_type in platformio.ini\n");
        return;
    }

#if LCD_BL_PIN >= 0
    ledcAttach(LCD_BL_PIN, 20000, 10);  // same PWM as Waveshare's demo
    ledcWrite(LCD_BL_PIN, 0);           // dark until the first frame
#endif

    if (!displayPowerOn()) logf("I/O expander (0x20) did not answer on I2C\n");
    const uint8_t panelIndex = settings.panel < PANEL_TYPE_COUNT ? settings.panel : 0;
    logf("Panel type %u: %s\n", panelIndex, PANEL_TYPES[panelIndex].name);
    int initResult = panelSendInit();
    displayDeselect();
    if (initResult != 0) {
        logf("Panel set-up over SPI failed (code %d)\n", initResult);
        return;
    }
    logf("Panel set-up commands sent\n");
    panel = createPanel(panelIndex);
    if (!panel) {
        logf("Display init failed (RGB panel)\n");
        return;
    }
    if (!setupFrameBuffers()) {
        logf("Display init failed (frame buffers)\n");
        return;
    }
    if (!renderer.begin(psramAlloc)) {
        logf("Out of PSRAM\n");
        return;
    }
    displayOk = true;
    applyBrightness();
    logf("Display ready\n");
}

// The card slot shares GPIO1/2 with the display's set-up bus, so this must
// run after setupDisplay(). Cards are only picked up at start-up.
static void setupSD() {
    Wire.begin(I2C_SDA, I2C_SCL);
    expanderSet(EXIO_SD_D3, true);  // card in SD (not SPI) mode
    delay(10);
    if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0) || !SD_MMC.begin("/sdcard", true) || SD_MMC.cardType() == CARD_NONE) {
        logf("No micro SD card, using internal flash for pictures\n");
        return;
    }
    sdOk = true;
    SD_MMC.mkdir("/emblem");
    logf("Micro SD card: %u MB, %u MB used\n", unsigned(SD_MMC.totalBytes() >> 20), unsigned(SD_MMC.usedBytes() >> 20));
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
#if LCD_BL_PIN >= 0
    gpio_hold_dis(gpio_num_t(LCD_BL_PIN));  // held low during deep sleep
    gpio_deep_sleep_hold_dis();
#endif
    loadSettings();
    checkLowVoltageWake();  // may go straight back to sleep
    sleepReason = SLEEP_NONE;
    delay(300);
    logf("\n=== Emblem starting ===\n");
    if (!LittleFS.begin(true)) logf("LittleFS mount failed\n");

    // Wi-Fi first: even if the display fails, the phone page stays reachable.
    setupWifi();
    setupWeb();
    setupDisplay();
    setupSD();
    Wire.setClock(400000);
    logf(rtcBegin() ? "Clock: set\n" : rtcPresent() ? "Clock: not set yet\n" : "Clock chip not found\n");
    logf(motionBegin() ? "Motion sensor found\n" : "Motion sensor not found\n");
    motionSetTapSensitivity(settings.doubleTap);
    nightNow = nightTime();
    applyBrightness();

    // Start-up: a picture slot's clip, or the built-in animation.
    if (displayOk && settings.startupAnim && settings.bootSlot) {
        uint8_t slots[IMAGE_SLOTS];
        listSlots(slots);
        if (slots[settings.bootSlot - 1]) {
            bootPlaying = true;
            bootStart = millis();
        }
    }
    if (displayOk && settings.startupAnim && !bootPlaying) renderer.startIntro(millis());
    updateCycleOn();
    refreshDisplay(false);
    lastActivity = showStart = cycleAt = millis();
    cycleAt += settings.cycleSec * 1000u;
}

void loop() {
    dns.processNextRequest();
    server.handleClient();

    static uint32_t lastFrame = 0;
    uint32_t now = millis();

    // Motion: speed-up from acceleration, double-tap for the next design.
    if (motionPresent()) {
        const float k = constrain((motionBoost() - 0.05f) / 0.35f, 0.0f, 1.0f);
        motionFactor = 1 + settings.motionReact / 100.0f * 2 * k;
        static uint32_t tapsSeen = motionDoubleTaps();
        const uint32_t taps = motionDoubleTaps();
        if (taps != tapsSeen) {
            tapsSeen = taps;
            if (settings.doubleTap && displayOk && !bootPlaying && !renderer.introRunning()) {
                logf("Double-tap: next design\n");
                lastActivity = now;
                nextItem();
            }
        }
    }
    renderer.setBoost(motionFactor);
    if (ntpSynced) {
        ntpSynced = false;
        rtcSaveSystemTime();
        logf("Clock set from the internet\n");
    }
    if (displayOk) {
        bootTick(now);
        cycleTick(now);
        stepAnimation(now);
    }
    static uint32_t lastResync = 0;
    if (displayOk && now - lastResync >= 1000) {
        lastResync = now;
        resyncDisplay();
    }
    if (displayOk && now - lastFrame >= FRAME_MS) {
        lastFrame = now;
        rampBrightness();
        Rect r = renderer.render(now);
        if (forceFull || fadeStart) {
            r = {0, 0, Renderer::W, Renderer::H};
            forceFull = false;
        }
        if (!r.empty()) present(r);
    }

    static uint32_t lastPowerTick = 0;
    if (now - lastPowerTick >= 1000) {
        lastPowerTick = now;
        powerTick(now);
    }
    if (shutdownAt && int32_t(now - shutdownAt) >= 0) shutdownNow(SLEEP_OFF);
    if (saveAt && int32_t(now - saveAt) >= 0) saveSettings();
    if (restartAt && int32_t(now - restartAt) >= 0) ESP.restart();
    delay(1);
}
#endif  // EMBLEM_DIAG
