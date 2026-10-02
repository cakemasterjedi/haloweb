#include "renderer.h"

#include <initializer_list>
#include <math.h>
#include <string.h>

#include "label_font.h"
#include "stroke_font.h"

// On the board the start-up animation's drawing runs from internal RAM, so
// it doesn't wait on the flash cache.
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define INTRO_HOT IRAM_ATTR
#else
#define INTRO_HOT
#endif

namespace {

const float PI_F = 3.14159265f;
const float CX = Renderer::W / 2.0f;
const float CY = Renderer::H / 2.0f;

// Roundel geometry (pixels from the centre), after the classic badge: thin
// silver outer rim, wide black ring, thin silver inner rim, quartered disc.
const float R_EDGE = 239.5f;       // outer edge of the outer rim
const float R_RING = 231.0f;       // outer edge of the black ring
const float R_INNER_RIM = 158.0f;  // outer edge of the inner rim
const float R_DISC = 151.0f;       // quarters
const float LABEL_MID = 194.0f;    // radius of the middle of the ring lettering
const float LABEL_CAP = 55.0f;     // cap height of the ring lettering, pixels
const float DIVIDER = 0.8f;        // half width of the lines between the quarters
const float STROKE = 1.35f;        // text mode stroke thickness in font units

// Light comes from the upper left, slightly towards the viewer.
const float LX = -0.48f, LY = -0.62f, LZ = 0.62f;

struct RGBf {
    float r, g, b;
};

const RGBf BLACK = {0, 0, 0};
const RGBf WHITE = {255, 255, 255};

RGBf rgbf(uint32_t c) {
    return {float((c >> 16) & 0xFF), float((c >> 8) & 0xFF), float(c & 0xFF)};
}

RGBf mix(const RGBf &a, const RGBf &b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

RGBf scale(const RGBf &a, float f) {
    return {a.r * f, a.g * f, a.b * f};
}

RGBf add(const RGBf &a, float v) {
    return {a.r + v, a.g + v, a.b + v};
}

inline float clamp01(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

inline float smoothstep(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}

inline uint8_t clamp255(float v) {
    return v < 0 ? 0 : (v > 255 ? 255 : uint8_t(v + 0.5f));
}

// 4x4 ordered dither so smooth gradients don't band in 16-bit colour.
const int8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

uint16_t to565d(const RGBf &c, int x, int y) {
    float d = (BAYER[y & 3][x & 3] - 7.5f) / 16.0f;
    uint8_t r = clamp255(c.r + d * 8), g = clamp255(c.g + d * 4), b = clamp255(c.b + d * 8);
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t to565(const RGBf &c) {
    uint8_t r = clamp255(c.r), g = clamp255(c.g), b = clamp255(c.b);
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Blend two RGB565 colours, t = 0..256 (weight of b).
inline uint16_t blend565(uint16_t a, uint16_t b, int t) {
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + (((br - ar) * t) >> 8);
    int g = ag + (((bg - ag) * t) >> 8);
    int bl = ab + (((bb - ab) * t) >> 8);
    return uint16_t((r << 11) | (g << 5) | bl);
}

// Scale an RGB565 colour by f/256.
inline uint16_t scale565(uint16_t c, int f) {
    int r = ((c >> 11) * f) >> 8, g = (((c >> 5) & 63) * f) >> 8, b = ((c & 31) * f) >> 8;
    return uint16_t((r << 11) | (g << 5) | b);
}

// Lighten an RGB565 colour towards white by a/256.
inline uint16_t lighten565(uint16_t c, int a) {
    return blend565(c, 0xFFFF, a);
}

// Fraction of a pixel at radius r that lies inside a circle of radius edge.
inline float inside(float edge, float r) {
    return clamp01(edge - r + 0.5f);
}

float segDist2(float px, float py, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float t = 0;
    if (len2 > 0) {
        t = ((px - x0) * dx + (py - y0) * dy) / len2;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
    }
    float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy);
    return ex * ex + ey * ey;
}

// Polished metal rim between radii r0 and r1: a rounded (torus) profile lit
// from the upper left, with a sharp specular highlight and a faint
// environment reflection so it reads as chrome rather than grey.
// Done on a ColourMix (how much rim colour and white), so it is worked out
// once for any rim colour.
struct ColourMix {
    float rim, ring, disc, white;  // white: added to all channels, 0..255
};
const ColourMix MIX_BLACK = {0, 0, 0, 0};
const ColourMix MIX_WHITE = {0, 0, 0, 255};
const ColourMix MIX_RIM = {1, 0, 0, 0};
const ColourMix MIX_RING = {0, 1, 0, 0};
const ColourMix MIX_DISC = {0, 0, 1, 0};  // half way between the quarter colours

ColourMix mix(const ColourMix &a, const ColourMix &b, float t) {
    return {a.rim + (b.rim - a.rim) * t, a.ring + (b.ring - a.ring) * t, a.disc + (b.disc - a.disc) * t,
            a.white + (b.white - a.white) * t};
}

ColourMix scale(const ColourMix &a, float f) {
    return {a.rim * f, a.ring * f, a.disc * f, a.white * f};
}

ColourMix add(const ColourMix &a, float v) {
    return {a.rim, a.ring, a.disc, a.white + v};
}

// Stored as bytes: rim * 200 (it can go a little over 1), the others * 255,
// white as is.
const float RIM_STEPS = 200.0f;

ColourMix chrome(const ColourMix &base, float fx, float fy, float r, float r0, float r1) {
    float t = clamp01((r - r0) / (r1 - r0));
    float slope = cosf(PI_F * t);  // +1 at the inner edge, -1 at the outer edge
    float nx = fx / r, ny = fy / r;
    float Nx = -slope * nx * 0.9f, Ny = -slope * ny * 0.9f;
    float inv = 1.0f / sqrtf(Nx * Nx + Ny * Ny + 1);
    float diff = (Nx * LX + Ny * LY + LZ) * inv;
    float d = diff > 0 ? diff : 0;
    float d2 = d * d, d4 = d2 * d2, d8 = d4 * d4, d16 = d8 * d8;
    float spec = d16 * d8 * d4;  // d^28: tight specular highlight
    float env = 0.5f + 0.5f * sinf((Ny * inv) * 5.0f + 0.6f);  // sky/ground banding
    ColourMix c = scale(base, 0.16f + 0.78f * d + 0.28f * env);
    return mix(c, MIX_WHITE, spec * 0.9f);
}

inline float easeOutCubic(float t) {
    t = clamp01(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

inline float easeInOutCubic(float t) {
    t = clamp01(t);
    return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

inline float easeOutBack(float t) {
    t = clamp01(t);
    const float c1 = 1.4f, c3 = c1 + 1;
    return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

}  // namespace

bool Renderer::begin(AllocFn alloc) {
    const size_t px = size_t(W) * H;
    frame_ = static_cast<uint16_t *>(alloc(px * 2));
    layer_ = static_cast<uint16_t *>(alloc(px * 2));
    image_ = static_cast<uint16_t *>(alloc(px * 2));
    angle_ = static_cast<uint16_t *>(alloc(px * 2));
    discA_ = static_cast<uint16_t *>(alloc(DISC * DISC * 2));
    discB_ = static_cast<uint16_t *>(alloc(DISC * DISC * 2));
    vignette_ = static_cast<uint8_t *>(alloc(px));
    mask_ = static_cast<uint8_t *>(alloc(px));
    shadow_ = static_cast<uint8_t *>(alloc(px));
    carbon_ = static_cast<uint16_t *>(alloc(px * 2));
    roundelMix_ = static_cast<uint8_t *>(alloc(size_t(roundelMixCount()) * 4));
    discMix_ = static_cast<uint8_t *>(alloc(DISC * DISC * 2));
    if (!frame_ || !layer_ || !image_ || !angle_ || !discA_ || !discB_ || !vignette_ || !mask_ || !shadow_ ||
        !carbon_ || !roundelMix_ || !discMix_) {
        return false;
    }
    memset(frame_, 0, px * 2);
    memset(image_, 0, px * 2);
    buildTables();
    settingsDefaults(s_);
    dirty_ = true;
    return true;
}

// Per-pixel angle (for the start-up sweep) and vignette, computed once.
void Renderer::buildTables() {
    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            float a = atan2f(fx, -fy);  // clockwise from the top, -pi..pi
            if (a < 0) a += 2 * PI_F;
            angle_[y * W + x] = uint16_t(a / (2 * PI_F) * 65535.0f);
            float rr = (fx * fx + fy * fy) / (240.0f * 240.0f);
            vignette_[y * W + x] = clamp255(255 * (1.0f - 0.42f * rr * rr));
        }
    }
}

void Renderer::apply(const Settings &s) {
    // Only rebuild what the change affects (the roundel takes a while).
    if (s.angle != s_.angle || s.spacing != s_.spacing || strcmp(s.labelText, s_.labelText) != 0 ||
        s.rim != s_.rim || s.ring != s_.ring || s.label != s_.label) {
        roundelValid_ = false;
    }
    if (s.quadA != s_.quadA || s.quadB != s_.quadB) roundelValid_ = discValid_ = false;
    if (s.stripeBg != s_.stripeBg) carbonValid_ = false;
    s_ = s;
    dirty_ = true;
}

void Renderer::startIntro(uint32_t) {
    if (!roundelValid_) buildRoundelLayer();  // before the clock starts
    introT_ = 0;
    introClear_ = 2;
    introRegionsPrev_ = {};
}

void Renderer::imageChanged(bool valid) {
    imageValid_ = valid;
    if (s_.mode == MODE_IMAGE) dirty_ = true;
}

// ---------------------------------------------------------------------------
// Text

void Renderer::layoutText(TextLayout &t, const char *text, int len, float tracking) {
    t.segCount = 0;
    t.glyphCount = 0;
    float x = 0;
    for (int i = 0; i < len && t.glyphCount < MAX_GLYPHS; i++) {
        const StrokeGlyph *g = strokeGlyph(text[i]);
        int gi = t.glyphCount++;
        t.glyphFirst[gi] = t.segCount;
        t.glyphX0[gi] = x;
        t.glyphX1[gi] = x + g->width;

        const char *p = g->strokes;
        bool havePrev = false;
        float px = 0, py = 0;
        while (*p) {
            if (*p == ' ') {
                havePrev = false;
                p++;
                continue;
            }
            if (!p[1]) break;
            float nx = x + (p[0] - '0');
            float ny = p[1] - '0';
            p += 2;
            bool single = (*p == ' ' || *p == 0) && !havePrev;
            if ((havePrev || single) && t.segCount < MAX_SEGS) {
                Seg &s = t.segs[t.segCount++];
                if (single) {
                    s = {nx, ny, nx, ny};
                } else {
                    s = {px, py, nx, ny};
                }
            }
            px = nx;
            py = ny;
            havePrev = true;
        }
        t.glyphLast[gi] = t.segCount;
        x += g->width + tracking;
    }
    t.width = t.glyphCount ? x - tracking : 0;
}

// ---------------------------------------------------------------------------
// Roundel

// Letters are spread evenly around the top of the ring, each one upright
// (pointing out from the centre). The spacing setting sets the angle between
// them; long labels get closer together and then smaller so they still fit.
void Renderer::layoutLabel() {
    LabelLayout &l = label_;
    l.count = 0;
    float maxAdv = 1;
    for (const char *p = s_.labelText; *p && l.count < MAX_LABEL; p++) {
        const LabelGlyph *g = labelGlyph(*p);
        l.glyph[l.count++] = g;
        if (g->advance > maxAdv) maxAdv = g->advance;
    }
    const float deg = PI_F / 180;
    float sc = LABEL_CAP / LABEL_FONT_CAP;
    const float minStep = maxAdv * sc / LABEL_MID + 2 * deg;  // widest letter + a gap
    float step = (22 + s_.spacing * 2.2f) * deg;
    if (step < minStep) step = minStep;
    if (l.count > 1 && (l.count - 1) * step > 300 * deg) step = 300 * deg / (l.count - 1);
    if (step < minStep) sc *= (step - 2 * deg) / (minStep - 2 * deg);
    l.step = step;
    l.scale = sc;
    l.baseline = LABEL_MID - LABEL_FONT_CAP * sc / 2;
}

// Coverage (0..1) of the ring lettering at a pixel; height is how far up the
// letter it is (0 at the baseline, 1 at the cap height).
float Renderer::labelCoverage(int idx, float r, float offset, float &height) const {
    const LabelLayout &l = label_;
    if (!l.count) return 0;
    // Clockwise from the top, from the angle table (atan2 is slow on the ESP32).
    float a = angle_[idx] * (2 * PI_F / 65535.0f) - offset;
    while (a > PI_F) a -= 2 * PI_F;
    while (a < -PI_F) a += 2 * PI_F;
    const float first = -(l.count - 1) * l.step / 2;
    int i = int(floorf((a - first) / l.step + 0.5f));
    if (i < 0) i = 0;
    if (i >= l.count) i = l.count - 1;
    const LabelGlyph *g = l.glyph[i];
    if (!g->w) return 0;
    const float da = a - (first + i * l.step);
    // Most of the band is the gap between letters: rule it out before the
    // (slow on the ESP32) trig. |u| = r|sin da| / scale >= r|da|(1 - da^2/6) / scale.
    const float reach = fmaxf(fabsf(g->left - g->advance * 0.5f), fabsf(g->left + g->w - g->advance * 0.5f)) + 2;
    const float ada = fabsf(da);
    if (ada > 1.5f || r * ada * (1 - ada * ada / 6) / l.scale > reach) return 0;
    const float u = r * sinf(da) / l.scale;                // across the letter
    const float v = (r * cosf(da) - l.baseline) / l.scale;  // up from the baseline
    height = v / LABEL_FONT_CAP;
    // Bilinear sample of the glyph bitmap.
    const float gx = u + g->advance * 0.5f - g->left - 0.5f;
    const float gy = LABEL_FONT_TOP + LABEL_FONT_CAP - v - 0.5f;
    const int x0 = int(floorf(gx)), y0 = int(floorf(gy));
    if (x0 < -1 || y0 < -1 || x0 >= g->w || y0 >= LABEL_FONT_H) return 0;
    const float tx = gx - x0, ty = gy - y0;
    const uint8_t *bits = LABEL_FONT_BITS + g->offset;
    auto px = [&](int x, int y) -> float {
        return (x < 0 || y < 0 || x >= g->w || y >= LABEL_FONT_H) ? 0 : bits[y * g->w + x];
    };
    float top = px(x0, y0) + (px(x0 + 1, y0) - px(x0, y0)) * tx;
    float bot = px(x0, y0 + 1) + (px(x0 + 1, y0 + 1) - px(x0, y0 + 1)) * tx;
    return (top + (bot - top) * ty) / 255.0f;
}

// Pixels of the roundel with a stored colour mix: inside the outer edge,
// outside the plain middle of the disc (which is just the disc colour).
const float MIX_OUT2 = (R_EDGE + 1) * (R_EDGE + 1);
const float MIX_IN2 = (R_DISC - 0.5f) * (R_DISC - 0.5f);

int Renderer::roundelMixCount() {
    int n = 0;
    for (int y = 0; y < H; y++) {
        const float fy = y + 0.5f - CY;
        for (int x = 0; x < W; x++) {
            const float fx = x + 0.5f - CX;
            const float r2 = fx * fx + fy * fy;
            n += r2 <= MIX_OUT2 && r2 >= MIX_IN2;
        }
    }
    return n;
}

static uint8_t mixByte(float v) {
    return v <= 0 ? 0 : (v >= 255 ? 255 : uint8_t(v + 0.5f));
}

// Glossy black ring: a soft reflection over the top half, slightly darker
// next to the rims. The ring colour is scaled by shade, then gloss is added.
static void ringShading(float fx, float fy, float r, float &shade, float &gloss) {
    const float across = (r - R_INNER_RIM) / (R_RING - R_INNER_RIM);
    shade = 1 - 0.3f * (smoothstep(0.8f, 1.0f, across) + smoothstep(0.2f, 0.0f, across));
    const float top = clamp01(-fy / r);
    const float lit = clamp01((fx * LX + fy * LY) / r);
    float g = 0.55f * top * top + 0.45f * lit * lit * lit;
    g *= 1 - fabsf(across - 0.5f) * 1.4f;
    gloss = 38.0f * clamp01(g) * shade;
}

// How much of the ring shows at radius r (the rest is rims and the disc).
static float ringWeight(float r) {
    if (r >= R_RING + 1) return 0;
    float w = inside(R_RING, r);
    if (r < R_INNER_RIM + 1) w *= 1 - inside(R_INNER_RIM, r);
    if (r < R_DISC + 1) w *= 1 - inside(R_DISC, r);
    return w;
}

// The roundel's shape and shading without the lettering, which don't depend
// on any setting: per pixel, how much of each colour it takes. Worked out
// once; the lettering and colours are added in buildRoundelLayer().
void Renderer::buildRoundelGeometry() {
    uint8_t *m = roundelMix_;
    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            const float r2 = fx * fx + fy * fy;
            if (r2 > MIX_OUT2 || r2 < MIX_IN2) continue;
            float r = sqrtf(r2);
            ColourMix c = MIX_BLACK;
            if (r < R_EDGE + 1) {
                c = mix(c, chrome(MIX_RIM, fx, fy, r, R_RING - 1, R_EDGE), inside(R_EDGE, r));
            }
            if (r < R_RING + 1) {
                float shade, gloss;
                ringShading(fx, fy, r, shade, gloss);
                c = mix(c, add(scale(MIX_RING, shade), gloss), inside(R_RING, r));
            }
            if (r < R_INNER_RIM + 1) {
                c = mix(c, chrome(MIX_RIM, fx, fy, r, R_DISC - 1, R_INNER_RIM), inside(R_INNER_RIM, r));
            }
            if (r < R_DISC + 1) {
                c = mix(c, MIX_DISC, inside(R_DISC, r));
            }
            m[0] = mixByte(c.rim * RIM_STEPS);
            m[1] = mixByte(c.ring * 255);
            m[2] = mixByte(c.disc * 255);
            m[3] = mixByte(c.white);
            m += 4;
        }
    }

    // Quarters: lighter towards the top left, darker towards the bottom
    // right, with a soft gloss and a slightly darker edge:
    // colour * shade * (1 - gloss) + white * gloss.
    const int x0 = int(CX) - DISC / 2, y0 = int(CY) - DISC / 2;
    // The gloss is a Gaussian, exp(-(hx^2 + hy^2) / 2s^2) = gx(x) * gy(y).
    static float glossX[DISC], glossY[DISC];
    for (int i = 0; i < DISC; i++) {
        const float hx = x0 + i + 0.5f - CX + 55, hy = y0 + i + 0.5f - CY + 60;
        glossX[i] = expf(-(hx * hx) / (2 * 70.0f * 70.0f));
        glossY[i] = expf(-(hy * hy) / (2 * 70.0f * 70.0f));
    }
    for (int y = 0; y < DISC; y++) {
        float fy = y0 + y + 0.5f - CY;
        for (int x = 0; x < DISC; x++) {
            float fx = x0 + x + 0.5f - CX;
            float rr = sqrtf(fx * fx + fy * fy) / R_DISC;
            float t = clamp01(((fx + fy) * 0.7071f / R_DISC + 1) / 2);  // 0 top left .. 1 bottom right
            float shade = (1.0f - 0.2f * t) * (1.0f - 0.12f * smoothstep(0.85f, 1.0f, rr));
            float gloss = 0.14f * (1 - t) + glossX[x] * glossY[y] * 0.12f;
            discMix_[(y * DISC + x) * 2] = mixByte(shade * (1 - gloss) * 255);
            discMix_[(y * DISC + x) * 2 + 1] = mixByte(gloss * 255);
        }
    }
    geometryValid_ = true;
}

// The roundel in the current colours, with its lettering, from the stored
// mixes. Fast enough to follow the colour, rotation and letter settings live.
void Renderer::buildRoundelLayer() {
    if (!geometryValid_) buildRoundelGeometry();
    layoutLabel();
    const float offset = s_.angle * PI_F / 180.0f;
    const float labelIn = label_.baseline - 14 * label_.scale;
    const float labelOut = fminf(label_.baseline + (LABEL_FONT_CAP + LABEL_FONT_TOP + 1) * label_.scale, R_RING + 1);
    const float labelIn2 = labelIn * labelIn, labelOut2 = labelOut * labelOut;

    const RGBf ringC = rgbf(s_.ring), labelC = rgbf(s_.label);
    const RGBf rim = scale(rgbf(s_.rim), 1 / RIM_STEPS);
    const RGBf ring = scale(ringC, 1 / 255.0f);
    const RGBf qa = rgbf(s_.quadA), qb = rgbf(s_.quadB);
    const RGBf qmid = mix(qa, qb, 0.5f);
    const RGBf disc = scale(qmid, 1 / 255.0f);
    uint16_t plain[16];  // the middle of the disc, dithered
    for (int i = 0; i < 16; i++) plain[i] = to565d(qmid, i & 3, i >> 2);

    const uint8_t *m = roundelMix_;
    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        uint16_t *row = layer_ + y * W;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            const float r2 = fx * fx + fy * fy;
            if (r2 > MIX_OUT2) {
                row[x] = 0;
                continue;
            }
            if (r2 < MIX_IN2) {
                row[x] = plain[(y & 3) * 4 + (x & 3)];
                continue;
            }
            const float a = m[0], b = m[1], d = m[2], w = m[3];
            m += 4;
            RGBf px = {a * rim.r + b * ring.r + d * disc.r + w, a * rim.g + b * ring.g + d * disc.g + w,
                       a * rim.b + b * ring.b + d * disc.b + w};
            if (r2 > labelIn2 && r2 < labelOut2) {
                // Lettering over the ring: white letters, a touch greyer
                // towards the centre.
                const float r = sqrtf(r2);
                float h = 0;
                const float cov = labelCoverage(y * W + x, r, offset, h);
                if (cov > 0) {
                    float shade, gloss;
                    ringShading(fx, fy, r, shade, gloss);
                    const RGBf under = add(scale(ringC, shade), gloss);
                    const RGBf l = mix(scale(labelC, 0.84f), labelC, clamp01(0.25f + h));
                    const float k = cov * ringWeight(r);
                    px = {px.r + (l.r - under.r) * k, px.g + (l.g - under.g) * k, px.b + (l.b - under.b) * k};
                }
            }
            row[x] = to565d(px, x, y);
        }
    }

    if (!discValid_) {
        const RGBf qa255 = scale(qa, 1 / 255.0f), qb255 = scale(qb, 1 / 255.0f);
        for (int y = 0; y < DISC; y++) {
            const uint8_t *dm = discMix_ + y * DISC * 2;
            for (int x = 0; x < DISC; x++) {
                const float k = dm[x * 2], w = dm[x * 2 + 1];
                discA_[y * DISC + x] = to565d({qa255.r * k + w, qa255.g * k + w, qa255.b * k + w}, x, y);
                discB_[y * DISC + x] = to565d({qb255.r * k + w, qb255.g * k + w, qb255.b * k + w}, x, y);
            }
        }
        discValid_ = true;
    }
    divider_ = to565(scale(rgbf(s_.rim), 0.8f));
    roundelValid_ = true;
}

// Quarters, rotated clockwise by phi, with thin lines between them. Only
// touches pixels fully inside the disc.
void Renderer::drawDisc(float phi) {
    const float cs = cosf(phi), sn = sinf(phi);
    const float rr = R_DISC - 0.5f;
    const int x0 = int(CX) - DISC / 2, y0 = int(CY) - DISC / 2;

    for (int y = int(CY - R_DISC); y < int(CY + R_DISC); y++) {
        float fy = y + 0.5f - CY;
        float h2 = rr * rr - fy * fy;
        if (h2 <= 0) continue;
        float half = sqrtf(h2);
        int xs = int(ceilf(CX - 0.5f - half));
        int xe = int(floorf(CX - 0.5f + half));
        uint16_t *row = frame_ + y * W;
        const uint16_t *ra = discA_ + (y - y0) * DISC - x0;
        const uint16_t *rb = discB_ + (y - y0) * DISC - x0;
        for (int x = xs; x <= xe; x++) {
            float fx = x + 0.5f - CX;
            float u = fx * cs + fy * sn;
            float v = -fx * sn + fy * cs;
            bool a = (u * v) > 0;  // top-left / bottom-right
            float d = fminf(fabsf(u), fabsf(v));
            uint16_t c = a ? ra[x] : rb[x];
            if (d < DIVIDER + 0.5f) c = blend565(c, divider_, int(clamp01(DIVIDER + 0.5f - d) * 256));
            row[x] = c;
        }
    }
}

// ---------------------------------------------------------------------------
// Start-up animation

// One pixel of the start-up animation for the parameters in p.
INTRO_HOT uint16_t Renderer::introPixel(int x, int y, const IntroParams &p) const {
    const float fx = x + 0.5f - CX, fy = y + 0.5f - CY;
    const float r2 = fx * fx + fy * fy;
    if (r2 >= 240.0f * 240.0f) return 0;
    uint16_t c = 0;
    if (r2 >= p.rDisc2) {
        // Ring, letters and rims, revealed clockwise from the top.
        uint16_t base = layer_[y * W + x];
        if (r2 < p.rIn2 && p.discS <= 0) base = 0;  // disc edge pixels wait for the disc
        if (p.ringP >= 1) {
            c = base;
        } else {
            float af = angle_[y * W + x] * (1 / 65535.0f) - p.offFrac;
            af = af < 0 ? af + 1 : (af >= 1 ? af - 1 : af);
            const float vis = clamp01((p.ringP - af) * 60 + 0.5f);
            c = scale565(base, int(vis * 256));
            const float d = (af - p.ringP) * 70;
            if (d > -3 && d < 3 && r2 > p.rIn2) {
                const float e = expf(-d * d);
                c = lighten565(c, int(e * 220 * vis + e * 60));
            }
        }
    } else if (p.discS > 0) {
        // Quarters growing from the centre while spinning into place.
        const float sx = fx * p.invS, sy = fy * p.invS;
        if (sx * sx + sy * sy < p.rDisc2) {
            int ix = int(sx + CX) - p.dx0, iy = int(sy + CY) - p.dy0;
            ix = ix < 0 ? 0 : (ix >= DISC ? DISC - 1 : ix);
            iy = iy < 0 ? 0 : (iy >= DISC ? DISC - 1 : iy);
            const int i = iy * DISC + ix;
            const float u = sx * p.cs + sy * p.sn;
            const float v = -sx * p.sn + sy * p.cs;
            const bool a = (u * v) > 0;
            const float d = fminf(fabsf(u), fabsf(v)) * p.discS;
            c = a ? discA_[i] : discB_[i];
            if (d < DIVIDER + 0.5f) c = blend565(c, divider_, int(clamp01(DIVIDER + 0.5f - d) * 256));
        }
    }
    if (p.glint && c) {
        const float d = (fx + fy) * 0.7071f - p.glintC;
        if (d > -60 && d < 60) {
            const float g = 1 - (d / 60) * (d / 60);
            c = lighten565(c, int(g * g * 120));
        }
    }
    return p.fade256 >= 256 ? c : scale565(c, p.fade256);
}

// Limits a rectangle to the screen.
static Rect clip(Rect r) {
    int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > Renderer::W ? Renderer::W : r.x + r.w;
    int y1 = r.y + r.h > Renderer::H ? Renderer::H : r.y + r.h;
    if (x1 <= x0 || y1 <= y0) return {0, 0, 0, 0};
    return {int16_t(x0), int16_t(y0), int16_t(x1 - x0), int16_t(y1 - y0)};
}

static Rect unite(Rect a, Rect b) {
    a = clip(a);
    b = clip(b);
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    const int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    const int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return {int16_t(x0), int16_t(y0), int16_t(x1 - x0), int16_t(y1 - y0)};
}

// Merges the parts of the animation that change in two frames.
static Renderer::IntroRegions merge(const Renderer::IntroRegions &a, const Renderer::IntroRegions &b) {
    Renderer::IntroRegions m = a;
    if (b.sweep) {
        m.sweepFrom = a.sweep ? fminf(a.sweepFrom, b.sweepFrom) : b.sweepFrom;
        m.sweepTo = a.sweep ? fmaxf(a.sweepTo, b.sweepTo) : b.sweepTo;
        m.sweep = true;
    }
    m.discR = fmaxf(a.discR, b.discR);
    if (b.glint) {
        m.glintLo = a.glint ? fminf(a.glintLo, b.glintLo) : b.glintLo;
        m.glintHi = a.glint ? fmaxf(a.glintHi, b.glintHi) : b.glintHi;
        m.glint = true;
    }
    return m;
}

// Start-up animation. Only what changes is redrawn: the slice the ring sweep
// passed, the disc while it grows and turns, the band of the glint. With a
// target (the frame buffer about to be shown, last drawn two frames ago) the
// changes of this and the previous frame are drawn straight into it, which
// saves copying the frame. Returns the area that changed.
INTRO_HOT Rect Renderer::drawIntro(float t, bool intoRoundel, uint16_t *target) {
    if (!roundelValid_) buildRoundelLayer();
    uint16_t *out = target ? target : frame_;

    IntroParams p;
    const float offset = s_.angle * PI_F / 180.0f;
    p.offFrac = offset / (2 * PI_F);
    p.offFrac -= floorf(p.offFrac);
    p.ringP = t < 1.1f ? easeInOutCubic(t / 1.1f) : 1.0f;    // ring sweep
    const float discT = clamp01((t - 0.75f) / 1.35f);          // quarters
    p.discS = discT > 0 ? easeOutBack(discT) : 0;              // disc size
    p.invS = p.discS > 0 ? 1 / p.discS : 0;
    const float phi = offset - (1 - easeOutCubic(discT)) * 1.5f * 2 * PI_F;
    p.cs = cosf(phi);
    p.sn = sinf(phi);
    const float glintT = (t - 2.1f) / 0.75f;                   // light sweep
    p.glint = glintT > 0 && glintT < 1;
    p.glintC = -360 + 720 * glintT;
    const float fade = intoRoundel ? 1.0f : 1.0f - clamp01((t - 2.95f) / 0.4f);
    p.fade256 = int(fade * 256);
    p.rDisc2 = (R_DISC - 0.5f) * (R_DISC - 0.5f);
    p.rIn2 = (R_DISC + 1) * (R_DISC + 1);
    p.dx0 = int(CX) - DISC / 2;
    p.dy0 = int(CY) - DISC / 2;

    IntroParams q = introPrev_;
    introPrev_ = p;
    if (p.fade256 < 256 || q.fade256 < 256) {
        // Fading out into another mode: everything changes.
        for (int y = 0; y < H; y++) {
            uint16_t *row = out + y * W;
            for (int x = 0; x < W; x++) row[x] = introPixel(x, y, p);
        }
        introRegionsPrev_ = {};
        introPixels_ += W * H;
        return {0, 0, W, H};
    }

    // Both frame buffers start from black (each is cleared the first time).
    Rect changed = {0, 0, 0, 0};
    if (introClear_ > 0) {
        introClear_--;
        memset(out, 0, size_t(W) * H * 2);
        if (introClear_ == 1 || !target) {  // very first frame
            q = p;
            q.ringP = 0;
            introRegionsPrev_ = {};
        }
        changed = {0, 0, W, H};
    }

    // What changes from the last frame to this one.
    IntroRegions cur;
    if (p.ringP < 1 || q.ringP < 1) {
        cur.sweep = true;
        cur.sweepFrom = q.ringP - 0.05f < 0 ? 0 : q.ringP - 0.05f;
        cur.sweepTo = p.ringP + 0.05f > 1 ? 1 : p.ringP + 0.05f;
    }
    if (p.discS != q.discS || p.cs != q.cs || p.sn != q.sn) {
        // While the disc grows only its current size changes; the first time
        // it appears the inner rim's edge is uncovered too.
        const float grow = q.discS > 0 ? fminf(1.0f, fmaxf(p.discS, q.discS)) : 1.0f;
        cur.discR = R_DISC * grow + 3;
    }
    if (p.glint || q.glint) {
        cur.glint = true;
        cur.glintLo = fminf(p.glint ? p.glintC : q.glintC, q.glint ? q.glintC : p.glintC);
        cur.glintHi = fmaxf(p.glint ? p.glintC : q.glintC, q.glint ? q.glintC : p.glintC);
    }
    // Drawing into a frame buffer: it also needs the previous frame's changes.
    const IntroRegions draw = target ? merge(cur, introRegionsPrev_) : cur;
    introRegionsPrev_ = cur;

    const float R_OUT2 = 240.0f * 240.0f;
    // Ring sweep: pixels of the ring whose angle lies in the swept slice.
    if (draw.sweep) {
        const float a0 = draw.sweepFrom + p.offFrac, a1 = draw.sweepTo + p.offFrac;
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        const int n = 8 + int((a1 - a0) * 64);
        for (int i = 0; i <= n; i++) {
            const float a = (a0 + (a1 - a0) * i / n) * 2 * PI_F;
            for (float r : {R_DISC - 2.0f, 240.0f}) {
                const float x = CX + r * sinf(a), y = CY - r * cosf(a);
                bx0 = fminf(bx0, x);
                by0 = fminf(by0, y);
                bx1 = fmaxf(bx1, x);
                by1 = fmaxf(by1, y);
            }
        }
        const Rect box = clip({int16_t(bx0 - 8), int16_t(by0 - 8), int16_t(bx1 - bx0 + 16), int16_t(by1 - by0 + 16)});
        const float lo = draw.sweepFrom - 0.002f, hi = draw.sweepTo + 0.002f;
        for (int y = box.y; y < box.y + box.h; y++) {
            const float fy = y + 0.5f - CY;
            uint16_t *row = out + y * W;
            const uint16_t *ang = angle_ + y * W;
            const uint16_t *lay = layer_ + y * W;
            for (int x = box.x; x < box.x + box.w; x++) {
                const float fx = x + 0.5f - CX;
                const float r2 = fx * fx + fy * fy;
                if (r2 < p.rDisc2 || r2 >= R_OUT2) continue;
                float af = ang[x] * (1 / 65535.0f) - p.offFrac;
                af = af < 0 ? af + 1 : (af >= 1 ? af - 1 : af);
                if (af < lo || af > hi) continue;
                if (p.glint) {
                    row[x] = introPixel(x, y, p);
                    continue;
                }
                // introPixel() for a ring pixel, reusing r2 and af.
                uint16_t c = lay[x];
                if (r2 < p.rIn2 && p.discS <= 0) c = 0;
                if (p.ringP < 1) {
                    const float vis = clamp01((p.ringP - af) * 60 + 0.5f);
                    c = scale565(c, int(vis * 256));
                    const float d = (af - p.ringP) * 70;
                    if (d > -3 && d < 3 && r2 > p.rIn2) {
                        const float e = expf(-d * d);
                        c = lighten565(c, int(e * 220 * vis + e * 60));
                    }
                }
                row[x] = c;
            }
            introPixels_ += box.w;
        }
        changed = unite(changed, box);
    }
    // Disc: the round area that grows / turns.
    if (draw.discR > 0) {
        const float rr = fminf(draw.discR, R_DISC + 3);
        const int y0 = int(CY - rr), y1 = int(ceilf(CY + rr));
        for (int y = y0 < 0 ? 0 : y0; y < (y1 > H ? H : y1); y++) {
            const float fy = y + 0.5f - CY;
            const float h2 = rr * rr - fy * fy;
            if (h2 <= 0) continue;
            const float half = sqrtf(h2) + 1;
            int x0 = int(CX - half), x1 = int(ceilf(CX + half));
            x0 = x0 < 0 ? 0 : x0;
            x1 = x1 > W ? W : x1;
            uint16_t *row = out + y * W;
            // introPixel() for the inside of the disc, with what doesn't
            // change along the row worked out once.
            const float fy2 = fy * fy, sy = fy * p.invS, sy2 = sy * sy;
            const float sySn = sy * p.sn, syCs = sy * p.cs;
            int iy = int(sy + CY) - p.dy0;
            iy = iy < 0 ? 0 : (iy >= DISC ? DISC - 1 : iy);
            const uint16_t *ra = discA_ + iy * DISC - p.dx0, *rb = discB_ + iy * DISC - p.dx0;
            const bool simple = !p.glint;
            for (int x = x0; x < x1; x++) {
                const float fx = x + 0.5f - CX;
                if (!simple || fx * fx + fy2 >= p.rDisc2) {
                    row[x] = introPixel(x, y, p);
                    continue;
                }
                uint16_t c = 0;
                if (p.discS > 0) {
                    const float sx = fx * p.invS;
                    if (sx * sx + sy2 < p.rDisc2) {  // then the index is within the table
                        const int ix = int(sx + CX);
                        const float u = sx * p.cs + sySn;
                        const float v = -sx * p.sn + syCs;
                        const float d = fminf(fabsf(u), fabsf(v)) * p.discS;
                        c = (u * v) > 0 ? ra[ix] : rb[ix];
                        if (d < DIVIDER + 0.5f) c = blend565(c, divider_, int(clamp01(DIVIDER + 0.5f - d) * 256));
                    }
                }
                row[x] = c;
            }
            introPixels_ += x1 - x0;
        }
        const int ri = int(ceilf(rr)) + 1;
        changed = unite(changed, {int16_t(CX - ri), int16_t(CY - ri), int16_t(2 * ri), int16_t(2 * ri)});
    }
    // Glint: the band, within the badge.
    if (draw.glint) {
        const float k = 1 / 0.7071f;
        for (int y = 0; y < H; y++) {
            const float fy = y + 0.5f - CY;
            const float h2 = R_OUT2 - fy * fy;
            if (h2 <= 0) continue;
            const float half = sqrtf(h2) + 1;
            int x0 = int(floorf((draw.glintLo - 60) * k - fy + CX - 2));
            int x1 = int(ceilf((draw.glintHi + 60) * k - fy + CX + 2));
            x0 = x0 < int(CX - half) ? int(CX - half) : x0;
            x1 = x1 > int(ceilf(CX + half)) ? int(ceilf(CX + half)) : x1;
            x0 = x0 < 0 ? 0 : x0;
            x1 = x1 > W ? W : x1;
            uint16_t *row = out + y * W;
            for (int x = x0; x < x1; x++) row[x] = introPixel(x, y, p);
            introPixels_ += x1 > x0 ? x1 - x0 : 0;
        }
        changed = {0, 0, W, H};
    }
    return clip(changed);
}

// ---------------------------------------------------------------------------
// Stripes

// Carbon-fibre twill with a soft diagonal sheen and vignette.
void Renderer::buildCarbonLayer() {
    const RGBf bg = rgbf(s_.stripeBg);
    const int cell = 7;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int tu = x / cell, tv = y / cell;
            bool horiz = ((tu + tv) & 3) < 2;  // 2x2 twill
            float pos = horiz ? (y % cell + 0.5f) / cell : (x % cell + 0.5f) / cell;
            float along = horiz ? (x % cell + 0.5f) / cell : (y % cell + 0.5f) / cell;
            float bulge = sinf(PI_F * pos);                // round threads
            float fibre = 0.9f + 0.1f * sinf(along * 18);  // fine fibres
            float f = (0.55f + 0.6f * bulge) * fibre * (horiz ? 1.08f : 0.92f);
            float fx = x + 0.5f - CX, fy = y + 0.5f - CY;
            float sheenD = (fx + fy) * 0.7071f + 120;  // band across the upper left
            float sheen = expf(-sheenD * sheenD / (2 * 110.0f * 110.0f)) * 0.22f;
            RGBf c = mix(scale(bg, f), WHITE, sheen * 0.5f * bulge);
            c = scale(c, vignette_[y * W + x] / 255.0f);
            carbon_[y * W + x] = to565d(c, x, y);
        }
    }
    carbonValid_ = true;
}

