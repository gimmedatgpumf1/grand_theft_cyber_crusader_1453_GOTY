// The interface, checked as pixels.
//
// The overlay that used to live here was deleted and is being rebuilt one piece
// at a time. What exists now is the frame-rate strip, the reticle and the F1 need
// controls. All three are drawn by the host into the finished picture, so unlike
// anything routed through the device they can be checked exactly, on this
// machine, with no driver involved -- and that is the entire reason they are on
// the host.
//
// What is asserted:
//
//   * the strip is centred, translucent, and set in the real antialiased face;
//   * the reticle is at the middle of the window, is drawn in the reading colour
//     with a dark outline, has a gap around the centre, and is absent in god view;
//   * the need panel is centred, translucent, fits inside a 480-line frame, its
//     four rows do not collide, its two columns do not collide, and it draws the
//     values it was handed rather than last round's;
//   * the panel is drawn over the reticle, in the order the loop draws them.

#include "client/cheat_menu.h"
#include "client/interface_style.h"
#include "client/mode_bar.h"
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
std::array<int, 3> bytes(gtc::native::Color colour) {
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
std::array<int, 3> blendOver(const std::array<int, 3>& colour, const std::array<int, 3>& under,
                             double opacity = gtc::native::kPanelOpacity) {
    const int alpha = static_cast<int>(std::clamp(opacity, 0.0, 1.0) * 255);
    return {(colour[0] * alpha + under[0] * (255 - alpha)) / 255,
            (colour[1] * alpha + under[1] * (255 - alpha)) / 255,
            (colour[2] * alpha + under[2] * (255 - alpha)) / 255};
}

int worstChannel(const std::array<int, 3>& a, const std::array<int, 3>& b) {
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

TextSurvey survey(const Picture& picture, int left, int top, int width, int height,
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

void zoom(Picture& picture, int left, int top, int width, int height, int zoomFactor, const std::string& path) {
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

void examineStrip(int width, int height, bool windowOrder) {
    Picture picture(width, height, windowOrder);
    gtc::native::drawFpsCounter(picture.frame, 300.0);
    const std::string where = "strip " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");

    const auto layout = gtc::native::layoutFpsCounter(width, height, 300.0);
    check(layout.width > 0 && layout.height > 0, where + ": the strip has a size");
    const int leftMargin = layout.left;
    const int rightMargin = width - (layout.left + layout.width);
    check(std::abs(leftMargin - rightMargin) <= gtc::native::kFpsCounterCentreTolerance,
          where + ": the strip is centred (" + std::to_string(leftMargin) + " left, " +
              std::to_string(rightMargin) + " right)");
    check(layout.top == gtc::native::kFpsCounterMargin, where + ": the strip hangs from the top edge");
    check(picture.untouched(0, height - 1), where + ": the rest of the frame is untouched");

    const auto panel = bytes(gtc::native::colorHex("#05090b"));
    const auto expected = blendOver(panel, picture.ground);
    const auto inside = picture.at(layout.left + 1, layout.top + layout.height - 2);
    check(worstChannel(inside, expected) <= 1, where + ": the strip is half transparent");
    check(inside != picture.ground, where + ": the strip is drawn at all");
    check(inside != panel, where + ": the strip is not opaque");

    const auto text = survey(picture, layout.textLeft, layout.textTop, layout.textWidth, layout.textHeight, expected);
    check(text.lit > 40, where + ": the text is drawn (" + std::to_string(text.lit) + " pixels)");
    check(text.levels > 12, where + ": the text is antialiased (" + std::to_string(text.levels) + " levels)");
    check(gtc::native::fpsCounterTextHeight(height) >= 20,
          where + ": the text is below the 20-pixel floor");

    if (!windowOrder) {
        zoom(picture, layout.left, layout.top, layout.width, layout.height, 6,
             "build/native-client/interface-strip-zoom-" + std::to_string(width) + "x" + std::to_string(height) +
                 ".png");
        picture.frame.save("build/native-client/interface-strip-" + std::to_string(width) + "x" +
                           std::to_string(height) + ".png");
    }
    std::printf("  %-26s strip %dx%d at %d,%d, %d lit pixels, %d levels\n", where.c_str(), layout.width,
                layout.height, layout.left, layout.top, text.lit, text.levels);
}

void examineCrosshair(int width, int height, bool windowOrder) {
    Picture picture(width, height, windowOrder);
    gtc::native::ViewState view;
    view.mode = gtc::native::ViewMode::ThirdPerson;
    gtc::native::drawCrosshair(picture.frame, view);
    const std::string where = "reticle " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");

    const auto ink = bytes(gtc::native::colorHex("#d7ff54"));
    const auto shadow = bytes(gtc::native::colorHex("#020604"));
    const int centreX = width / 2, centreY = height / 2;
    const int unit = std::clamp(static_cast<int>(std::lround(std::min(width, height) / 640.0)), 1, 4);
    const int gap = 3 * unit, arm = 7 * unit;
    const int row = centreY - unit / 2, column = centreX - unit / 2;

    check(picture.same(column, row, ink), where + ": the centre dot is drawn in the reading colour");
    // An arm, its gap, and the outline that makes it readable on a pale floor.
    const int length = arm - gap + 1;
    check(picture.same(centreX - arm, row, ink), where + ": the left arm starts where the unit says");
    check(picture.same(centreX - arm + length - 1, row, ink), where + ": the left arm ends before the gap");
    // The hole in the middle of the reticle: whatever the outline does, the gap
    // itself is never ink, or it is not a reticle, it is a cross.
    const auto gapPixel = picture.at(centreX - gap + 1, row);
    check(worstChannel(gapPixel, ink) > 8, where + ": there is a gap between the arm and the dot");
    check(picture.same(centreX - arm - 1, row, shadow) || picture.same(centreX - arm - unit, row, shadow),
          where + ": the arm has a dark outline");
    check(picture.untouched(centreX - arm - unit - 1, row), where + ": nothing is drawn past the arm");
    check(picture.same(column, centreY - arm, ink), where + ": the top arm is drawn");
    check(picture.same(centreX - arm, centreY - arm, shadow) || picture.untouched(centreX - arm, centreY - arm),
          where + ": the corner of the arm is not a rectangle");

    // God view: the cursor is a command, not an aim, so there is no reticle.
    Picture god(width, height, windowOrder);
    gtc::native::ViewState godView;
    godView.mode = gtc::native::ViewMode::God;
    gtc::native::drawCrosshair(god.frame, godView);
    check(god.untouched(centreX, centreY), where + ": god view carries no reticle");

    if (!windowOrder) {
        zoom(picture, centreX - arm - 2, centreY - arm - 2, 2 * arm + 4, 2 * arm + 4, 4,
             "build/native-client/interface-reticle-zoom-" + std::to_string(width) + "x" + std::to_string(height) +
                 ".png");
    }
    std::printf("  %-26s reticle unit %d, arm %d, gap %d\n", where.c_str(), unit, arm, gap);
}

void examineCheatMenu(int width, int height, bool windowOrder) {
    Picture picture(width, height, windowOrder);
    gtc::native::drawCheatMenu(picture.frame, 62, 30);
    const std::string where = "cheats " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");

    const auto layout = gtc::native::layoutCheatMenu(width, height);
    check(layout.width > 0 && layout.height > 0, where + ": the panel has a size");
    check(layout.left >= 0 && layout.top >= 0 && layout.left + layout.width <= width &&
              layout.top + layout.height <= height,
          where + ": the panel fits inside the frame");
    const int leftMargin = layout.left, rightMargin = width - (layout.left + layout.width);
    const int topMargin = layout.top, bottomMargin = height - (layout.top + layout.height);
    check(std::abs(leftMargin - rightMargin) <= 1, where + ": the panel is centred horizontally");
    check(std::abs(topMargin - bottomMargin) <= 1, where + ": the panel is centred vertically");
    check(layout.footerTop + layout.textHeight < layout.top + layout.height - 2,
          where + ": the footer is inside the panel");
    check(layout.firstRowTop > layout.statusTop + layout.textHeight - 1,
          where + ": the rows start below the status line");
    check(layout.firstRowTop + 3 * layout.rowGap + layout.textHeight < layout.footerTop,
          where + ": the last row ends above the footer");

    const auto panel = bytes(gtc::native::colorHex("#05090b"));
    const auto expected = blendOver(panel, picture.ground, gtc::native::kMenuPanelOpacity);
    const auto corner = picture.at(layout.left + 1, layout.top + layout.height - 2);
    check(worstChannel(corner, expected) <= 1, where + ": the panel is half transparent");
    check(corner != panel, where + ": the panel is not opaque");

    // Text in every band: the header, the values, the rows, the footer.
    const int rowsTop = layout.firstRowTop;
    const int rowsHeight = 3 * layout.rowGap + layout.textHeight;
    const auto header = survey(picture, layout.left, layout.titleTop, layout.width, layout.titleHeight, expected);
    const auto status = survey(picture, layout.left, layout.statusTop, layout.width, layout.textHeight, expected);
    const auto rows = survey(picture, layout.contentLeft, rowsTop, layout.width - (layout.contentLeft - layout.left) * 2,
                             rowsHeight, expected);
    const auto footer = survey(picture, layout.left, layout.footerTop, layout.width, layout.textHeight, expected);
    check(header.lit > 20, where + ": the header is drawn");
    check(status.lit > 30, where + ": the values are drawn");
    check(rows.lit > 120, where + ": the eight keys are drawn (" + std::to_string(rows.lit) + " pixels)");
    check(footer.lit > 20, where + ": the footer is drawn");
    check(header.levels > 8 && rows.levels > 12, where + ": the panel text is antialiased");

    // Two columns that do not run into each other: the space between the left
    // label column and the right column's keys is empty panel, on every row.
    check(layout.rightKeyLeft > layout.leftColumnRight, where + ": the columns have a gap");
    int drawnInTheGap = 0;
    for (int y = rowsTop; y < rowsTop + rowsHeight; ++y) {
        for (int x = layout.leftColumnRight; x < layout.rightKeyLeft; ++x) {
            if (worstChannel(picture.at(x, y), expected) > 2) ++drawnInTheGap;
        }
    }
    check(drawnInTheGap == 0, where + ": the columns do not collide (" +
                                  std::to_string(drawnInTheGap) + " stray pixels)");

    // The values are live: a different bladder must produce a different picture.
    Picture other(width, height, windowOrder);
    gtc::native::drawCheatMenu(other.frame, 7, 30);
    bool differs = false;
    for (int y = layout.statusTop; y < layout.statusTop + layout.textHeight && !differs; ++y) {
        for (int x = layout.left; x < layout.left + layout.width; ++x) {
            if (!picture.same(x, y, std::array<int, 3>{other.at(x, y)[0], other.at(x, y)[1], other.at(x, y)[2]})) {
                differs = true;
                break;
            }
        }
    }
    check(differs, where + ": the values shown are the ones that were handed over");

    // Draw order: the panel is on top of the reticle, as in the loop.
    Picture composite(width, height, windowOrder);
    gtc::native::ViewState view;
    view.mode = gtc::native::ViewMode::ThirdPerson;
    gtc::native::drawCrosshair(composite.frame, view);
    gtc::native::drawCheatMenu(composite.frame, 62, 30);
    // The panel is drawn over whatever was there, which here is the reticle's ink.
    const auto overInk = blendOver(panel, bytes(gtc::native::colorHex("#d7ff54")), gtc::native::kMenuPanelOpacity);
    check(worstChannel(composite.at(width / 2, height / 2), overInk) <= 1,
          where + ": the panel covers the reticle");

    if (!windowOrder) {
        zoom(picture, layout.left, layout.top, layout.width, layout.height, 3,
             "build/native-client/interface-cheats-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
    }
    std::printf("  %-26s panel %dx%d at %d,%d, text %d px, %d row pixels\n", where.c_str(), layout.width,
                layout.height, layout.left, layout.top, layout.textHeight, rows.lit);
}

// Mean luminance of a block, for telling the running mode from the other two
// without knowing which colour it was drawn in.
int meanLuminance(const Picture& picture, int left, int top, int width, int height) {
    long total = 0;
    int count = 0;
    for (int y = top; y < top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
            const auto pixel = picture.at(x, y);
            if (pixel[0] < 0) continue;
            total += (pixel[0] * 30 + pixel[1] * 59 + pixel[2] * 11) / 100;
            ++count;
        }
    }
    return count == 0 ? 0 : static_cast<int>(total / count);
}

void examineModeBar(int width, int height, bool windowOrder) {
    const std::string where = "modes " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");
    const auto layout = gtc::native::layoutModeBar(width, height);
    check(layout.width > 0 && layout.height > 0, where + ": the bar has a size");
    check(layout.left >= 0 && layout.left + layout.width <= width, where + ": the bar fits across the frame");
    check(layout.top >= 0 && layout.top + layout.height <= height, where + ": the bar fits down the frame");
    check(std::abs(layout.left - (width - layout.left - layout.width)) <= 1, where + ": the bar is centred");
    const int bottomMargin = height - (layout.top + layout.height);
    check(bottomMargin >= 6 && bottomMargin <= layout.textHeight,
          where + ": the bar is anchored to the bottom (" + std::to_string(bottomMargin) + " px)");
    check(layout.top > layout.height, where + ": the bar is at the bottom, not the top");

    Picture picture(width, height, windowOrder);
    gtc::native::drawModeBar(picture.frame, gtc::native::ViewMode::ThirdPerson);
    const auto panel = bytes(gtc::native::colorHex("#05090b"));
    const auto expected = blendOver(panel, picture.ground, gtc::native::kPanelOpacity);
    check(worstChannel(picture.at(layout.left + 1, layout.top + 1), expected) <= 1,
          where + ": the bar is half transparent");
    check(!picture.same(layout.left + 1, layout.top + 1, panel), where + ": the bar is not opaque");

    // Three blocks with space between them, and the active one brighter with a
    // rule under it. Switching modes has to move both of those.
    for (int index = 0; index < 3; ++index) {
        check(layout.blockWidth[index] > layout.textHeight, where + ": block " + std::to_string(index) + " has width");
        if (index + 1 < 3) {
            const int gapLeft = layout.blockLeft[index] + layout.blockWidth[index];
            const int gapRight = layout.blockLeft[index + 1];
            check(gapRight > gapLeft, where + ": the blocks are separated");
            int stray = 0;
            for (int y = layout.top + layout.height / 4; y < layout.top + layout.height * 3 / 4; ++y) {
                for (int x = gapLeft; x < gapRight; ++x) {
                    if (worstChannel(picture.at(x, y), expected) > 2) ++stray;
                }
            }
            check(stray == 0, where + ": nothing is drawn between the blocks (" + std::to_string(stray) + ")");
        }
    }

    const gtc::native::ViewMode modes[3] = {gtc::native::ViewMode::ThirdPerson, gtc::native::ViewMode::God,
                                            gtc::native::ViewMode::FirstPerson};
    const char* names[3] = {"third person", "god", "first person"};
    for (int active = 0; active < 3; ++active) {
        Picture shot(width, height, windowOrder);
        gtc::native::drawModeBar(shot.frame, modes[active]);
        int means[3] = {0, 0, 0};
        for (int index = 0; index < 3; ++index) {
            means[index] = meanLuminance(shot, layout.blockLeft[index], layout.top, layout.blockWidth[index],
                                         layout.height);
        }
        for (int index = 0; index < 3; ++index) {
            if (index == active) continue;
            check(means[active] > means[index] + 2,
                  where + ": " + std::string(names[active]) + " is the brightest block");
        }
        // The rule under the active block, and nowhere else.
        const int ruleRow = layout.underlineTop + layout.underlineHeight / 2;
        const auto rule = shot.at(layout.blockLeft[active] + layout.blockWidth[active] / 2, ruleRow);
        check(worstChannel(rule, bytes(gtc::native::colorHex("#f2f7ea"))) <= 2,
              where + ": " + std::string(names[active]) + " carries the rule");
        for (int index = 0; index < 3; ++index) {
            if (index == active) continue;
            const auto other = shot.at(layout.blockLeft[index] + layout.blockWidth[index] / 2, ruleRow);
            check(worstChannel(other, bytes(gtc::native::colorHex("#f2f7ea"))) > 8,
                  where + ": the rule is not under block " + std::to_string(index));
        }
    }

    if (!windowOrder) {
        zoom(picture, layout.left, layout.top, layout.width, layout.height, 4,
             "build/native-client/interface-modes-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
    }
    std::printf("  %-26s bar %dx%d at %d,%d, text %d px, bottom margin %d\n", where.c_str(), layout.width,
                layout.height, layout.left, layout.top, layout.textHeight, bottomMargin);
}

// The bug this exists for: a panel that stops the game and draws nothing. Pressing
// E or H used to put the client into a mode that swallowed every click and every
// movement key while the screen showed nothing at all. So: every overlay has to
// put ink on the screen, an open overlay has to be where clicks are aimed, and a
// click on a row has to name that row and no other.
void examineOverlayPanels(int width, int height, bool windowOrder) {
    const std::string where = std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");
    const auto panel = bytes(gtc::native::colorHex("#05090b"));

    struct Case {
        const char* name;
        gtc::native::OverlayPanelLayout (*layout)(int, int);
        void (*draw)(gtc::native::NativeFrame&);
    };
    // Only the sheet is a panel any more: the throne's choices are a circle, and
    // that is checked on its own below.
    const Case cases[] = {
        {"manual", &gtc::native::manualLayout, &gtc::native::drawManual},
    };
    for (const Case& testCase : cases) {
        Picture picture(width, height, windowOrder);
        testCase.draw(picture.frame);
        const auto layout = testCase.layout(width, height);
        const std::string name = where + " " + testCase.name;

        check(layout.width > 0 && layout.height > 0, name + ": the panel has a size");
        check(layout.left >= 0 && layout.top >= 0 && layout.left + layout.width <= width &&
                  layout.top + layout.height <= height,
              name + ": the panel fits inside the frame");
        check(std::abs(layout.left - (width - layout.left - layout.width)) <= 1, name + ": centred horizontally");
        check(std::abs(layout.top - (height - layout.top - layout.height)) <= 1, name + ": centred vertically");

        // Half transparent, and not the raw panel colour: the world stays visible
        // through it, which is what stops a panel looking like a hole.
        const auto expected = blendOver(panel, picture.ground, gtc::native::kMenuPanelOpacity);
        const auto inside = picture.at(layout.left + 1, layout.top + layout.height - 2);
        check(worstChannel(inside, expected) <= 1, name + ": half transparent");
        check(inside != panel, name + ": not opaque");

        // Ink: the title, the rows and the footer are all drawn.
        const auto title = survey(picture, layout.left, layout.titleTop, layout.width, layout.titleHeight, expected);
        check(title.lit > 20, name + ": the title is drawn");
        const auto rows = survey(picture, layout.left, layout.clickableTop, layout.width, layout.clickableHeight,
                                 expected);
        check(rows.lit > 60, name + ": the rows are drawn (" + std::to_string(rows.lit) + " pixels)");
        check(rows.levels > 8, name + ": the text is antialiased");
    }

    if (!windowOrder) {
        Picture manual(width, height, windowOrder);
        gtc::native::drawManual(manual.frame);
        manual.frame.save("build/native-client/interface-manual-" + std::to_string(width) + "x" +
                          std::to_string(height) + ".png");
    }
    std::printf("  %-26s controls sheet: drawn, centred, inside the frame\n", where.c_str());
}

}  // namespace


// The interaction circle: three options on a ring around a cancel button.
//
// Three things are asserted, and the third is the one that costs a round trip if
// it is wrong. The options have to be reachable and distinct, the middle has to
// cancel, and what is under the pointer has to be what looks lit -- so the picture
// is compared against the same hit test a click uses.
void examineInteractionWheel(int width, int height, bool windowOrder) {
    const std::string where = "wheel " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");
    const auto layout = gtc::native::layoutInteractionWheel(width, height);

    // One disc, centred, inside the frame.
    check(layout.radius > 0, where + ": the disc has a radius");
    check(std::abs(layout.centreX - width / 2) <= 1 && std::abs(layout.centreY - height / 2) <= 1,
          where + ": the disc is centred");
    check(layout.centreX - layout.radius >= 0 && layout.centreY - layout.radius >= 0 &&
              layout.centreX + layout.radius < width && layout.centreY + layout.radius < height,
          where + ": the disc fits inside the frame");
    check(layout.cancelRadius > 0 && layout.cancelRadius < layout.radius / 2,
          where + ": the cancel is a button, not the whole disc");

    // Everything inside the disc is one of the three sectors or the cancel, and
    // the three sectors are the same size: a divided circle, not three targets.
    int counts[gtc::native::kWheelOptionCount] = {0, 0, 0};
    long cancels = 0, inside = 0, outsideDrawn = 0;
    for (int dy = -layout.radius; dy <= layout.radius; ++dy) {
        for (int dx = -layout.radius; dx <= layout.radius; ++dx) {
            const int hit = gtc::native::interactionWheelHit(layout, layout.centreX + dx, layout.centreY + dy);
            const bool inDisc = dx * dx + dy * dy <= layout.radius * layout.radius;
            if (inDisc) ++inside;
            if (hit == gtc::native::kWheelOutside && inDisc) ++outsideDrawn;
            if (hit == gtc::native::kWheelCancel) ++cancels;
            if (hit >= 0) ++counts[hit];
        }
    }
    check(outsideDrawn == 0, where + ": every pixel of the disc belongs to a sector (" +
                                 std::to_string(outsideDrawn) + " belong to none)");
    const int largest = std::max({counts[0], counts[1], counts[2]});
    const int smallest = std::min({counts[0], counts[1], counts[2]});
    check(counts[0] > 0 && counts[1] > 0 && counts[2] > 0, where + ": all three sectors exist");
    check(smallest * 100 >= largest * 90, where + ": the three sectors are equal thirds (" +
                                               std::to_string(smallest) + " to " + std::to_string(largest) + ")");
    check(cancels > 40, where + ": the middle cancels (" + std::to_string(cancels) + " pixels)");
    check(gtc::native::interactionWheelHit(layout, 0, 0) == gtc::native::kWheelOutside,
          where + ": the corner of the frame is outside");
    check(gtc::native::interactionWheelHit(layout, layout.centreX, layout.centreY) == gtc::native::kWheelCancel,
          where + ": the dead centre cancels");
    // Up is the first sector, and the other two are where the layout says.
    check(gtc::native::interactionWheelHit(layout, layout.centreX, layout.centreY - layout.radius * 3 / 4) == 0,
          where + ": straight up is the first option");

    // Nothing outside the disc is touched: no panel, no box, no heading.
    Picture resting(width, height, windowOrder);
    gtc::native::drawInteractionWheel(resting.frame, layout, -1, -1);
    int touchedOutside = 0;
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            const long dx = x - layout.centreX, dy = y - layout.centreY;
            if (dx * dx + dy * dy <= static_cast<long>(layout.radius + layout.stroke + 2) *
                    (layout.radius + layout.stroke + 2)) continue;
            if (!resting.untouched(x, y)) ++touchedOutside;
        }
    }
    check(touchedOutside == 0, where + ": nothing is drawn outside the disc (" + std::to_string(touchedOutside) +
                                   " pixels)");

    // Hovering a sector lights that sector and only that sector, and the name in
    // it switches to the reading colour.
    for (int index = 0; index < gtc::native::kWheelOptionCount; ++index) {
        Picture hovered(width, height, windowOrder);
        gtc::native::drawInteractionWheel(hovered.frame, layout, layout.labelX[index], layout.labelY[index]);
        for (int other = 0; other < gtc::native::kWheelOptionCount; ++other) {
            // A point halfway out along the other sector's middle: past its label,
            // well inside its own wedge.
            const int probeX = layout.centreX + (layout.labelX[other] - layout.centreX) * 3 / 2;
            const int probeY = layout.centreY + (layout.labelY[other] - layout.centreY) * 3 / 2;
            const auto a = hovered.at(probeX, probeY);
            const auto b = resting.at(probeX, probeY);
            if (other == index) {
                check(a != b, where + ": the sector under the pointer is lit");
            } else {
                check(a == b, where + ": sector " + std::to_string(other) + " stays dark while " +
                                   std::to_string(index) + " is hovered");
            }
        }
    }

    // The middle, under the pointer, and gone again when it is not.
    Picture centreLit(width, height, windowOrder);
    gtc::native::drawInteractionWheel(centreLit.frame, layout, layout.centreX, layout.centreY);
    check(centreLit.at(layout.centreX, layout.centreY) != resting.at(layout.centreX, layout.centreY),
          where + ": the cancel lights up under the pointer");

    if (!windowOrder) {
        zoom(resting, layout.centreX - layout.radius - 4, layout.centreY - layout.radius - 4,
             layout.radius * 2 + 8, layout.radius * 2 + 8, 2,
             "build/native-client/interface-wheel-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
    }
    std::printf("  %-26s disc r=%d cancel r=%d, thirds %d/%d/%d\n", where.c_str(), layout.radius,
                layout.cancelRadius, counts[0], counts[1], counts[2]);
    (void)inside;
}


int main() {
    examineStrip(640, 480, false);
    examineStrip(640, 480, true);
    examineStrip(1920, 1080, true);
    examineStrip(1568, 929, true);

    examineCrosshair(640, 480, false);
    examineCrosshair(640, 480, true);
    examineCrosshair(1920, 1080, true);

    examineInteractionWheel(640, 480, false);
    examineInteractionWheel(640, 480, true);
    examineInteractionWheel(1920, 1080, true);

    examineOverlayPanels(640, 480, false);
    examineOverlayPanels(640, 480, true);
    examineOverlayPanels(1920, 1080, true);

    examineModeBar(640, 480, false);
    examineModeBar(640, 480, true);
    examineModeBar(1568, 929, true);
    examineModeBar(1920, 1080, true);

    examineCheatMenu(640, 480, false);
    examineCheatMenu(640, 480, true);
    examineCheatMenu(1568, 929, true);
    examineCheatMenu(1920, 1080, true);

    // A number that is not a number is not allowed to print nonsense, and the
    // strip has to keep its shape whatever the rate is.
    gtc::native::NativeFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.rgb.assign(640U * 480U * 3, 0);
    gtc::native::drawFpsCounter(frame, std::nan(""));
    gtc::native::drawFpsCounter(frame, -1.0);
    gtc::native::drawFpsCounter(frame, 0.4);
    // Needs outside their range are clamped, not printed.
    gtc::native::drawCheatMenu(frame, -20, 400);

    if (failures == 0) {
        std::printf("interface: PASS (strip, reticle, need panel, mode bar, panels, interaction circle)\n");
        return 0;
    }
    std::printf("interface: FAIL (%d)\n", failures);
    return 1;
}
