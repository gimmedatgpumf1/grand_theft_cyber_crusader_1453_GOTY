#include "client/fps_counter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace gtc::native {
namespace {

// Seven rows of five bits. A digit is drawn as rectangles, one per lit bit, so
// the counter needs no image, no sampler, no atlas and no antialiasing -- three
// colours and a loop. It cannot come out soft, because there is nothing between
// the screen and the fill.
using Glyph = std::array<unsigned char, 7>;

const Glyph& glyphFor(char character) {
    static const Glyph zero = {14, 17, 17, 17, 17, 17, 14};
    static const Glyph one = {4, 12, 4, 4, 4, 4, 14};
    static const Glyph two = {14, 17, 1, 2, 4, 8, 31};
    static const Glyph three = {31, 2, 4, 2, 1, 17, 14};
    static const Glyph four = {2, 6, 10, 18, 31, 2, 2};
    static const Glyph five = {31, 16, 30, 1, 1, 17, 14};
    static const Glyph six = {6, 8, 16, 30, 17, 17, 14};
    static const Glyph seven = {31, 1, 2, 4, 8, 8, 8};
    static const Glyph eight = {14, 17, 17, 14, 17, 17, 14};
    static const Glyph nine = {14, 17, 17, 15, 1, 2, 12};
    static const Glyph space = {0, 0, 0, 0, 0, 0, 0};
    static const Glyph f = {31, 16, 16, 30, 16, 16, 16};
    static const Glyph p = {30, 17, 17, 30, 16, 16, 16};
    static const Glyph s = {15, 16, 16, 14, 1, 1, 30};
    static const Glyph dot = {0, 0, 0, 0, 0, 12, 12};
    static const Glyph dash = {0, 0, 0, 31, 0, 0, 0};
    switch (character) {
        case '0': return zero;
        case '1': return one;
        case '2': return two;
        case '3': return three;
        case '4': return four;
        case '5': return five;
        case '6': return six;
        case '7': return seven;
        case '8': return eight;
        case '9': return nine;
        case 'F': return f;
        case 'P': return p;
        case 'S': return s;
        case '.': return dot;
        case '-': return dash;
        default: return space;
    }
}

// One pixel of the logical grid, which is `scale` pixels of the real screen.
void cell(NativeFrame& frame, int x, int y, int scale, Color color) {
    frame.rectangle(x, y, scale, scale, color);
}

void drawWord(NativeFrame& frame, int x, int y, int scale, const std::string& text, Color color) {
    const int advance = 6 * scale;
    for (char character : text) {
        if (character == ' ') {
            x += advance;
            continue;
        }
        // 'F' is the only letter the counter needs, and it is drawn from the same
        // table as the digits so there is one thing to look at when a glyph is
        // wrong.
        const Glyph& rows = glyphFor(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) != 0) {
                    cell(frame, x + column * scale, y + row * scale, scale, color);
                }
            }
        }
        x += advance;
    }
}

int textWidth(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * 6 * scale - scale;
}

}  // namespace

void drawFpsCounter(NativeFrame& frame, double framesPerSecond) {
    // The counter scales with the window instead of sitting at a fixed pixel
    // size: at 640x480 it is small, at 1080p it is still readable without being
    // the subject. The transform is the frame's own, so the fills go through the
    // same scaled rectangle as everything else -- and it is put back afterwards,
    // because a drawing call has no business leaving the frame in another space.
    const double previousScale = frame.uiScale;
    const double previousOffsetX = frame.uiOffsetX;
    const double previousOffsetY = frame.uiOffsetY;
    frame.uiScale = std::clamp(frame.height / 480.0, 1.0, 3.0);
    frame.uiOffsetX = 0.0;
    frame.uiOffsetY = 0.0;
    struct Restore {
        NativeFrame& frame;
        double scale, x, y;
        ~Restore() {
            frame.uiScale = scale;
            frame.uiOffsetX = x;
            frame.uiOffsetY = y;
        }
    } restore{frame, previousScale, previousOffsetX, previousOffsetY};
    const int scale = kFpsCounterDigit;
    if (!std::isfinite(framesPerSecond) || framesPerSecond < 0.0) framesPerSecond = 0.0;
    // Rounded, not truncated: a frame rate that reads nineteen while the machine
    // reports twenty is the sort of small lie that makes a counter useless.
    char number[32];
    std::snprintf(number, sizeof(number), "%.0f", framesPerSecond);
    const std::string text = std::string("FPS ") + number;

    const Color back = colorHex("#0b110e");
    const Color edge = colorHex("#d7ff54");
    const Color ink = colorHex("#d7ff54");
    const int padding = 8 * scale / 3;
    const int height = 7 * scale;
    const int width = textWidth(text, scale);
    const int x = kFpsCounterX, y = kFpsCounterY;

    frame.rectangle(x, y, width + padding * 2, height + padding * 2, back);
    // A one-logical-pixel frame, so the box has an edge that holds its shape at
    // every window size instead of dissolving into the world behind it.
    const int border = std::max(1, scale / 3);
    frame.rectangle(x, y, width + padding * 2, border, edge);
    frame.rectangle(x, y + height + padding * 2 - border, width + padding * 2, border, edge);
    frame.rectangle(x, y, border, height + padding * 2, edge);
    frame.rectangle(x + width + padding * 2 - border, y, border, height + padding * 2, edge);
    drawWord(frame, x + padding, y + padding, scale, text, ink);
}

}  // namespace gtc::native