// Across the stripes everything depends only on the position across them
// (pos), so it's worked out once per frame in steps of 1/STRIPE_RES pixel.
constexpr int STRIPE_RES = 4;
constexpr float STRIPE_W = 58.0f;           // one stripe
constexpr float STRIPE_PERIOD = 6 * STRIPE_W;  // three stripes, then as much carbon (scrolling)
constexpr float STRIPE_SHADOW = 16.0f;      // shadow of the stripes on the carbon
// Without scrolling, pos runs over the stripes plus their shadows.
constexpr float STRIPE_LO = -STRIPE_SHADOW - 2, STRIPE_HI = 3 * STRIPE_W + STRIPE_SHADOW + 2;

struct StripeStep {
    float r, g, b;   // stripe colour (0..255), before the edge darkening
    int16_t bg;      // carbon brightness, /256 (the shadow)
    int16_t cover;   // how much of the pixel is stripe, /256
};

INTRO_HOT void Renderer::drawStripes(float offset, bool repeat, uint16_t *out) {
    if (!carbonValid_) buildCarbonLayer();
    const float w = STRIPE_W, period = STRIPE_PERIOD;
    const float lo = repeat ? 0 : STRIPE_LO;
    const int steps = int(((repeat ? period : STRIPE_HI) - lo) * STRIPE_RES);
    static_assert(STRIPE_HI - STRIPE_LO <= STRIPE_PERIOD, "the scrolling table is the bigger one");
    static StripeStep prof[int(STRIPE_PERIOD * STRIPE_RES) + 2];

    // Each stripe is slightly rounded: lighter in the middle, darker at the edges.
    const RGBf cols[3] = {rgbf(s_.stripe1), rgbf(s_.stripe2), rgbf(s_.stripe3)};
    for (int i = 0; i <= steps; i++) {
        const float pos = lo + (i + 0.5f) / STRIPE_RES;
        // Distance outside the stripe group (for its shadow on the carbon).
        float out;
        if (repeat) {
            out = pos < 3 * w ? -1 : fminf(pos - 3 * w, period - pos);
        } else {
            out = pos < 0 ? -pos : (pos >= 3 * w ? pos - 3 * w : -1);
        }
        StripeStep &st = prof[i];
        st.bg = 256;
        if (out >= 0 && out < STRIPE_SHADOW) {
            const float sh = 1 - out / STRIPE_SHADOW;
            st.bg = int16_t(256 * (1 - 0.6f * sh * sh));
        }
        st.cover = out < 0 ? 256 : (out < 0.5f ? int16_t((0.5f - out) * 256) : 0);
        int b = int(floorf(pos / w));
        b = b < 0 ? 0 : (b > 2 ? 2 : b);
        const float f = sinf(PI_F * clamp01(pos / w - b));
        const RGBf c = mix(scale(cols[b], 0.72f + 0.3f * f), WHITE, 0.1f * f * f);
        st.r = c.r;
        st.g = c.g;
        st.b = c.b;
    }
    const StripeStep plain = {0, 0, 0, 256, 0};  // carbon away from the stripes

    const float beta = (s_.angle + 30) * PI_F / 180.0f;
    const float cs = cosf(beta), sn = sinf(beta);
    const float res = STRIPE_RES, periodRes = period * STRIPE_RES;
    const float R2 = 240.5f * 240.5f;
    const float vigK = 0.42f / (240.0f * 240.0f * 240.0f * 240.0f);

    for (int y = 0; y < H; y++) {
        const float fy = y + 0.5f - CY;
        // Only inside the round screen; the corners aren't visible.
        const float h2 = R2 - fy * fy;
        if (h2 <= 0) continue;
        const float half = sqrtf(h2);
        int x0 = int(CX - half), x1 = int(ceilf(CX + half));
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 > W ? W : x1;
        uint16_t *row = out + y * W;
        const uint16_t *bgRow = carbon_ + y * W;
        // Ordered dither (as to565d) so the stripes' shading doesn't band.
        float dr[4], dg[4];
        for (int i = 0; i < 4; i++) {
            const float d = (BAYER[y & 3][i] - 7.5f) / 16.0f;
            dr[i] = d * 8 + 0.5f;
            dg[i] = d * 4 + 0.5f;
        }
        // Position across the stripes, in steps, moving along the row.
        const float fx0 = x0 + 0.5f - CX;
        // (Without scrolling, the group is centred: pos = q + 1.5 stripes.)
        float p = ((fx0 * cs + fy * sn - offset) + (repeat ? 0 : 1.5f * w) - lo) * res;
        const float dp = cs * res;
        if (repeat) p -= floorf(p / periodRes) * periodRes;
        const float fy2 = fy * fy;
        for (int x = x0; x < x1; x++, p += dp) {
            if (repeat) {
                if (p >= periodRes) p -= periodRes;
                else if (p < 0) p += periodRes;
            }
            const int i = int(p);
            const StripeStep &st = (p >= 0 && i < steps) ? prof[i] : plain;
            uint16_t bgc = bgRow[x];
            if (st.bg < 256) bgc = scale565(bgc, st.bg);
            if (st.cover == 0) {
                row[x] = bgc;
                continue;
            }
            // The same edge darkening as the carbon (vignette_), worked out
            // here rather than read from memory.
            const float fx = x + 0.5f - CX, r2 = fx * fx + fy2;
            const float v = 1.0f - vigK * r2 * r2;
            const int k = x & 3;
            const float rf = st.r * v + dr[k], gf = st.g * v + dg[k], bf = st.b * v + dr[k];
            const int r8 = rf <= 0 ? 0 : (rf >= 255 ? 255 : int(rf));
            const int g8 = gf <= 0 ? 0 : (gf >= 255 ? 255 : int(gf));
            const int b8 = bf <= 0 ? 0 : (bf >= 255 ? 255 : int(bf));
            uint16_t c = uint16_t(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));
            if (st.cover < 256) c = blend565(bgc, c, st.cover);
            row[x] = c;
        }
    }
}

