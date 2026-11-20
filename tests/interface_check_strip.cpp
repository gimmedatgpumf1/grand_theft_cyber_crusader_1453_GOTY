#include "interface_check.h"

// The rate strip, the reticle and the interact prompt: the three things drawn
// straight into the finished picture that have no panel behind them.

namespace interface_check {

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

void examineTargetPrompt(int width, int height, bool windowOrder) {
    const std::string where = "prompt " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");
    const auto layout = gtc::native::layoutTargetPrompt(width, height, height / 2);
    check(layout.textHeight >= 20, where + ": the text is at or above the floor");
    check(layout.firstTop > height / 2, where + ": it starts below the reticle");

    // Nothing to show, nothing drawn: the prompt is gated on there being a target.
    Picture blank(width, height, windowOrder);
    gtc::native::drawTargetPrompt(blank.frame, layout, nullptr);
    int touched = 0;
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            if (!blank.untouched(x, y)) ++touched;
        }
    }
    check(touched == 0, where + ": no target, no prompt (" + std::to_string(touched) + " pixels drawn)");

    Picture picture(width, height, windowOrder);
    gtc::native::drawTargetPrompt(picture.frame, layout, "Toilet");

    // Two lines, both centred, both legible: bright pixels where the words are and
    // an untouched frame either side of them.
    const int lineOne = gtc::native::textHeightPixels(layout.textHeight);
    const auto action = survey(picture, 0, layout.firstTop, width, lineOne, picture.ground);
    const auto name = survey(picture, 0, layout.secondTop, width, lineOne, picture.ground);
    check(action.lit > 40, where + ": the action line is drawn");
    check(name.lit > 10, where + ": the name is drawn");
    check(name.brightest > action.brightest, where + ": the name is brighter than the action");
    check(picture.untouched(4, height / 2), where + ": nothing is drawn at the left edge");
    check(picture.untouched(width - 5, height / 2), where + ": nothing is drawn at the right edge");
    // No panel and no box: the corners of the band the text sits in are untouched.
    check(picture.untouched(2, layout.firstTop - 2), where + ": there is no box around it");
    check(picture.untouched(width - 3, layout.secondTop + lineOne + 1), where + ": the box has no bottom edge");

    // Centred on the reticle's column: the lit pixels are symmetric about the
    // middle of the frame, to within a pixel or two of type.
    int firstLit = -1, lastLit = -1;
    for (int x = 0; x < width; ++x) {
        int column = 0;
        for (int y = layout.firstTop; y < layout.firstTop + lineOne; ++y) {
            const auto pixel = picture.at(x, y);
            if (pixel[0] < 0) continue;
            if ((pixel[0] * 30 + pixel[1] * 59 + pixel[2] * 11) / 100 > 90) ++column;
        }
        if (column > 0) { if (firstLit < 0) firstLit = x; lastLit = x; }
    }
    check(firstLit >= 0 && std::abs((firstLit + lastLit) / 2 - width / 2) <= 2,
          where + ": the action line is centred on the reticle");

    // What counts as pointing at it. The bug this exists for: E used to fire
    // whenever the player was merely in range, while the highlight asked for aim,
    // so the key worked at times the screen said there was nothing to use.
    check(gtc::native::pointerOnTarget(width, height, width / 2, height / 2, width / 2, height / 2),
          where + ": a pointer on the thing is on the thing");
    check(!gtc::native::pointerOnTarget(width, height, width / 2 + 300, height / 2, width / 2, height / 2),
          where + ": a thing three hundred pixels to the side is not pointed at");
    check(gtc::native::pointerOnTarget(width, height, width / 2 + 40, height / 2 - 40, width / 2, height / 2),
          where + ": the target has a forgiving radius, not a pixel");

    if (!windowOrder) {
        const int top = layout.firstTop - 6;
        const int bandHeight = layout.secondTop + lineOne + 6 - top;
        zoom(picture, width / 2 - 120, top, 240, bandHeight, 4,
             "build/native-client/interface-prompt-" + std::to_string(width) + "x" + std::to_string(height) +
                 ".png");
    }
    std::printf("  %-26s prompt at y=%d, %d/%d lit pixels\n", where.c_str(), layout.firstTop, action.lit,
                name.lit);
}

}  // namespace interface_check
