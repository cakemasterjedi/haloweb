#include "renderer.h"

#include <math.h>
#include <string.h>

#include "stroke_font.h"

namespace {

const float PI_F = 3.14159265f;
const float CX = Renderer::W / 2.0f;
const float CY = Renderer::H / 2.0f;

// Roundel geometry (pixels from the centre).
const float R_EDGE = 239.5f;       // outer edge of the outer chrome rim
const float R_RING = 228.0f;       // outer edge of the black ring
const float R_INNER_RIM = 158.0f;  // outer edge of the inner chrome rim
const float R_DISC = 147.0f;       // quarters
const float LABEL_BASE = 168.5f;   // baseline of the ring lettering
const float LABEL_SCALE = 6.2f;    // pixels per font unit on the ring
const float LABEL_STROKE = 1.6f;   // ring lettering stroke thickness in font units
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
RGBf chrome(const RGBf &base, float fx, float fy, float r, float r0, float r1) {
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
    RGBf c = scale(base, 0.16f + 0.78f * d + 0.28f * env);
    return mix(c, WHITE, spec * 0.9f);
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
    if (!frame_ || !layer_ || !image_ || !angle_ || !discA_ || !discB_ || !vignette_ || !mask_ || !shadow_) {
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
    s_ = s;
    dirty_ = true;
    layer = LAYER_NONE;  // colours may have changed
}

void Renderer::startIntro(uint32_t ms) {
    introStart_ = ms ? ms : 1;
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

float Renderer::distToLayout(const TextLayout &t, float u, float v) {
    const float margin = LABEL_STROKE + 1;
    float best = 1e9f;
    for (int g = 0; g < t.glyphCount; g++) {
        if (u < t.glyphX0[g] - margin || u > t.glyphX1[g] + margin) continue;
        for (int i = t.glyphFirst[g]; i < t.glyphLast[g]; i++) {
            const Seg &s = t.segs[i];
            float d = segDist2(u, v, s.x0, s.y0, s.x1, s.y1);
            if (d < best) best = d;
        }
    }
    return sqrtf(best);
}

// ---------------------------------------------------------------------------
// Roundel

void Renderer::buildRoundelLayer() {
    layoutText(text_, s_.labelText, strlen(s_.labelText), s_.spacing);

    const RGBf rim = rgbf(s_.rim);
    const RGBf ring = rgbf(s_.ring);
    const RGBf label = rgbf(s_.label);
    const RGBf qa = rgbf(s_.quadA), qb = rgbf(s_.quadB);
    const float offset = s_.angle * PI_F / 180.0f;
    const float labelTop = LABEL_BASE + FONT_CAP_HEIGHT * LABEL_SCALE;
    const float labelMid = LABEL_BASE + FONT_CAP_HEIGHT * LABEL_SCALE / 2;
    const float halfStroke = LABEL_STROKE * LABEL_SCALE / 2;

    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            float r = sqrtf(fx * fx + fy * fy);
            RGBf c = BLACK;
            if (r < R_EDGE + 1) {
                c = mix(c, chrome(rim, fx, fy, r, R_RING - 1, R_EDGE), inside(R_EDGE, r));
            }
            if (r < R_RING + 1) {
                // Glossy ring: a soft sheen on the lit (upper-left) side and
                // darker next to the rims.
                float lit = clamp01((fx * LX + fy * LY) / r);
                float across = (r - R_INNER_RIM) / (R_RING - R_INNER_RIM);
                float edgeShade = 1 - 0.4f * (smoothstep(0.7f, 1.0f, across) + smoothstep(0.3f, 0.0f, across));
                float gloss = lit * lit * lit * (1 - fabsf(across - 0.5f) * 1.6f);
                RGBf ringPx = scale(add(ring, 55.0f * clamp01(gloss)), edgeShade);

                if (r > LABEL_BASE - 10 && r < labelTop + 10) {
                    float a = atan2f(fx, -fy) - offset;  // clockwise from the top
                    while (a > PI_F) a -= 2 * PI_F;
                    while (a < -PI_F) a += 2 * PI_F;
                    float u = a * labelMid / LABEL_SCALE + text_.width / 2;
                    float v = (r - LABEL_BASE) / LABEL_SCALE;
                    float d = distToLayout(text_, u, v) * LABEL_SCALE;
                    // Soft shadow around the letters, then the letters with a
                    // top-to-bottom metallic gradient.
                    float sh = clamp01((halfStroke + 5.0f - d) / 5.0f);
                    ringPx = scale(ringPx, 1 - 0.55f * sh * sh);
                    float cov = clamp01(halfStroke + 0.5f - d);
                    if (cov > 0) {
                        float g = clamp01(v / FONT_CAP_HEIGHT);
                        RGBf l = mix(scale(label, 0.78f), mix(label, WHITE, 0.35f), g);
                        float edge = clamp01((halfStroke - d) / 2.5f);  // bevel: brighter core
                        l = mix(scale(l, 0.8f), l, edge);
                        ringPx = mix(ringPx, l, cov);
                    }
                }
                c = mix(c, ringPx, inside(R_RING, r));
            }
            if (r < R_INNER_RIM + 1) {
                c = mix(c, chrome(rim, fx, fy, r, R_DISC - 1, R_INNER_RIM), inside(R_INNER_RIM, r));
            }
            if (r < R_DISC + 1) {
                c = mix(c, mix(qa, qb, 0.5f), inside(R_DISC, r));
            }
            layer_[y * W + x] = to565d(c, x, y);
        }
    }

    // Domed, glossy quarters: slightly darker towards the rim, with a soft
    // highlight on the upper left. One image per quarter colour.
    const int x0 = int(CX) - DISC / 2, y0 = int(CY) - DISC / 2;
    for (int y = 0; y < DISC; y++) {
        float fy = y0 + y + 0.5f - CY;
        for (int x = 0; x < DISC; x++) {
            float fx = x0 + x + 0.5f - CX;
            float rr = sqrtf(fx * fx + fy * fy) / R_DISC;
            float shade = (1.0f - 0.12f * rr * rr) * (1.0f - 0.3f * smoothstep(0.84f, 1.0f, rr));
            float hx = fx + 58, hy = fy + 66;
            float gloss = expf(-(hx * hx + hy * hy) / (2 * 62.0f * 62.0f)) * 0.26f;
            float rimGlow = smoothstep(0.93f, 1.0f, rr) * clamp01(-(fx * LX + fy * LY) / (R_DISC * 0.7f)) * 0.25f;
            discA_[y * DISC + x] = to565d(mix(scale(qa, shade), WHITE, gloss + rimGlow), x, y);
            discB_[y * DISC + x] = to565d(mix(scale(qb, shade), WHITE, gloss + rimGlow), x, y);
        }
    }
    layer = LAYER_ROUNDEL;
}

// Quarters, rotated clockwise by phi. Only touches pixels fully inside the disc.
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
            uint16_t own = a ? ra[x] : rb[x];
            if (d < 0.5f) {
                row[x] = blend565(a ? rb[x] : ra[x], own, int((0.5f + d) * 256));
            } else {
                row[x] = own;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Start-up animation

void Renderer::drawIntro(float t, bool intoRoundel) {
    if (layer != LAYER_ROUNDEL) buildRoundelLayer();

    const float offset = s_.angle * PI_F / 180.0f;
    const float offFrac = offset / (2 * PI_F);
    const float ringP = t < 1.1f ? easeInOutCubic(t / 1.1f) : 1.0f;       // ring sweep
    const float discT = clamp01((t - 0.75f) / 1.35f);                      // quarters
    const float discS = discT > 0 ? easeOutBack(discT) : 0;                // disc size
    const float phi = offset - (1 - easeOutCubic(discT)) * 2.5f * 2 * PI_F;
    const float glintT = (t - 2.1f) / 0.75f;                               // light sweep
    const float glintC = -360 + 720 * glintT;
    const float fade = intoRoundel ? 1.0f : 1.0f - clamp01((t - 2.95f) / 0.4f);
    const int fade256 = int(fade * 256);
    const float cs = cosf(phi), sn = sinf(phi);
    const float rDisc2 = (R_DISC - 0.5f) * (R_DISC - 0.5f);
    const float rIn2 = (R_DISC + 1) * (R_DISC + 1);
    const int dx0 = int(CX) - DISC / 2, dy0 = int(CY) - DISC / 2;

    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        uint16_t *row = frame_ + y * W;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            float r2 = fx * fx + fy * fy;
            uint16_t c = 0;
            if (r2 >= 240.0f * 240.0f) {
                row[x] = 0;
                continue;
            }
            if (r2 >= rDisc2) {
                // Ring, letters and rims, revealed clockwise from the top.
                uint16_t base = layer_[y * W + x];
                if (r2 < rIn2 && discS <= 0) base = 0;  // disc edge pixels wait for the disc
                if (ringP >= 1) {
                    c = base;
                } else {
                    float af = angle_[y * W + x] / 65535.0f - offFrac;
                    af -= floorf(af);
                    float vis = clamp01((ringP - af) * 60 + 0.5f);
                    c = scale565(base, int(vis * 256));
                    float d = (af - ringP) * 70;
                    if (d > -3 && d < 3 && r2 > rIn2) {
                        c = lighten565(c, int(expf(-d * d) * 220 * vis + expf(-d * d) * 60));
                    }
                }
            } else if (discS > 0) {
                // Quarters growing from the centre while spinning into place.
                float sx = fx / discS, sy = fy / discS;
                if (sx * sx + sy * sy < rDisc2) {
                    int ix = int(sx + CX) - dx0, iy = int(sy + CY) - dy0;
                    ix = ix < 0 ? 0 : (ix >= DISC ? DISC - 1 : ix);
                    iy = iy < 0 ? 0 : (iy >= DISC ? DISC - 1 : iy);
                    const int i = iy * DISC + ix;
                    float u = sx * cs + sy * sn;
                    float v = -sx * sn + sy * cs;
                    bool a = (u * v) > 0;
                    float d = fminf(fabsf(u), fabsf(v)) * discS;
                    c = a ? discA_[i] : discB_[i];
                    if (d < 0.5f) c = blend565(a ? discB_[i] : discA_[i], c, int((0.5f + d) * 256));
                } else {
                    c = 0;
                }
            }
            if (glintT > 0 && glintT < 1 && c) {
                float d = (fx + fy) * 0.7071f - glintC;
                if (d > -60 && d < 60) {
                    float g = 1 - (d / 60) * (d / 60);
                    c = lighten565(c, int(g * g * 120));
                }
            }
            row[x] = fade256 >= 256 ? c : scale565(c, fade256);
        }
    }
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
            layer_[y * W + x] = to565d(c, x, y);
        }
    }
    layer = LAYER_CARBON;
}