// ---------------------------------------------------------------------------
// Text

void Renderer::drawText() {
    const RGBf bg = rgbf(s_.textBg);
    const RGBf fg = rgbf(s_.textFg);

    // Split into lines on '|'.
    const char *lines[4];
    int lens[4];
    int count = 0;
    const char *p = s_.text;
    while (count < 4) {
        const char *bar = strchr(p, '|');
        lines[count] = p;
        lens[count] = bar ? int(bar - p) : int(strlen(p));
        count++;
        if (!bar) break;
        p = bar + 1;
    }

    static TextLayout layouts[4];
    float maxW = 1;
    for (int i = 0; i < count; i++) {
        layoutText(layouts[i], lines[i], lens[i], 2.0f);
        if (layouts[i].width > maxW) maxW = layouts[i].width;
    }
    const float lineGap = 5;
    const float totalH = count * FONT_CAP_HEIGHT + (count - 1) * lineGap;
    float sc = 330.0f / (maxW + STROKE);
    if (sc > 290.0f / totalH) sc = 290.0f / totalH;
    if (sc > 20) sc = 20;
    const float half = STROKE * sc / 2;
    const float shOffX = sc * 0.3f, shOffY = sc * 0.45f, shBlur = sc * 0.7f;
    const float top = CY - totalH * sc / 2, bottom = CY + totalH * sc / 2;

    memset(mask_, 0, W * H);
    memset(shadow_, 0, W * H);
    for (int i = 0; i < count; i++) {
        const TextLayout &t = layouts[i];
        const float ox = CX - t.width * sc / 2;
        // Baseline in pixels (screen y grows downwards).
        const float base = CY + totalH * sc / 2 - (count - 1 - i) * (FONT_CAP_HEIGHT + lineGap) * sc;
        for (int k = 0; k < t.segCount; k++) {
            const Seg &s = t.segs[k];
            float x0 = ox + s.x0 * sc, y0 = base - s.y0 * sc;
            float x1 = ox + s.x1 * sc, y1 = base - s.y1 * sc;
            const float reach = half + shBlur + fmaxf(shOffX, shOffY) + 2;
            int bx0 = int(fminf(x0, x1) - reach), bx1 = int(fmaxf(x0, x1) + reach);
            int by0 = int(fminf(y0, y1) - reach), by1 = int(fmaxf(y0, y1) + reach);
            if (bx0 < 0) bx0 = 0;
            if (by0 < 0) by0 = 0;
            if (bx1 >= W) bx1 = W - 1;
            if (by1 >= H) by1 = H - 1;
            for (int y = by0; y <= by1; y++) {
                for (int x = bx0; x <= bx1; x++) {
                    const int i2 = y * W + x;
                    float d = sqrtf(segDist2(x + 0.5f, y + 0.5f, x0, y0, x1, y1));
                    uint8_t cov = uint8_t(clamp01(half + 0.5f - d) * 255);
                    if (cov > mask_[i2]) mask_[i2] = cov;
                    float ds = sqrtf(segDist2(x + 0.5f - shOffX, y + 0.5f - shOffY, x0, y0, x1, y1));
                    uint8_t sc8 = uint8_t(clamp01((half + shBlur - ds) / (shBlur * 2)) * 255);
                    if (sc8 > shadow_[i2]) shadow_[i2] = sc8;
                }
            }
        }
    }

    // Background: gently lit from the middle. Text: top-to-bottom gradient.
    static RGBf rowFg[H];
    for (int y = 0; y < H; y++) {
        float g = clamp01((y - top) / (bottom - top + 1));
        rowFg[y] = mix(mix(fg, WHITE, 0.3f), scale(fg, 0.72f), g);
    }
    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        for (int x = 0; x < W; x++) {
            const int i = y * W + x;
            float fx = x + 0.5f - CX;
            float rr = (fx * fx + fy * fy) / (240.0f * 240.0f);
            RGBf c = mix(scale(bg, 0.65f + 0.45f * (1 - rr)), WHITE, 0.06f * (1 - rr));
            c = scale(c, 1 - 0.75f * shadow_[i] / 255.0f);
            if (mask_[i]) c = mix(c, rowFg[y], mask_[i] / 255.0f);
            frame_[i] = to565d(c, x, y);
        }
    }
    if (s_.angle) {
        // Turned like the other modes; the carbon buffer is free in text mode.
        memcpy(carbon_, frame_, size_t(W) * H * 2);
        carbonValid_ = false;
        rotateInto(frame_, carbon_, s_.angle);
    }
}

