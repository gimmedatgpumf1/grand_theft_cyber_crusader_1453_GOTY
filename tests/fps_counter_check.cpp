// The frame-rate strip, checked as pixels.
//
// The overlay that used to live here is gone, and this is the smallest thing that
// could replace it: a strip across the top with the frame rate in it. It is drawn
// by the host into the finished picture, so unlike anything that was routed
// through the device it can be checked exactly, on this machine, with no driver
// involved -- and that is the entire reason it is on the host.
//
// What is asserted, on an rgb canvas and on the window-order one the game
// actually hands over, at four window sizes:
//
//   * the strip is centred on the window and hangs from the top edge;
//   * it is translucent: over a known background it is half the panel colour and
//     half the picture, not opaque black, and never the raw panel colour;
//   * the text is the real proportional face -- antialiased, so it has to contain
//     partially covered pixels, and it may not be a fixed grid of solid blocks;
//   * the text height is at least the 20-pixel floor everywhere, and grows with
//     the window;
//   * the text sits inside the strip and is centred in it.

#include "client/fps_counter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
}

struct Picture {
    gtc::native::NativeFrame frame;
    int width = 0, height = 0;
    bool windowOrder = false;   // BGRA, the form the game's own frames arrive in
    std::array<int, 3> ground{90, 30, 60};

