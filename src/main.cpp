// Digital BMW-style emblem for the ESP32-S3 2.8" round display.
//
// The board starts its own Wi-Fi network ("BMW-Emblem" / "emblem123" by
// default). Join it from your phone and a control page opens automatically
// (or browse to http://192.168.4.1 or http://emblem.local). Optionally it can
// also join an existing network such as your phone's hotspot.
#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "display_config.h"
#include "renderer.h"
#include "settings.h"
#include "web_ui.h"  // generated from web/index.html by tools/embed_web.py

static const size_t IMAGE_BYTES = size_t(Renderer::W) * Renderer::H * 2;
static const uint32_t FRAME_MS = 20;
static const uint32_t SAVE_DELAY_MS = 2000;

static Arduino_RGB_Display *gfx;
static Renderer renderer;
static Settings settings;
static Preferences prefs;
static WebServer server(80);
static DNSServer dns;

static uint16_t *presentBuf;  // brightness-scaled copy of the region being sent to the panel
static uint8_t lut5[32], lut6[64];
static bool softDim = LCD_BL_PIN < 0;
static bool forceFull = true;
static uint32_t saveAt = 0;
static uint32_t restartAt = 0;

// ---------------------------------------------------------------------------
// Display output

static void *psramAlloc(size_t n) {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void applyBrightness() {
    uint8_t pct = constrain(settings.brightness, 5, 100);
#if LCD_BL_PIN >= 0
    ledcWrite(0, map(pct, 0, 100, 0, 255));
#endif
    for (int i = 0; i < 32; i++) lut5[i] = i * pct / 100;
    for (int i = 0; i < 64; i++) lut6[i] = i * pct / 100;
    forceFull = true;
}

static void present(Rect r) {
    const uint16_t *src = renderer.frame();
    const bool scale = softDim && settings.brightness < 100;
    if (!scale && r.x == 0 && r.w == Renderer::W) {
        // Rows are already contiguous in the frame buffer.
        gfx->draw16bitRGBBitmap(0, r.y, const_cast<uint16_t *>(src + r.y * Renderer::W), r.w, r.h);
        return;
    }
    uint16_t *dst = presentBuf;
    for (int y = r.y; y < r.y + r.h; y++) {
        const uint16_t *row = src + y * Renderer::W + r.x;
        if (!scale) {
            memcpy(dst, row, r.w * 2);
            dst += r.w;
            continue;
        }
        for (int x = 0; x < r.w; x++) {
            uint16_t p = row[x];
            *dst++ = (lut5[p >> 11] << 11) | (lut6[(p >> 5) & 63] << 5) | lut5[p & 31];
        }
    }
    gfx->draw16bitRGBBitmap(r.x, r.y, presentBuf, r.w, r.h);
}

// ---------------------------------------------------------------------------
// Storage

static String imagePath(int slot) {
    return "/img" + String(slot) + ".bin";
}

static void loadImage() {
    bool ok = false;
    File f = LittleFS.open(imagePath(settings.imageSlot), "r");
    if (f && f.size() == IMAGE_BYTES) {
        ok = f.read(reinterpret_cast<uint8_t *>(renderer.imageBuffer()), IMAGE_BYTES) == IMAGE_BYTES;
    }
    if (f) f.close();
    renderer.imageChanged(ok);
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
    j += ",\"slots\":[";
    for (int i = 0; i < IMAGE_SLOTS; i++) {
        if (i) j += ",";
        j += LittleFS.exists(imagePath(i)) ? "1" : "0";
    }
    j += "]}";
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
    renderer.apply(settings);
    if (oldMode != settings.mode && (settings.mode == MODE_ROUNDEL || settings.mode == MODE_SPIN)) {
        renderer.startIntro(millis());
    }
    saveAt = millis() + SAVE_DELAY_MS;
    sendState();
}

static File uploadFile;
static size_t uploadBytes;

static void handleImageUpload() {
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        uploadBytes = 0;
        uploadFile = LittleFS.open("/upload.tmp", "w");
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(up.buf, up.currentSize);
        uploadBytes += up.currentSize;
    } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) uploadFile.close();
    }
}

static void handleImageDone() {
    long slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= IMAGE_SLOTS || uploadBytes != IMAGE_BYTES) {
        LittleFS.remove("/upload.tmp");
        server.send(400, "text/plain", "Expected a 480x480 RGB565 image (" + String(IMAGE_BYTES) + " bytes)");
        return;
    }
    LittleFS.remove(imagePath(slot));
    if (!LittleFS.rename("/upload.tmp", imagePath(slot))) {
        server.send(500, "text/plain", "Could not save image (storage full?)");
        return;
    }
    settings.mode = MODE_IMAGE;
    settings.imageSlot = slot;
    loadImage();
    renderer.apply(settings);
    saveAt = millis() + SAVE_DELAY_MS;
    sendState();
}

static void handleImageDelete() {
    long slot = server.arg("slot").toInt();
    if (slot >= 0 && slot < IMAGE_SLOTS) LittleFS.remove(imagePath(slot));
    if (settings.mode == MODE_IMAGE && settings.imageSlot == slot) loadImage();
    sendState();
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
    server.on("/api/image", HTTP_POST, handleImageDone, handleImageUpload);
    server.on("/api/image/delete", HTTP_POST, handleImageDelete);
    server.on("/api/wifi", HTTP_POST, handleWifi);
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
    Serial.printf("Wi-Fi \"%s\" up, open http://%s or http://emblem.local\n", settings.apSsid,
                  WiFi.softAPIP().toString().c_str());
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    displayPowerOn();
    gfx = createDisplay();
    if (!gfx->begin()) Serial.println("Display init failed");
    gfx->fillScreen(RGB565_BLACK);

#if LCD_BL_PIN >= 0
    ledcSetup(0, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, 0);
#endif

    presentBuf = static_cast<uint16_t *>(psramAlloc(IMAGE_BYTES));
    if (!presentBuf || !renderer.begin(psramAlloc)) {
        Serial.println("Out of PSRAM");
        gfx->fillScreen(RGB565_RED);
        while (true) delay(1000);
    }

    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
    loadSettings();
    applyBrightness();
    if (settings.mode == MODE_IMAGE) loadImage();
    renderer.apply(settings);
    if (settings.mode == MODE_ROUNDEL || settings.mode == MODE_SPIN) renderer.startIntro(millis());

    setupWifi();
    setupWeb();
}

void loop() {
    dns.processNextRequest();
    server.handleClient();

    static uint32_t lastFrame = 0;
    uint32_t now = millis();
    if (now - lastFrame >= FRAME_MS) {
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
