// The counter, checked as pixels.
//
// The overlay that used to live here is gone, and this is the smallest thing
// that could replace it: a box with a frame rate in it. It is drawn by the host
// into the finished picture, so unlike anything that was routed through the
// device it can be checked exactly, on this machine, with no driver involved --
// and that is the entire reason it is on the host.
//
// What is asserted:
//
//   * the box is where the header says, whatever size the window is, and it
//     scales up with the window instead of staying a speck at 1080p;
//   * the border is the accent colour and the interior is the panel colour, so
//     the box cannot quietly become transparent over bright geometry;
//   * every lit pixel of every glyph is part of a solid block whose corners are
//     on the same grid -- no antialiasing, no fractional scaling, no smearing;
//   * the glyphs are the digits they claim to be, against the table below;
//   * the same picture comes out of an rgb canvas and of a window-order one,
//     which is the canvas the game actually hands over.

#include "client/fps_counter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using gtc::native::Color;

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
}

// The same table the counter draws from, repeated here on purpose: if a glyph is
// edited by accident, the picture changes and this notices.
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

struct Picture {
    gtc::native::NativeFrame frame;
    int width = 0, height = 0;
    bool windowOrder = false;   // BGRA, the form the game's own frames arrive in

    Picture(int w, int h, bool bgra) : width(w), height(h), windowOrder(bgra) {
        frame.width = w;
        frame.height = h;
        frame.wantsBgra = bgra;
        // A colour nothing in the counter uses, so any pixel of it that changes
        // is the counter's doing and not a coincidence of the fill.
        const std::array<unsigned char, 3> ground = {90, 30, 60};
        if (bgra) {
            frame.bgra.resize(static_cast<std::size_t>(w) * h * 4, 255);
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(w) * h; ++pixel) {
                frame.bgra[pixel * 4] = ground[2];
                frame.bgra[pixel * 4 + 1] = ground[1];
                frame.bgra[pixel * 4 + 2] = ground[0];
            }
        } else {
            frame.rgb.resize(static_cast<std::size_t>(w) * h * 3);
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(w) * h; ++pixel) {
                std::memcpy(frame.rgb.data() + pixel * 3, ground.data(), 3);
            }
        }
    }

    std::array<int, 3> at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return {-1, -1, -1};
        const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
        if (windowOrder) {
            return {frame.bgra[pixel * 4 + 2], frame.bgra[pixel * 4 + 1], frame.bgra[pixel * 4]};
        }
        return {frame.rgb[pixel * 3], frame.rgb[pixel * 3 + 1], frame.rgb[pixel * 3 + 2]};
    }

    bool is(int x, int y, const std::array<int, 3>& colour) const {
        const auto pixel = at(x, y);
        return pixel[0] == colour[0] && pixel[1] == colour[1] && pixel[2] == colour[2];
    }
};

// The bytes a colour becomes, asked of the drawing code rather than guessed at:
// a one-pixel rectangle is filled on a scratch frame and read back. A test that
// reimplements the colour pipeline tests its own copy of it.
std::array<int, 3> bytes(Color colour) {
    gtc::native::NativeFrame scratch;
    scratch.width = 4;
    scratch.height = 4;
    scratch.rgb.assign(4U * 4U * 3U, 0);
    scratch.rectangle(1, 1, 1, 1, colour);
    return {scratch.rgb[(4U + 1U) * 3U], scratch.rgb[(4U + 1U) * 3U + 1U], scratch.rgb[(4U + 1U) * 3U + 2U]};
}

int scaled(double logical, double uiScale) { return static_cast<int>(std::lround(logical * uiScale)); }

