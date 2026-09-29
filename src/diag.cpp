// Hardware check, built only by the "diag" environments in platformio.ini:
//   pio run -e diag -t upload && pio device monitor -e diag
// It skips the display entirely, starts an open Wi-Fi network "EMBLEM-TEST"
// and prints what it finds to both serial outputs (native USB and UART0), so
// it works whichever USB-C port the cable is in.
#ifdef EMBLEM_DIAG
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_chip_info.h>

#include "display_config.h"

static WebServer server(80);
static String scanResult;

static void logBoth(const String &s) {
    Serial.println(s);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.println(s);
#endif
}

static const char *resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "CRASH (panic)";
        case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
        case ESP_RST_TASK_WDT: return "CRASH (task watchdog)";
        case ESP_RST_WDT: return "CRASH (watchdog)";
        case ESP_RST_BROWNOUT: return "BROWNOUT (power supply too weak)";
        default: return "other";
    }
}

static String i2cScan() {
    String out;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "0x%02X ", a);
            out += buf;
        }
    }
    return out.length() ? out : String("nothing found");
}

static String report() {
    String r;
    r += "Chip: " + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    r += "Last reset: " + String(resetReason()) + "\n";
    r += "Flash size: " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
    r += "PSRAM in use: " + String(psramFound() ? "yes, " + String(ESP.getPsramSize() / 1024) + " KB" : String("no")) + "\n";
    r += "Free heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    r += "I2C devices (SDA 15, SCL 7): " + scanResult + "\n";
    r += "Uptime: " + String(millis() / 1000) + " s\n";
    return r;
}

static void page() {
    String h = "<!doctype html><meta name=viewport content='width=device-width'><title>Emblem test</title>"
               "<body style='font:16px sans-serif;background:#111;color:#eee;padding:16px'>"
               "<h2>Emblem hardware test</h2><pre>" + report() + "</pre>"
               "<p><a style='color:#6af' href='/bl?on=1'>Backlight GPIO6 ON</a> &nbsp; "
               "<a style='color:#6af' href='/bl?on=0'>OFF</a></p>"
               "<p><a style='color:#6af' href='/'>Refresh</a></p>";
    server.send(200, "text/html", h);
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(115200);
#endif
    delay(2000);
    logBoth("\n=== Emblem hardware test ===");

    Wire.begin(I2C_SDA, I2C_SCL);
    scanResult = i2cScan();

    pinMode(6, OUTPUT);
    digitalWrite(6, HIGH);  // backlight on (screen stays black but may glow)

    WiFi.mode(WIFI_AP);
    bool ap = WiFi.softAP("EMBLEM-TEST");
    logBoth(String("Wi-Fi EMBLEM-TEST: ") + (ap ? "started, open http://192.168.4.1" : "FAILED"));

    server.on("/", page);
    server.on("/bl", [] {
        digitalWrite(6, server.arg("on") == "1" ? HIGH : LOW);
        server.sendHeader("Location", "/");
        server.send(302);
    });
    server.begin();
    logBoth(report());
}

void loop() {
    server.handleClient();
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        logBoth(report());
    }
    delay(2);
}
#endif