// Copies src into dst turned clockwise about the centre (bilinear).
void Renderer::rotateInto(uint16_t *dst, const uint16_t *src, float degrees) {
    const float a = degrees * PI_F / 180.0f, cs = cosf(a), sn = sinf(a);
    const int32_t stepX = int32_t(cs * 65536), stepY = int32_t(-sn * 65536);
    const float fx0 = 0.5f - CX;
    for (int y = 0; y < H; y++) {
        const float fy = y + 0.5f - CY;
        int32_t sx = int32_t((fx0 * cs + fy * sn + CX - 0.5f) * 65536);
        int32_t sy = int32_t((-fx0 * sn + fy * cs + CY - 0.5f) * 65536);
        uint16_t *row = dst + y * W;
        for (int x = 0; x < W; x++, sx += stepX, sy += stepY) {
            const int ix = sx >> 16, iy = sy >> 16;
            if (ix < 0 || iy < 0 || ix >= W - 1 || iy >= H - 1) {
                row[x] = 0;
                continue;
            }
            const uint16_t *p = src + iy * W + ix;
            const int tx = (sx >> 8) & 255, ty = (sy >> 8) & 255;
            row[x] = blend565(blend565(p[0], p[1], tx), blend565(p[W], p[W + 1], tx), ty);
        }
    }
}

