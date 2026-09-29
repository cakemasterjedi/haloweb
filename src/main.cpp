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

#include "display_config.h"
#include "renderer.h"
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

static void applyBrightness() {
    if (!displayOk) return;
    uint8_t pct = constrain(settings.brightness, 5, 100);
#if LCD_BL_PIN >= 0
    ledcWrite(LCD_BL_PIN, map(pct, 0, 100, 0, 1023));
#endif
    for (int i = 0; i < 32; i++) lut5[i] = i * pct / 100;
    for (int i = 0; i < 64; i++) lut6[i] = i * pct / 100;
    forceFull = true;
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
static void present(Rect r) {
    if (framesShown++ == 0) logf("First emblem frame sent to the display\n");
    const Rect area = unite(r, lastDirty);
    lastDirty = r;
    const uint16_t *src = renderer.frame();
    uint16_t *dst = fbs[backFb];
    const bool scale = softDim && settings.brightness < 100;
    for (int y = area.y; y < area.y + area.h; y++) {
        const size_t off = size_t(y) * Renderer::W + area.x;
        if (!scale) {
            memcpy(dst + off, src + off, area.w * 2);
            continue;
        }
        for (int x = 0; x < area.w; x++) {
            uint16_t p = src[off + x];
            dst[off + x] = (lut5[p >> 11] << 11) | (lut6[(p >> 5) & 63] << 5) | lut5[p & 31];
        }
    }
    swapBuffers();
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

// Loads the picture or animation in the selected slot into the renderer.
static void loadImage() {
    if (!displayOk) return;
    freeAnimation();
    const int slot = settings.imageSlot;
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

// Shows the next animation frame when it's due.
static void stepAnimation(uint32_t now) {
    if (anim.count < 2 || settings.mode != MODE_IMAGE || int32_t(now - anim.nextAt) < 0) return;
    anim.index = (anim.index + 1) % anim.count;
    if (decodeFrame(anim.index)) renderer.imageChanged(true);
    resyncDisplay();  // each frame is read from flash / SD
    anim.nextAt = now + max<uint16_t>(anim.delay[anim.index], 20);
}

static void loadSettings() {
    settingsDefaults(settings);
    prefs.begin("emblem", false);
    Settings stored;
    if (prefs.getBytesLength("s") == sizeof(Settings) &&
        prefs.getBytes("s", &stored, sizeof(stored)) == sizeof(stored) &&
        stored.version == SETTINGS_VERSION) {
        settings = stored;
    }
}

static void saveSettings() {
    prefs.putBytes("s", &settings, sizeof(settings));
    saveAt = 0;
    resyncDisplay();
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
    j += ",\"solid\":\"" + colorHex(settings.solid) + "\"";
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

static void handleSet() {
    long v;
    const uint8_t oldMode = settings.mode;
    const uint8_t oldSlot = settings.imageSlot;
    const uint8_t oldBrightness = settings.brightness;

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
    argColor("solid", settings.solid);
    argColor("textBg", settings.textBg);
    argColor("textFg", settings.textFg);
    argText("labelText", settings.labelText, sizeof(settings.labelText));
    argText("text", settings.text, sizeof(settings.text));

    if (settings.mode == MODE_IMAGE && (oldMode != MODE_IMAGE || oldSlot != settings.imageSlot)) loadImage();
    if (settings.brightness != oldBrightness) applyBrightness();
    if (displayOk) renderer.apply(settings);
    if (displayOk && oldMode != settings.mode && (settings.mode == MODE_ROUNDEL || settings.mode == MODE_SPIN)) {
        renderer.startIntro(millis());
    }
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
    if (slot == settings.imageSlot) freeAnimation();
    media().remove(imagePath(slot));
    media().remove(animPath(slot));
    if (!media().rename(uploadPath(), path)) {
        server.send(500, "text/plain", "Could not save the file");
        return false;
    }
    settings.mode = MODE_IMAGE;
    settings.imageSlot = slot;
    loadImage();
    if (displayOk) renderer.apply(settings);
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
        if (slot == settings.imageSlot) freeAnimation();
        media().remove(imagePath(slot));
        media().remove(animPath(slot));
    }
    if (settings.mode == MODE_IMAGE && settings.imageSlot == slot) loadImage();
    sendState();
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

// Any unknown URL (including phones' "is there internet?" checks) goes to the
// control page, which makes the phone pop it up as a sign-in page.
static void handleNotFound() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
}

static void setupWeb() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/state", HTTP_GET, sendState);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/image", HTTP_POST, handleImageDone, [] { handleUpload(IMAGE_BYTES); });
    server.on("/api/anim", HTTP_POST, handleAnimDone, [] { handleUpload(animMaxBytes()); });
    server.on("/api/image/delete", HTTP_POST, handleImageDelete);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.on("/api/panel", HTTP_POST, handlePanel);
    server.on("/api/test", HTTP_POST, [] {
        server.send(200, "text/plain", "Showing red, green, blue, white");
        showTestColours();
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
    renderer.apply(settings);
    if (settings.mode == MODE_ROUNDEL || settings.mode == MODE_SPIN) renderer.startIntro(millis());
    logf("Display ready\n");
    showTestColours();
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
    delay(1500);  // give the USB serial port time to appear so the log isn't lost
    logf("\n=== Emblem starting ===\n");

    if (!LittleFS.begin(true)) logf("LittleFS mount failed\n");
    loadSettings();

    // Wi-Fi first: even if the display fails, the phone page stays reachable.
    setupWifi();
    setupWeb();
    setupDisplay();
    setupSD();
    if (settings.mode == MODE_IMAGE) loadImage();
}

void loop() {
    dns.processNextRequest();
    server.handleClient();

    static uint32_t lastFrame = 0;
    uint32_t now = millis();
    if (displayOk) stepAnimation(now);
    static uint32_t lastResync = 0;
    if (displayOk && now - lastResync >= 1000) {
        lastResync = now;
        resyncDisplay();
    }
    if (displayOk && now - lastFrame >= FRAME_MS) {
        lastFrame = now;
        Rect r = renderer.render(now);
        if (forceFull) {
            r = {0, 0, Renderer::W, Renderer::H};
            forceFull = false;
        }
        if (!r.empty()) present(r);
    }

    if (saveAt && int32_t(now - saveAt) >= 0) saveSettings();
    if (restartAt && int32_t(now - restartAt) >= 0) ESP.restart();
    delay(1);
}
#endif  // EMBLEM_DIAG