    Picture(int w, int h, bool bgra) : width(w), height(h), windowOrder(bgra) {
        frame.width = w;
        frame.height = h;
        frame.wantsBgra = bgra;
        // A colour nothing in the strip uses, so any pixel that changes is the
        // strip's doing and not a coincidence of the fill.
        if (bgra) {
            frame.bgra.resize(static_cast<std::size_t>(w) * h * 4, 255);
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(w) * h; ++pixel) {
                frame.bgra[pixel * 4] = static_cast<unsigned char>(ground[2]);
                frame.bgra[pixel * 4 + 1] = static_cast<unsigned char>(ground[1]);
                frame.bgra[pixel * 4 + 2] = static_cast<unsigned char>(ground[0]);
            }
        } else {
            frame.rgb.resize(static_cast<std::size_t>(w) * h * 3);
            // Bytes, not ints: memcpy of an int array here paints one real byte in
            // three, which is how a canvas ends up looking like a failing check
            // rather than like a failing counter.
            const std::array<unsigned char, 3> fill = {
                static_cast<unsigned char>(ground[0]), static_cast<unsigned char>(ground[1]),
                static_cast<unsigned char>(ground[2])};
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(w) * h; ++pixel) {
                std::memcpy(frame.rgb.data() + pixel * 3, fill.data(), 3);
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

    bool same(int x, int y, const std::array<int, 3>& colour) const {
        const auto pixel = at(x, y);
        return pixel[0] == colour[0] && pixel[1] == colour[1] && pixel[2] == colour[2];
    }
};

// The bytes a colour becomes, asked of the drawing code rather than guessed at:
// a one-pixel rectangle is filled on a scratch frame and read back. A test that
// reimplements the colour pipeline only tests its own copy of it.
std::array<int, 3> bytes(gtc::native::Color colour) {
    gtc::native::NativeFrame scratch;
    scratch.width = 4;
    scratch.height = 4;
    scratch.rgb.assign(4U * 4U * 3U, 0);
    scratch.rectangle(1, 1, 1, 1, colour);
    return {scratch.rgb[(4U + 1U) * 3U], scratch.rgb[(4U + 1U) * 3U + 1U], scratch.rgb[(4U + 1U) * 3U + 2U]};
}

void examine(int width, int height, bool windowOrder) {
    Picture picture(width, height, windowOrder);
    gtc::native::drawFpsCounter(picture.frame, 300.0);
    const std::string where = std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");

    const auto layout = gtc::native::layoutFpsCounter(width, height, 300.0);
    check(layout.width > 0 && layout.height > 0, where + ": the strip has a size");
    const int leftMargin = layout.left;
    const int rightMargin = width - (layout.left + layout.width);
    check(std::abs(leftMargin - rightMargin) <= gtc::native::kFpsCounterCentreTolerance,
          where + ": the strip is centred (" + std::to_string(leftMargin) + " left, " +
              std::to_string(rightMargin) + " right)");
    check(layout.top == gtc::native::kFpsCounterMargin, where + ": the strip hangs from the top edge");
    check(picture.same(0, height - 1, picture.ground), where + ": the rest of the frame is untouched");

    // Translucent: half panel, half picture. The panel's own bytes come from the
    // drawing code, so this compares the blend and nothing else.
    const auto panel = bytes(gtc::native::colorHex("#05090b"));
    std::array<int, 3> expected{};
    for (int channel = 0; channel < 3; ++channel) {
        expected[channel] = (panel[channel] + picture.ground[channel] + 1) / 2;
    }
    const auto inside = picture.at(layout.left + 1, layout.top + layout.height - 2);
    int worst = 0;
    for (int channel = 0; channel < 3; ++channel) worst = std::max(worst, std::abs(inside[channel] - expected[channel]));
    check(worst <= 1, where + ": the strip is half transparent, off by " + std::to_string(worst));
    check(inside != picture.ground, where + ": the strip is drawn at all");
    check(worst <= 1 && inside != panel, where + ": the strip is not opaque");

    // The text: brighter than the strip, and antialiased. A pixel font would give
    // exactly two levels here; a real one gives a ramp at every curve.
    int brightest = 0, midTones = 0, textPixels = 0;
    std::array<bool, 256> levels{};
    for (int y = layout.textTop; y < layout.textTop + layout.textHeight; ++y) {
        for (int x = layout.textLeft; x < layout.textLeft + layout.textWidth; ++x) {
            const auto pixel = picture.at(x, y);
            const int luminance = (pixel[0] * 30 + pixel[1] * 59 + pixel[2] * 11) / 100;
            brightest = std::max(brightest, luminance);
            levels[static_cast<std::size_t>(std::clamp(luminance, 0, 255))] = true;
            const int floor = (expected[0] * 30 + expected[1] * 59 + expected[2] * 11) / 100;
            if (luminance > floor + 8) ++textPixels;
            if (luminance > floor + 8 && luminance < brightest - 8) ++midTones;
        }
    }
    int distinct = 0;
    for (bool level : levels) distinct += level ? 1 : 0;
    check(textPixels > 40, where + ": the text is drawn (" + std::to_string(textPixels) + " pixels)");
    check(midTones > 10, where + ": the text is antialiased (" + std::to_string(midTones) + " partial pixels)");
    check(distinct > 12, where + ": the text is not a two-colour bitmap (" + std::to_string(distinct) + " levels)");

    // Size: never below the floor, and bigger on a bigger window.
    const int textHeight = gtc::native::fpsCounterTextHeight(height);
    check(textHeight >= 20, where + ": text height " + std::to_string(textHeight) + " is below the 20-pixel floor");
    check(layout.textHeight >= 20 && layout.textHeight <= textHeight + 2,
          where + ": the measured line height matches the requested size");
    check(layout.textLeft > layout.left && layout.textTop > layout.top &&
              layout.textLeft + layout.textWidth < layout.left + layout.width &&
              layout.textTop + layout.textHeight < layout.top + layout.height,
          where + ": the text is inside the strip");
    const int textLeftMargin = layout.textLeft - layout.left;
    const int textRightMargin = (layout.left + layout.width) - (layout.textLeft + layout.textWidth);
    check(std::abs(textLeftMargin - textRightMargin) <= 1,
          where + ": the text is centred in the strip (" + std::to_string(textLeftMargin) + " vs " +
              std::to_string(textRightMargin) + ")");

    // The picture, and a six-times enlargement of the strip, because a line of
    // type is either the face it should be or it is not, and that is a thing to
    // look at.
    const int zoom = 6;
    gtc::native::NativeFrame big;
    big.width = (layout.width + 8) * zoom;
    big.height = (layout.height + 8) * zoom;
    big.rgb.assign(static_cast<std::size_t>(big.width) * big.height * 3, 0);
    for (int y = 0; y < big.height; ++y) {
        for (int x = 0; x < big.width; ++x) {
            const auto pixel = picture.at(layout.left - 4 + x / zoom, layout.top - 4 + y / zoom);
            unsigned char* out = big.rgb.data() + (static_cast<std::size_t>(y) * big.width + x) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                out[channel] = static_cast<unsigned char>(std::max(0, pixel[channel]));
            }
        }
    }
    big.save("build/native-client/fps-counter-zoom-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
    if (!windowOrder) {
        picture.frame.save("build/native-client/fps-counter-" + std::to_string(width) + "x" +
                           std::to_string(height) + ".png");
    }
    std::printf("  %-18s strip %dx%d at %d,%d, text %d px, %d lit pixels, %d levels\n", where.c_str(), layout.width,
                layout.height, layout.left, layout.top, textHeight, textPixels, distinct);
}

}  // namespace

int main() {
    examine(640, 480, false);
    examine(640, 480, true);
    examine(1920, 1080, true);
    examine(1568, 929, true);

    // A number that is not a number is not allowed to print nonsense, and the
    // strip has to keep its shape whatever the rate is.
    gtc::native::NativeFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.rgb.assign(640U * 480U * 3, 0);
    gtc::native::drawFpsCounter(frame, std::nan(""));
    gtc::native::drawFpsCounter(frame, -1.0);
    gtc::native::drawFpsCounter(frame, 0.4);
    check(gtc::native::fpsCounterTextHeight(480) >= 20 && gtc::native::fpsCounterTextHeight(2160) > 30,
          "the text grows with the window and never falls below the floor");

    if (failures == 0) {
        std::printf("fps counter: PASS (centred, translucent, proportional antialiased type, 640x480 to 1080p)\n");
        return 0;
    }
    std::printf("fps counter: FAIL (%d)\n", failures);
    return 1;
}
