// What the interface checks share: the scratch picture, the colour arithmetic read
// back out of the drawing code, the text survey, and the counter that failures add
// to. Everything here is inline so every check file can include it, and the one
// thing that must exist exactly once -- the counter -- is declared extern.
//
// The checks live in interface_check.cpp (the run order), and one file per part:
// the strip and the reticle in interface_check_strip.cpp, the cheat menu and the
// mode bar in interface_check_menu.cpp, the overlays, the circle and the needs
// panel in interface_check_panels.cpp.

#include "client/cheat_menu.h"
#include "client/interface_style.h"
#include "client/mode_bar.h"
#include "client/status_panel.h"
#include "client/target_prompt.h"
#include "client/ui_layer.h"

#include "client/interaction_wheel.h"
#include "client/overlay_panel.h"
#include "client/crosshair.h"
#include "client/fps_counter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


namespace interface_check {

// One counter for the whole check; defined once, in interface_check.cpp.
extern int failures;

inline void check(bool condition, const std::string& what) {
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
        // A colour nothing in the interface uses, so any pixel that changes is
        // the interface's doing and not a coincidence of the fill.
        if (bgra) {
            frame.bgra.resize(static_cast<std::size_t>(w) * h * 4, 255);
            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(w) * h; ++pixel) {
                frame.bgra[pixel * 4] = static_cast<unsigned char>(ground[2]);
                frame.bgra[pixel * 4 + 1] = static_cast<unsigned char>(ground[1]);
                frame.bgra[pixel * 4 + 2] = static_cast<unsigned char>(ground[0]);
            }
        } else {
            frame.rgb.resize(static_cast<std::size_t>(w) * h * 3);
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

    bool untouched(int x, int y) const { return same(x, y, ground); }
};

// The bytes a colour becomes, asked of the drawing code rather than guessed at:
// a one-pixel rectangle is filled on a scratch frame and read back. A test that
// reimplements the colour pipeline only tests its own copy of it.
inline std::array<int, 3> bytes(gtc::native::Color colour) {
    gtc::native::NativeFrame scratch;
    scratch.width = 4;
    scratch.height = 4;
    scratch.rgb.assign(4U * 4U * 3U, 0);
    scratch.rectangle(1, 1, 1, 1, colour);
    return {scratch.rgb[(4U + 1U) * 3U], scratch.rgb[(4U + 1U) * 3U + 1U], scratch.rgb[(4U + 1U) * 3U + 2U]};
}

// The same arithmetic the drawing uses: source over destination, rounded down,
// with the opacity as a byte. Kept here as a formula rather than as a copy of the
// code path, so a wrong blend shows up as a wrong picture.
inline std::array<int, 3> blendOver(const std::array<int, 3>& colour, const std::array<int, 3>& under,
                             double opacity = gtc::native::kPanelOpacity) {
    const int alpha = static_cast<int>(std::clamp(opacity, 0.0, 1.0) * 255);
    return {(colour[0] * alpha + under[0] * (255 - alpha)) / 255,
            (colour[1] * alpha + under[1] * (255 - alpha)) / 255,
            (colour[2] * alpha + under[2] * (255 - alpha)) / 255};
}

inline int worstChannel(const std::array<int, 3>& a, const std::array<int, 3>& b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}

// Counts the pixels inside a box that are clearly brighter than what is under
// them, and how many distinct luminance levels they span. A pixel font gives two
// levels; the real face gives a ramp at every curve.
struct TextSurvey {
    int lit = 0;
    int levels = 0;
    int brightest = 0;
};

inline TextSurvey survey(const Picture& picture, int left, int top, int width, int height,
                  const std::array<int, 3>& under) {
    const int floorLuminance = (under[0] * 30 + under[1] * 59 + under[2] * 11) / 100;
    std::array<bool, 256> seen{};
    TextSurvey result;
    for (int y = top; y < top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
            const auto pixel = picture.at(x, y);
            if (pixel[0] < 0) continue;
            const int luminance = (pixel[0] * 30 + pixel[1] * 59 + pixel[2] * 11) / 100;
            seen[static_cast<std::size_t>(std::clamp(luminance, 0, 255))] = true;
            result.brightest = std::max(result.brightest, luminance);
            if (luminance > floorLuminance + 8) ++result.lit;
        }
    }
    for (bool level : seen) result.levels += level ? 1 : 0;
    return result;
}

inline void zoom(Picture& picture, int left, int top, int width, int height, int zoomFactor, const std::string& path) {
    gtc::native::NativeFrame big;
    big.width = (width + 8) * zoomFactor;
    big.height = (height + 8) * zoomFactor;
    big.rgb.assign(static_cast<std::size_t>(big.width) * big.height * 3, 0);
    for (int y = 0; y < big.height; ++y) {
        for (int x = 0; x < big.width; ++x) {
            const auto pixel = picture.at(left - 4 + x / zoomFactor, top - 4 + y / zoomFactor);
            unsigned char* out = big.rgb.data() + (static_cast<std::size_t>(y) * big.width + x) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                out[channel] = static_cast<unsigned char>(std::max(0, pixel[channel]));
            }
        }
    }
    big.save(path);
}

// The checks themselves, one file per part of the interface. Each is handed a
// window size and the byte order the frame arrives in, and looks at pixels.
void examineStrip(int width, int height, bool windowOrder);
void examineCrosshair(int width, int height, bool windowOrder);
void examineTargetPrompt(int width, int height, bool windowOrder);
void examineCheatMenu(int width, int height, bool windowOrder);
void examineModeBar(int width, int height, bool windowOrder);
void examineOverlayPanels(int width, int height, bool windowOrder);
void examineInteractionWheel(int width, int height, bool windowOrder);
void examineNeedsPanel(int width, int height, bool windowOrder);

}  // namespace interface_check
