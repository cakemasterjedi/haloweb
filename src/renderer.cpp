#include "renderer.h"

#include <math.h>
#include <string.h>

#include "stroke_font.h"

namespace {

const float PI_F = 3.14159265f;
const float CX = Renderer::W / 2.0f;
const float CY = Renderer::H / 2.0f;

// Roundel geometry (pixels from the centre).
const float R_EDGE = 239.5f;       // outer edge of the chrome rim
const float R_RING = 232.0f;       // outer edge of the black ring
const float R_INNER_RIM = 156.0f;  // outer edge of the inner chrome rim
const float R_DISC = 150.0f;       // blue/white disc
const float LABEL_BASE = 165.0f;   // baseline of the ring lettering
const float LABEL_SCALE = 7.0f;    // pixels per font unit on the ring
const float LABEL_STROKE = 1.5f;   // ring lettering stroke thickness in font units
const float STROKE = 1.35f;        // stroke thickness in font units

struct RGBf {
    float r, g, b;
};

RGBf rgbf(uint32_t c) {
    return {float((c >> 16) & 0xFF), float((c >> 8) & 0xFF), float(c & 0xFF)};
}

RGBf mix(const RGBf &a, const RGBf &b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

RGBf scale(const RGBf &a, float f) {
    return {a.r * f, a.g * f, a.b * f};
}

inline float clamp01(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

inline uint8_t clamp255(float v) {
    return v < 0 ? 0 : (v > 255 ? 255 : uint8_t(v + 0.5f));
}

uint16_t to565(const RGBf &c) {
    uint8_t r = clamp255(c.r), g = clamp255(c.g), b = clamp255(c.b);
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t to565(uint32_t c) {
    return to565(rgbf(c));
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

// Brushed-metal look for the rims: lit from the upper left.
RGBf chrome(const RGBf &base, float fx, float fy, float r, float r0, float r1) {
    float a = atan2f(-fy, fx);  // maths angle, y up
    float light = cosf(a - 0.75f * PI_F);
    float across = (r - r0) / (r1 - r0);  // 0..1 across the rim
    float bevel = 1.0f - 0.35f * fabsf(across - 0.4f) * 2.0f;
    float f = (0.78f + 0.3f * light) * bevel;
    RGBf c = scale(base, f);
    if (light > 0.6f) c = mix(c, {255, 255, 255}, (light - 0.6f) * 0.6f * bevel);
    return c;
}

}  // namespace

bool Renderer::begin(AllocFn alloc) {
    const size_t px = size_t(W) * H;
    frame_ = static_cast<uint16_t *>(alloc(px * 2));
    layer_ = static_cast<uint16_t *>(alloc(px * 2));
    image_ = static_cast<uint16_t *>(alloc(px * 2));
    mask_ = static_cast<uint8_t *>(alloc(px));
    if (!frame_ || !layer_ || !image_ || !mask_) return false;
    memset(frame_, 0, px * 2);
    memset(image_, 0, px * 2);
    settingsDefaults(s_);
    dirty_ = true;
    return true;
}

void Renderer::apply(const Settings &s) {
    s_ = s;
    dirty_ = true;
}

void Renderer::startIntro(uint32_t ms) {
    introStart_ = ms ? ms : 1;
}

void Renderer::imageChanged(bool valid) {
    imageValid_ = valid;
    if (s_.mode == MODE_IMAGE) dirty_ = true;
}

float Renderer::introAngle(uint32_t ms) const {
    if (!introActive(ms)) return 0;
    float p = float(ms - introStart_) / INTRO_MS;
    float e = 1 - (1 - p) * (1 - p) * (1 - p);
    return 3 * 2 * PI_F * (e - 1);  // ends exactly on the resting angle
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
    const float margin = LABEL_STROKE;
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

    const RGBf black = {0, 0, 0};
    const RGBf rim = rgbf(s_.rim);
    const RGBf ring = rgbf(s_.ring);
    const RGBf label = rgbf(s_.label);
    const RGBf disc = mix(rgbf(s_.quadA), rgbf(s_.quadB), 0.5f);
    const float offset = s_.angle * PI_F / 180.0f;
    const float labelMid = LABEL_BASE + FONT_CAP_HEIGHT * LABEL_SCALE / 2;
    const float halfStroke = LABEL_STROKE * LABEL_SCALE / 2;

    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            float r = sqrtf(fx * fx + fy * fy);
            RGBf c = black;
            if (r < R_EDGE + 1) {
                c = mix(c, chrome(rim, fx, fy, r, R_RING, R_EDGE), inside(R_EDGE, r));
            }
            if (r < R_RING + 1) {
                RGBf ringPx = ring;
                if (r > LABEL_BASE - 8 && r < LABEL_BASE + FONT_CAP_HEIGHT * LABEL_SCALE + 8) {
                    float a = atan2f(fx, -fy) - offset;  // clockwise from the top
                    while (a > PI_F) a -= 2 * PI_F;
                    while (a < -PI_F) a += 2 * PI_F;
                    float u = a * labelMid / LABEL_SCALE + text_.width / 2;
                    float v = (r - LABEL_BASE) / LABEL_SCALE;
                    float d = distToLayout(text_, u, v) * LABEL_SCALE;
                    ringPx = mix(ring, label, clamp01(halfStroke + 0.5f - d));
                }
                c = mix(c, ringPx, inside(R_RING, r));
            }
            if (r < R_INNER_RIM + 1) {
                c = mix(c, chrome(rim, fx, fy, r, R_DISC, R_INNER_RIM), inside(R_INNER_RIM, r));
            }
            if (r < R_DISC + 1) {
                c = mix(c, disc, inside(R_DISC, r));
            }
            layer_[y * W + x] = to565(c);
        }
    }
}

// Quadrant disc, rotated clockwise by phi. Only touches pixels fully inside it.
void Renderer::drawDisc(float phi) {
    const uint16_t qa = to565(s_.quadA);
    const uint16_t qb = to565(s_.quadB);
    const float cs = cosf(phi), sn = sinf(phi);
    const float rr = R_DISC - 0.5f;

    for (int y = int(CY - R_DISC); y < int(CY + R_DISC); y++) {
        float fy = y + 0.5f - CY;
        float h2 = rr * rr - fy * fy;
        if (h2 <= 0) continue;
        float half = sqrtf(h2);
        int xs = int(ceilf(CX - 0.5f - half));
        int xe = int(floorf(CX - 0.5f + half));
        uint16_t *row = frame_ + y * W;
        for (int x = xs; x <= xe; x++) {
            float fx = x + 0.5f - CX;
            float u = fx * cs + fy * sn;
            float v = -fx * sn + fy * cs;
            bool a = (u * v) > 0;  // top-left / bottom-right
            float d = fminf(fabsf(u), fabsf(v));
            uint16_t own = a ? qa : qb;
            if (d < 0.5f) {
                row[x] = blend565(a ? qb : qa, own, int((0.5f + d) * 256));
            } else {
                row[x] = own;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Other modes

void Renderer::drawStripes(float offset, bool repeat) {
    const uint16_t colors[3] = {to565(s_.stripe1), to565(s_.stripe2), to565(s_.stripe3)};
    const RGBf bg = rgbf(s_.stripeBg);
    const uint16_t bgA = to565(bg);
    const uint16_t bgB = to565(mix(bg, {255, 255, 255}, 0.05f));
    const float w = 58.0f;
    const float period = 6 * w;
    const float beta = (s_.angle + 30) * PI_F / 180.0f;
    const float cs = cosf(beta), sn = sinf(beta);

    const float invW = 1 / w, invPeriod = 1 / period;

    // Bands 0..2 are the stripes; anything else is background. When
    // repeating, the pattern is 3 stripes followed by 3 stripe-widths of gap.
    auto colorOf = [&](int b, uint16_t bgc) -> uint16_t {
        if (repeat) b = (b + 6) % 6;
        return (b >= 0 && b < 3) ? colors[b] : bgc;
    };

    for (int y = 0; y < H; y++) {
        float fy = y + 0.5f - CY;
        uint16_t *row = frame_ + y * W;
        for (int x = 0; x < W; x++) {
            float fx = x + 0.5f - CX;
            // Carbon-fibre-ish weave for the background.
            uint16_t bgc = (((x >> 3) + (y >> 3)) & 1) ^ (((x + y) >> 2) & 1) ? bgA : bgB;
            float q = fx * cs + fy * sn - offset;
            float pos;
            if (repeat) {
                pos = q - floorf(q * invPeriod) * period;
            } else {
                pos = q + 1.5f * w;
                if (pos < -1 || pos > 3 * w + 1) {
                    row[x] = bgc;
                    continue;
                }
            }
            int b = int(floorf(pos * invW));
            float frac = pos - b * w;
            uint16_t c = colorOf(b, bgc);
            // Anti-alias the edges against the neighbouring band.
            if (frac < 0.5f) {
                c = blend565(colorOf(b - 1, bgc), c, int((0.5f + frac) * 256));
            } else if (frac > w - 0.5f) {
                c = blend565(colorOf(b + 1, bgc), c, int((w + 0.5f - frac) * 256));
            }
            row[x] = c;
        }
    }
}

void Renderer::drawSolid(float level) {
    uint16_t c = to565(scale(rgbf(s_.solid), level));
    for (int i = 0; i < W * H; i++) frame_[i] = c;
}

void Renderer::drawText() {
    const uint16_t bg = to565(s_.textBg);
    const uint16_t fg = to565(s_.textFg);

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
    float sc = 340.0f / (maxW + STROKE);
    if (sc > 300.0f / totalH) sc = 300.0f / totalH;
    if (sc > 20) sc = 20;
    const float half = STROKE * sc / 2;

    memset(mask_, 0, W * H);
    for (int i = 0; i < count; i++) {
        const TextLayout &t = layouts[i];
        const float ox = CX - t.width * sc / 2;
        // Baseline in pixels (screen y grows downwards).
        const float base = CY + totalH * sc / 2 - (count - 1 - i) * (FONT_CAP_HEIGHT + lineGap) * sc;
        for (int k = 0; k < t.segCount; k++) {
            const Seg &s = t.segs[k];
            float x0 = ox + s.x0 * sc, y0 = base - s.y0 * sc;
            float x1 = ox + s.x1 * sc, y1 = base - s.y1 * sc;
            int bx0 = int(fminf(x0, x1) - half - 2), bx1 = int(fmaxf(x0, x1) + half + 2);
            int by0 = int(fminf(y0, y1) - half - 2), by1 = int(fmaxf(y0, y1) + half + 2);
            if (bx0 < 0) bx0 = 0;
            if (by0 < 0) by0 = 0;
            if (bx1 >= W) bx1 = W - 1;
            if (by1 >= H) by1 = H - 1;
            for (int y = by0; y <= by1; y++) {
                for (int x = bx0; x <= bx1; x++) {
                    float d = sqrtf(segDist2(x + 0.5f, y + 0.5f, x0, y0, x1, y1));
                    uint8_t cov = uint8_t(clamp01(half + 0.5f - d) * 255);
                    uint8_t &m = mask_[y * W + x];
                    if (cov > m) m = cov;
                }
            }
        }
    }
    for (int i = 0; i < W * H; i++) {
        uint8_t m = mask_[i];
        frame_[i] = m == 0 ? bg : (m == 255 ? fg : blend565(bg, fg, m + 1));
    }
}

void Renderer::drawImage() {
    if (imageValid_) {
        memcpy(frame_, image_, size_t(W) * H * 2);
        return;
    }
    Settings keep = s_;
    s_.textBg = 0x000000;
    s_.textFg = 0x606060;
    strcpy(s_.text, "NO|IMAGE");
    drawText();
    s_ = keep;
}

// ---------------------------------------------------------------------------

Rect Renderer::render(uint32_t ms) {
    const Rect full = {0, 0, W, H};
    const Rect disc = {int16_t(CX - R_DISC), int16_t(CY - R_DISC), int16_t(2 * R_DISC), int16_t(2 * R_DISC)};
    float dt = lastMs_ ? (ms - lastMs_) / 1000.0f : 0;
    if (dt > 0.2f) dt = 0.2f;  // don't jump after a stall
    lastMs_ = ms;
    const bool wasDirty = dirty_;
    dirty_ = false;
    const float offset = s_.angle * PI_F / 180.0f;

    switch (s_.mode) {
        case MODE_ROUNDEL:
        case MODE_SPIN: {
            bool intro = introActive(ms);
            if (s_.mode == MODE_SPIN) phase_ += dt * (s_.speed / 100.0f) * 2 * PI_F;
            bool introDone = introStart_ && !intro;  // draw the resting frame once more
            if (introDone) introStart_ = 0;
            bool moving = intro || introDone || (s_.mode == MODE_SPIN && s_.speed != 0);
            if (!wasDirty && !moving) return {0, 0, 0, 0};
            if (wasDirty) {
                buildRoundelLayer();
                memcpy(frame_, layer_, size_t(W) * H * 2);
            }
            float phi = offset + introAngle(ms) + (s_.mode == MODE_SPIN ? phase_ : 0);
            drawDisc(phi);
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
        case MODE_SOLID:
            if (s_.speed != 0) {
                phase_ += dt * (fabsf(s_.speed) / 100.0f) * 1.5f * 2 * PI_F;
                drawSolid(0.25f + 0.75f * (0.5f + 0.5f * sinf(phase_)));
                return full;
            }
            if (!wasDirty) return {0, 0, 0, 0};
            drawSolid(1);
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