void Renderer::drawStripes(float offset, bool repeat) {
    if (layer != LAYER_CARBON) buildCarbonLayer();

    // Each stripe is slightly rounded: lighter in the middle, darker at the edges.
    const RGBf cols[3] = {rgbf(s_.stripe1), rgbf(s_.stripe2), rgbf(s_.stripe3)};
    static uint16_t shades[3][16];
    for (int b = 0; b < 3; b++) {
        for (int k = 0; k < 16; k++) {
            float f = sinf(PI_F * (k + 0.5f) / 16);
            shades[b][k] = to565(mix(scale(cols[b], 0.72f + 0.3f * f), WHITE, 0.1f * f * f));
        }
    }

    const float w = 58.0f;
    const float period = 6 * w;
    const float beta = (s_.angle + 30) * PI_F / 180.0f;
    const float cs = cosf(beta), sn = sinf(beta);
    const float invW = 1 / w, invPeriod = 1 / period;
    const float shadowW = 16.0f;

    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        uint16_t *row = frame_ + y * W;
        const uint16_t *bgRow = layer_ + y * W;
        const uint8_t *vig = vignette_ + y * W;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            float q = fx * cs + fy * sn - offset;
            float pos;
            if (repeat) {
                pos = q - floorf(q * invPeriod) * period;
            } else {
                pos = q + 1.5f * w;
            }
            uint16_t bgc = bgRow[x];
            // Distance from the stripe group (for its shadow on the carbon).
            float out;
            if (repeat) {
                out = pos < 3 * w ? -1 : fminf(pos - 3 * w, period - pos);
            } else {
                out = pos < 0 ? -pos : (pos >= 3 * w ? pos - 3 * w : -1);
            }
            if (out >= 0) {
                if (out < shadowW) {
                    float s = 1 - out / shadowW;
                    bgc = scale565(bgc, int(256 * (1 - 0.6f * s * s)));
                }
                if (out > 0.5f) {
                    row[x] = bgc;
                    continue;
                }
            }
            int b = int(floorf(pos * invW));
            float frac = pos - b * w;
            int bi = repeat ? ((b % 6) + 6) % 6 : b;
            uint16_t c;
            if (bi >= 0 && bi < 3) {
                int k = int(frac * 16 / w);
                k = k < 0 ? 0 : (k > 15 ? 15 : k);
                c = scale565(shades[bi][k], vig[x] + 1);
            } else {
                c = bgc;
            }
            // Anti-alias the outer edges of the stripe group.
            if (out >= 0) c = blend565(bgc, c, int((0.5f - out) * 256));
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
}

