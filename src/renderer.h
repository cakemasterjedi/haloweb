#pragma once
// Draws the emblem into an RGB565 frame buffer. Plain C++ with no Arduino
// dependencies so it can also be built and checked on a PC (see test/).
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

struct Rect {
    int16_t x, y, w, h;
    bool empty() const { return w <= 0 || h <= 0; }
};

class Renderer {
public:
    static const int W = 480;
    static const int H = 480;
    typedef void *(*AllocFn)(size_t);

    // alloc is used for the large buffers (PSRAM on the ESP32).
    bool begin(AllocFn alloc);

    // Call whenever settings change; the next render() redraws everything.
    void apply(const Settings &s);

    // Spin-up animation for the roundel modes.
    void startIntro(uint32_t ms);

    // Picture for MODE_IMAGE: fill imageBuffer() (W*H RGB565) then call imageChanged().
    uint16_t *imageBuffer() { return image_; }
    void imageChanged(bool valid);

    // Draws the frame for time ms. Returns the part of frame() that changed
    // (empty when nothing did).
    Rect render(uint32_t ms);
    const uint16_t *frame() const { return frame_; }

private:
    struct Seg {
        float x0, y0, x1, y1;
    };
    static const int MAX_SEGS = 512;
    static const int MAX_GLYPHS = 40;

    struct TextLayout {
        Seg segs[MAX_SEGS];
        int segCount;
        int glyphCount;
        int16_t glyphFirst[MAX_GLYPHS];
        int16_t glyphLast[MAX_GLYPHS];
        float glyphX0[MAX_GLYPHS];
        float glyphX1[MAX_GLYPHS];
        float width;
    };

    static void layoutText(TextLayout &t, const char *text, int len, float tracking);
    static float distToLayout(const TextLayout &t, float u, float v);

    void buildRoundelLayer();
    void drawDisc(float phi);
    void drawStripes(float offset, bool repeat);
    void drawSolid(float level);
    void drawText();
    void drawImage();

    bool introActive(uint32_t ms) const { return introStart_ && ms - introStart_ < INTRO_MS; }
    float introAngle(uint32_t ms) const;

    static const uint32_t INTRO_MS = 2200;

    Settings s_;
    uint16_t *frame_ = nullptr;
    uint16_t *layer_ = nullptr;
    uint16_t *image_ = nullptr;
    uint8_t *mask_ = nullptr;
    bool imageValid_ = false;
    bool dirty_ = true;
    uint32_t lastMs_ = 0;
    uint32_t introStart_ = 0;
    float phase_ = 0;  // radians (spin) or pixels (stripes) or radians (pulse)
    TextLayout text_;
};
