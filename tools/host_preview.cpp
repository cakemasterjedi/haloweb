// Renders every mode on a PC so the artwork can be checked without the board:
//   g++ -O2 -std=c++17 -Isrc tools/host_preview.cpp src/renderer.cpp -o preview && ./preview out/
// Writes one PPM per mode into the given directory.
#include <stdio.h>
#include <stdlib.h>
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
    const char *names[] = {"roundel", "spin", "stripes", "solid", "image", "text"};
    for (int m = 0; m < MODE_COUNT; m++) {
        s.mode = m;
        r.apply(s);
        uint32_t t = 1000;
        r.render(t);
        for (int i = 0; i < 10; i++) r.render(t += 33);  // run animations a little
        save(r, dir + "/" + names[m] + ".ppm");
    }
    // Intro mid-spin, and a custom label.
    s.mode = MODE_ROUNDEL;
    strcpy(s.labelText, "M3 GTR");
    s.spacing = 4;
    s.quadA = 0x111111;
    s.quadB = 0x666666;
    r.apply(s);
    r.startIntro(5000);
    r.render(5000);
    r.render(5600);
    save(r, dir + "/intro_custom.ppm");
    s.mode = MODE_TEXT;
    strcpy(s.text, "ABCDEFGHI|JKLMNOPQR|STUVWXYZ");
    r.apply(s);
    r.render(9000);
    save(r, dir + "/font1.ppm");
    strcpy(s.text, "0123456789|-.!+/':?");
    r.apply(s);
    r.render(9100);
    save(r, dir + "/font2.ppm");
    return 0;
}
