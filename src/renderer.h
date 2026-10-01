#pragma once
// Draws the emblem into an RGB565 frame buffer. Plain C++ with no Arduino
// dependencies so it can also be built and checked on a PC (see tools/).
#include <stddef.h>
#include <stdint.h>
#include "settings.h"

struct LabelGlyph;

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

    // Start-up animation: the ring sweeps in, the quarters spin into place,
    // a glint crosses the badge, then the selected mode takes over.
    void startIntro(uint32_t ms);
    bool introRunning() const { return introT_ >= 0; }

    // Picture for MODE_IMAGE: fill imageBuffer() (W*H RGB565) then call imageChanged().
    uint16_t *imageBuffer() { return image_; }
    void imageChanged(bool valid);

    // Draws the frame for time ms. Returns the part of frame() that changed
    // (empty when nothing did).
    Rect render(uint32_t ms);
    const uint16_t *frame() const { return frame_; }

    // Speeds up the moving modes (spin, stripes) by this factor, e.g. when the
    // car accelerates. 1 = normal.
    void setBoost(float boost) { boost_ = boost; }

    // Draws a full-screen message (e.g. before shutting down) straight away.
    // The next render() goes back to the selected mode.
    Rect showMessage(const char *text, uint32_t fg);

private:
    struct Seg {
        float x0, y0, x1, y1;
    };
    static const int MAX_SEGS = 512;
    static const int MAX_GLYPHS = 40;
    static const int DISC = 304;  // side of the disc shading images
    static const int MAX_LABEL = 16;

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

    enum Layer : uint8_t { LAYER_NONE, LAYER_ROUNDEL, LAYER_CARBON };

    // Ring lettering: upright bold letters spread evenly around the top.
    struct LabelLayout {
        int count;
        const LabelGlyph *glyph[MAX_LABEL];
        float step;      // angle between letter centres, radians
        float scale;     // screen pixels per font pixel
        float baseline;  // radius of the baseline
    };

    static void layoutText(TextLayout &t, const char *text, int len, float tracking);

    void buildTables();
    void layoutLabel();
    float labelCoverage(float fx, float fy, float r, float offset, float &height) const;
    void buildRoundelLayer();
    void buildCarbonLayer();
    void drawDisc(float phi);
    void drawStripes(float offset, bool repeat);
    void drawText();
    void drawImage();
    void drawIntro(float t, bool intoRoundel);
    void rotateInto(uint16_t *dst, const uint16_t *src, float degrees);

    Settings s_;
    uint16_t *frame_ = nullptr;
    uint16_t *layer_ = nullptr;   // static background: roundel or carbon fibre
    uint16_t *image_ = nullptr;
    uint16_t *discA_ = nullptr;   // shaded disc in quarter colour A / B
    uint16_t *discB_ = nullptr;
    uint16_t *angle_ = nullptr;   // per-pixel angle, clockwise from the top (0..65535)
    uint8_t *vignette_ = nullptr; // per-pixel edge darkening (255 = none)
    uint8_t *mask_ = nullptr;     // text coverage
    uint8_t *shadow_ = nullptr;   // text shadow coverage
    Layer layer = LAYER_NONE;
    bool imageValid_ = false;
    bool dirty_ = true;
    uint32_t lastMs_ = 0;
    float introT_ = -1;  // seconds into the start-up animation, -1 = not playing
    float phase_ = 0;  // radians (spin) or pixels (stripes)
    float boost_ = 1;
    uint16_t divider_ = 0;  // lines between the quarters
    LabelLayout label_;
};
