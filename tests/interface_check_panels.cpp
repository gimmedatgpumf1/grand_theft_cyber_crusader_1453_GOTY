#include "interface_check.h"

// The panel-shaped things: the overlays, the interaction circle, and the needs
// panel in the bottom right corner.

namespace interface_check {

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

// The needs panel, bottom right: three rows, a bar per row, anchored to the
// corner. The bar has to be the reading and the number has to agree with it, so
// both are measured from the picture rather than from the layout.
void examineNeedsPanel(int width, int height, bool windowOrder) {
    const std::string where = "needs " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");
    const auto bar = gtc::native::layoutModeBar(width, height);
    const auto layout = gtc::native::layoutNeedsPanel(width, height, bar.top, bar.width);
    check(layout.width > 0 && layout.height > 0, where + ": the panel has a size");
    check(layout.left + layout.width <= width && layout.top + layout.height <= height,
          where + ": the panel is inside the frame");
    const int rightMargin = width - (layout.left + layout.width);
    const int bottomMargin = height - (layout.top + layout.height);
    check(rightMargin >= 6 && rightMargin <= layout.textHeight, where + ": it is against the right edge");
    // Against the bottom edge, or lifted just clear of the mode bar when the two
    // would otherwise overlap.
    check((bottomMargin >= 6 && bottomMargin <= layout.textHeight) ||
              (layout.top + layout.height <= bar.top && bar.top - (layout.top + layout.height) <= layout.textHeight),
          where + ": it is against the bottom edge");
    check(layout.left > width / 2 && layout.top > height / 2, where + ": it is in the bottom right, not the middle");
    // Small: not a fourth of the screen. This is a glance, not a page.
    check(layout.width < width / 3 && layout.height < height / 3,
          where + ": it is small (" + std::to_string(layout.width) + "x" + std::to_string(layout.height) + ")");

    // It sits on the same line as the mode bar, or above it, but never through it.
    const bool overlap = layout.left < bar.left + bar.width && bar.left < layout.left + layout.width &&
        layout.top < bar.top + bar.height && bar.top < layout.top + layout.height;
    check(!overlap, where + ": it does not collide with the mode bar");

    const gtc::native::NeedsStatus status{72, 56, 100};
    Picture picture(width, height, windowOrder);
    gtc::native::drawNeedsPanel(picture.frame, layout, status);

    const auto panel = bytes(gtc::native::colorHex("#05090b"));
    const auto expected = blendOver(panel, picture.ground, gtc::native::kPanelOpacity);
    check(worstChannel(picture.at(layout.left + 1, layout.top + 1), expected) <= 1,
          where + ": the panel is half transparent");
    check(!picture.same(layout.left + 1, layout.top + 1, panel), where + ": the panel is not opaque");
    check(picture.untouched(2, 2), where + ": nothing is drawn in the opposite corner");

    // Each row: a filled bar that is as long as the value says, and a track after
    // it. Measured on the row's own middle line.
    const int values[3] = {status.bladder, status.bowel, status.health};
    for (int row = 0; row < gtc::native::kNeedsRowCount; ++row) {
        const int y = layout.rowTop[row] + layout.rowHeight / 2;
        // The fill colour, not "anything that is not the panel": the empty part of
        // the bar is a track of its own and would otherwise count as filled.
        const std::array<int, 3> fill = bytes(gtc::native::colorHex(
            row == 0 ? "#d7ff54" : row == 1 ? "#b07a3c" : "#ff7a5e"));
        int filled = 0;
        for (int x = layout.barLeft; x < layout.barLeft + layout.barWidth; ++x) {
            if (picture.same(x, y, fill)) ++filled;
        }
        const int want = layout.barWidth * values[row] / 100;
        check(std::abs(filled - want) <= 2, where + ": row " + std::to_string(row) + " bar is " +
                                              std::to_string(filled) + " of " + std::to_string(want));
        // Text in the row: the label on the left, the value on the right.
        const auto label = survey(picture, layout.left, layout.rowTop[row], layout.barLeft - layout.left,
                                  layout.rowHeight, expected);
        check(label.lit > 10, where + ": row " + std::to_string(row) + " has a label");
    }

    // The numbers are live: a different value has to change the picture, and a
    // longer number has to stay inside the panel.
    Picture other(width, height, windowOrder);
    gtc::native::drawNeedsPanel(other.frame, layout, gtc::native::NeedsStatus{5, 100, 7});
    bool differs = false;
    for (int y = layout.top; y < layout.top + layout.height && !differs; ++y) {
        for (int x = layout.left; x < layout.left + layout.width; ++x) {
            if (picture.at(x, y) != other.at(x, y)) { differs = true; break; }
        }
    }
    check(differs, where + ": the values shown are the ones handed over");
    Picture extremes(width, height, windowOrder);
    gtc::native::drawNeedsPanel(extremes.frame, layout, gtc::native::NeedsStatus{-10, 250, 100});
    check(extremes.untouched(layout.left - 2, layout.top + layout.height / 2),
          where + ": values outside 0..100 are clamped inside the panel");

    // A full bar is the whole width, an empty one is none of it.
    Picture full(width, height, windowOrder);
    gtc::native::drawNeedsPanel(full.frame, layout, gtc::native::NeedsStatus{100, 100, 100});
    const int y = layout.rowTop[0] + layout.rowHeight / 2;
    check(full.same(layout.barLeft + layout.barWidth - 1, y, bytes(gtc::native::colorHex("#d7ff54"))),
          where + ": a full bar reaches the end");
    Picture empty(width, height, windowOrder);
    gtc::native::drawNeedsPanel(empty.frame, layout, gtc::native::NeedsStatus{0, 0, 0});
    const bool emptyBar = empty.same(layout.barLeft + layout.barWidth / 2, y,
                                     bytes(gtc::native::colorHex("#1b2a24")));
    check(emptyBar, where + ": an empty bar is empty");

    if (!windowOrder) {
        zoom(picture, layout.left, layout.top, layout.width, layout.height, 4,
             "build/native-client/interface-needs-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
    }
    std::printf("  %-26s panel %dx%d at %d,%d (margins %d/%d)\n", where.c_str(), layout.width, layout.height,
                layout.left, layout.top, rightMargin, bottomMargin);
}

}  // namespace interface_check
