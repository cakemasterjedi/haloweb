// Renders every mode on a PC so the artwork can be checked without the board:
//   g++ -O2 -std=c++17 -Isrc tools/host_preview.cpp src/renderer.cpp -o preview && ./preview out/
// Writes one PPM per mode (and some start-up animation frames) into the given directory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "renderer.h"

static void save(const Renderer &r, const std::string &path) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", Renderer::W, Renderer::H);
    for (int i = 0; i < Renderer::W * Renderer::H; i++) {
        uint16_t p = r.frame()[i];
        unsigned char rgb[3] = {uint8_t((p >> 11) << 3), uint8_t(((p >> 5) & 63) << 2), uint8_t((p & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    static Renderer r;
    if (!r.begin(malloc)) return 1;
    Settings s;
    settingsDefaults(s);
    const char *names[] = {"roundel", "spin", "stripes", "image", "text"};
    for (int m = 0; m < MODE_COUNT; m++) {
        s.mode = m;
        r.apply(s);
        uint32_t t = 1000;
        r.render(t);
        for (int i = 0; i < 10; i++) r.render(t += 33);  // run animations a little
        save(r, dir + "/" + names[m] + ".ppm");
    }
    s.mode = MODE_STRIPES;
    s.speed = 0;
    r.apply(s);
    r.render(5000);
    save(r, dir + "/stripes_static.ppm");

    // Start-up animation frames.
    s.mode = MODE_ROUNDEL;
    r.apply(s);
    const int frames[] = {300, 700, 1100, 1500, 2000, 2400};
    for (int f : frames) {
        // The animation keeps its own clock, so step through it frame by frame.
        r.render(9000);
        r.startIntro(9000);
        uint32_t ms = 10000;
        for (int t = 0; t < f; t += 50) r.render(ms += 50);
        r.render(ms += 1);
        save(r, dir + "/intro_" + std::to_string(f) + ".ppm");
    }

    // Rotation (auto-level) applies to pictures and text too.
    uint16_t *img = r.imageBuffer();
    for (int y = 0; y < Renderer::H; y++) {
        for (int x = 0; x < Renderer::W; x++) {
            const bool bar = (x / 40 + y / 40) % 2 == 0;
            img[y * Renderer::W + x] = y < 60 ? 0xF800 : (bar ? 0xFFFF : 0x001F);
        }
    }
    r.imageChanged(true);
    s.angle = 20;
    s.mode = MODE_IMAGE;
    r.apply(s);
    r.render(20000);
    save(r, dir + "/image_rotated.ppm");
    s.mode = MODE_TEXT;
    r.apply(s);
    r.render(20100);
    save(r, dir + "/text_rotated.ppm");
    s.angle = 0;

    r.showMessage("LOW|BATTERY", 0xE22718);
    save(r, dir + "/message.ppm");
    return 0;
}