void Renderer::drawImage() {
    if (imageValid_) {
        if (s_.angle) {
            rotateInto(frame_, image_, s_.angle);
        } else {
            memcpy(frame_, image_, size_t(W) * H * 2);
        }
        return;
    }
    Settings keep = s_;
    s_.textBg = 0x000000;
    s_.textFg = 0x5A5A5A;
    strcpy(s_.text, "NO|IMAGE");
    drawText();
    s_ = keep;
}

Rect Renderer::showMessage(const char *text, uint32_t fg) {
    Settings keep = s_;
    s_.textBg = 0x000000;
    s_.textFg = fg;
    strncpy(s_.text, text, sizeof(s_.text) - 1);
    s_.text[sizeof(s_.text) - 1] = 0;
    drawText();
    s_ = keep;
    dirty_ = true;
    return {0, 0, W, H};
}

// ---------------------------------------------------------------------------

Rect Renderer::render(uint32_t ms, uint16_t *introTarget) {
    drewIntoTarget_ = false;
    const Rect full = {0, 0, W, H};
    const Rect disc = {int16_t(CX - R_DISC), int16_t(CY - R_DISC), int16_t(2 * R_DISC), int16_t(2 * R_DISC)};
    float dt = lastMs_ && int32_t(ms - lastMs_) > 0 ? (ms - lastMs_) / 1000.0f : 0;
    if (dt > 0.2f) dt = 0.2f;  // don't jump after a stall
    lastMs_ = ms;

    if (introT_ >= 0) {
        // The animation's own clock: it advances by at most 0.15 s per frame,
        // so a slow frame (start-up, storage, Wi-Fi) pauses it rather than
        // skipping ahead to the end.
        const bool intoRoundel = s_.mode == MODE_ROUNDEL || s_.mode == MODE_SPIN;
        const float t = introT_;
        introT_ += dt < 0.15f ? dt : 0.15f;
        if (t < (intoRoundel ? 2.9f : 3.4f)) {
            drewIntoTarget_ = introTarget != nullptr;
            return drawIntro(t, intoRoundel, introTarget);
        }
        introT_ = -1;
        phase_ = 0;
        dirty_ = true;
    }

    const bool wasDirty = dirty_;
    dirty_ = false;
    const float offset = s_.angle * PI_F / 180.0f;

    switch (s_.mode) {
        case MODE_ROUNDEL:
        case MODE_SPIN: {
            if (s_.mode == MODE_SPIN) phase_ += dt * (s_.speed / 100.0f) * 2 * PI_F * boost_;
            bool moving = s_.mode == MODE_SPIN && s_.speed != 0;
            if (!wasDirty && !moving) return {0, 0, 0, 0};
            if (wasDirty) {
                if (!roundelValid_) buildRoundelLayer();
                memcpy(frame_, layer_, size_t(W) * H * 2);
            }
            drawDisc(offset + (s_.mode == MODE_SPIN ? phase_ : 0));
            return wasDirty ? full : disc;
        }
        case MODE_STRIPES:
            if (s_.speed != 0) {
                phase_ += dt * s_.speed * 1.5f * boost_;
            } else if (!wasDirty) {
                return {0, 0, 0, 0};
            }
            // Every visible pixel changes, so it can go straight into the
            // frame buffer about to be shown.
            drawStripes(phase_, s_.speed != 0, introTarget ? introTarget : frame_);
            drewIntoTarget_ = introTarget != nullptr;
            return full;
        case MODE_IMAGE:
            if (!wasDirty) return {0, 0, 0, 0};
            drawImage();
            return full;
        case MODE_TEXT:
            if (!wasDirty) return {0, 0, 0, 0};
            drawText();
            return full;
        default:
            return {0, 0, 0, 0};
    }
}