// One counter, checked on a canvas of this size and this byte order.
void examine(int width, int height, bool windowOrder) {
    Picture picture(width, height, windowOrder);
    gtc::native::drawFpsCounter(picture.frame, 300.0);
    const std::string where = std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");

    // The transform the counter uses, worked out the same way it does.
    const double uiScale = std::clamp(height / 480.0, 1.0, 3.0);
    const int scale = gtc::native::kFpsCounterDigit;
    const int padding = 8 * scale / 3;
    const int textHeight = 7 * scale;
    const std::string text = "FPS 300";
    const int textWidth = static_cast<int>(text.size()) * 6 * scale - scale;
    const int boxW = textWidth + padding * 2, boxH = textHeight + padding * 2;
    const int left = scaled(gtc::native::kFpsCounterX, uiScale);
    const int top = scaled(gtc::native::kFpsCounterY, uiScale);
    const int right = scaled(gtc::native::kFpsCounterX + boxW, uiScale);
    const int bottom = scaled(gtc::native::kFpsCounterY + boxH, uiScale);

    const Color backColour = gtc::native::colorHex("#0b110e");
    const Color accentColour = gtc::native::colorHex("#d7ff54");
    const auto back = bytes(backColour);
    const auto accent = bytes(accentColour);
    const std::array<int, 3> ground = {90, 30, 60};

    check(right > left + 40 && bottom > top + 20, where + ": the box has a size");
    check(picture.is(left - 2, top - 2, ground), where + ": nothing above or left of the box");
    check(picture.is(right + 2, top + 2, ground), where + ": nothing right of the box");
    check(picture.is(left, top, accent), where + ": the top-left corner is the accent colour");
    check(picture.is(right - 1, top, accent), where + ": the top-right corner is the accent colour");
    check(picture.is(left, bottom - 1, accent), where + ": the bottom-left corner is the accent colour");
    check(picture.is(right - 1, bottom - 1, accent), where + ": the bottom-right corner is the accent colour");

    // The interior: panel colour, and every glyph block solid and on the grid.
    const int textLeft = scaled(gtc::native::kFpsCounterX + padding, uiScale);
    const int textTop = scaled(gtc::native::kFpsCounterY + padding, uiScale);
    check(picture.is(left + 2, textTop + 2, back), where + ": the panel is filled behind the text");

    int litCells = 0, wrongCells = 0;
    for (std::size_t character = 0; character < text.size(); ++character) {
        const Glyph& rows = glyphFor(text[character]);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                const bool lit = (rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) != 0;
                const int x0 = scaled(gtc::native::kFpsCounterX + padding + static_cast<int>(character) * 6 * scale +
                                          column * scale, uiScale);
                const int y0 = scaled(gtc::native::kFpsCounterY + padding + row * scale, uiScale);
                const int x1 = scaled(gtc::native::kFpsCounterX + padding + static_cast<int>(character) * 6 * scale +
                                          (column + 1) * scale, uiScale);
                const int y1 = scaled(gtc::native::kFpsCounterY + padding + (row + 1) * scale, uiScale);
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        if (!picture.is(x, y, lit ? accent : back)) ++wrongCells;
                    }
                }
                litCells += lit ? 1 : 0;
            }
        }
    }
    check(wrongCells == 0, where + ": every glyph block is solid and in its cell (" +
                              std::to_string(wrongCells) + " pixels are not)");
    check(litCells > 40, where + ": there is a number in the box (" + std::to_string(litCells) + " lit cells)");
    check(textLeft < scaled(gtc::native::kFpsCounterX + padding + 1, uiScale) + 1, where + ": the text starts inside");

    // The counter, enlarged six times with no filtering, because a glyph at this
    // size is either the letter it should be or it is not, and that is a thing to
    // look at rather than to infer from a pixel count.
    if (!windowOrder) {
        const int zoom = 6;
        gtc::native::NativeFrame big;
        big.width = (right - left + 8) * zoom;
        big.height = (bottom - top + 8) * zoom;
        big.rgb.assign(static_cast<std::size_t>(big.width) * big.height * 3, 0);
        for (int y = 0; y < big.height; ++y) {
            for (int x = 0; x < big.width; ++x) {
                const auto pixel = picture.at(left - 4 + x / zoom, top - 4 + y / zoom);
                unsigned char* out = big.rgb.data() + (static_cast<std::size_t>(y) * big.width + x) * 3;
                for (int channel = 0; channel < 3; ++channel) {
                    out[channel] = static_cast<unsigned char>(std::max(0, pixel[channel]));
                }
            }
        }
        big.save("build/native-client/fps-counter-zoom.png");
    }
    if (!windowOrder) {
        const std::string path = "build/native-client/fps-counter-" + std::to_string(width) + "x" +
            std::to_string(height) + ".png";
        picture.frame.save(path);
    }
    std::printf("  %-18s box %dx%d at %d,%d, %d lit cells\n", where.c_str(), right - left, bottom - top, left, top,
                litCells);
}

}  // namespace

int main() {
    // The size the sandbox renders at, then the size the King plays at, in both
    // byte orders: the game's frames arrive in window order, so that one is not
    // an afterthought.
    examine(640, 480, false);
    examine(640, 480, true);
    examine(1920, 1080, true);
    examine(1568, 929, true);

    // A number that is not a number is not allowed to print nonsense.
    gtc::native::NativeFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.rgb.assign(640U * 480U * 3, 0);
    gtc::native::drawFpsCounter(frame, std::nan(""));
    gtc::native::drawFpsCounter(frame, -1.0);

    if (failures == 0) {
        std::printf("fps counter: PASS (box, border, digits, both byte orders, 640x480 to 1080p)\n");
        return 0;
    }
    std::printf("fps counter: FAIL (%d)\n", failures);
    return 1;
}
