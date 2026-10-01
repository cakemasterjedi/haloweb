#pragma once
// Tiny geometric stroke font. Glyphs are polylines on a grid with the
// baseline at y=0 and cap height at y=8 (y points up). Each stroke is a run
// of "xy" digit pairs; strokes are separated by spaces. A single point is a
// dot. Rendering them as thick anti-aliased lines lets the font be drawn at
// any size (text mode and messages).
#include <stdint.h>

struct StrokeGlyph {
    char c;
    uint8_t width;
    const char *strokes;
};

static const uint8_t FONT_CAP_HEIGHT = 8;

static const StrokeGlyph STROKE_FONT[] = {
    {' ', 3, ""},
    {'A', 5, "000628385650 0454"},
    {'B', 5, "000848575544 044453514000"},
    {'C', 5, "5748180701104051"},
    {'D', 5, "00083856523000"},
    {'E', 5, "58080050 0444"},
    {'F', 5, "580800 0444"},
    {'G', 5, "57481807011040515434"},
    {'H', 5, "0008 5058 0454"},
    {'I', 2, "0020 1018 0828"},
    {'J', 5, "585140100102"},
    {'K', 5, "0008 5803 2550"},
    {'L', 5, "080050"},
    {'M', 6, "0008346860"},
    {'N', 5, "00085058"},
    {'O', 5, "100107184857514010"},
    {'P', 5, "00084857554404"},
    {'Q', 5, "100107184857514010 3250"},
    {'R', 5, "00084857554404 2450"},
    {'S', 5, "574818070514445351401001"},
    {'T', 4, "0848 2820"},
    {'U', 5, "080110405158"},
    {'V', 4, "082048"},
    {'W', 6, "0810365068"},
    {'X', 5, "0058 0850"},
    {'Y', 4, "082448 2420"},
    {'Z', 5, "08580050"},
    {'0', 5, "100107184857514010"},
    {'1', 4, "062820 0040"},
    {'2', 5, "07184857550050"},
    {'3', 5, "07184857554424 445351401001"},
    {'4', 5, "40480252"},
    {'5', 5, "5808044453514000"},
    {'6', 5, "5748180701104051534404"},
    {'7', 5, "085810"},
    {'8', 5, "14050718485755441403011040515344"},
    {'9', 5, "0110405157481807051454"},
    {'-', 4, "0444"},
    {'.', 0, "00"},
    {'!', 0, "0803 00"},
    {'+', 4, "0444 2226"},
    {'/', 4, "0048"},
    {'\'', 0, "0806"},
    {':', 0, "02 06"},
    {'?', 4, "0718384745342322 20"},
};

static const int STROKE_FONT_COUNT = sizeof(STROKE_FONT) / sizeof(STROKE_FONT[0]);

inline const StrokeGlyph *strokeGlyph(char c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    for (int i = 0; i < STROKE_FONT_COUNT; i++) {
        if (STROKE_FONT[i].c == c) return &STROKE_FONT[i];
    }
    return &STROKE_FONT[0];
}