void Renderer::drawImage() {
    if (imageValid_) {
        memcpy(frame_, image_, size_t(W) * H * 2);
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

Rect Renderer::render(uint32_t ms) {
    const Rect full = {0, 0, W, H};
    const Rect disc = {int16_t(CX - R_DISC), int16_t(CY - R_DISC), int16_t(2 * R_DISC), int16_t(2 * R_DISC)};
    float dt = lastMs_ ? (ms - lastMs_) / 1000.0f : 0;
    if (dt > 0.2f) dt = 0.2f;  // don't jump after a stall
    lastMs_ = ms;

    if (introStart_) {
        const bool intoRoundel = s_.mode == MODE_ROUNDEL || s_.mode == MODE_SPIN;
        const float t = (ms - introStart_) / 1000.0f;
        if (t < (intoRoundel ? 2.9f : 3.4f)) {
            drawIntro(t, intoRoundel);
            return full;
        }
        introStart_ = 0;
        phase_ = 0;
        dirty_ = true;
    }

    const bool wasDirty = dirty_;
    dirty_ = false;
    const float offset = s_.angle * PI_F / 180.0f;

    switch (s_.mode) {
        case MODE_ROUNDEL:
        case MODE_SPIN: {
            if (s_.mode == MODE_SPIN) phase_ += dt * (s_.speed / 100.0f) * 2 * PI_F;
            bool moving = s_.mode == MODE_SPIN && s_.speed != 0;
            if (!wasDirty && !moving) return {0, 0, 0, 0};
            if (wasDirty) {
                if (layer != LAYER_ROUNDEL) buildRoundelLayer();
                memcpy(frame_, layer_, size_t(W) * H * 2);
            }
            drawDisc(offset + (s_.mode == MODE_SPIN ? phase_ : 0));
            return wasDirty ? full : disc;
        }
        case MODE_STRIPES:
            if (s_.speed != 0) {
                phase_ += dt * s_.speed * 1.5f;
            } else if (!wasDirty) {
                return {0, 0, 0, 0};
            }
            drawStripes(phase_, s_.speed != 0);
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
