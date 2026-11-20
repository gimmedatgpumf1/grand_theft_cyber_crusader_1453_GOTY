#include "interface_check.h"

// The two menus: the cheat list that stops the game, and the bar along the top
// that says what is switched on.

namespace interface_check {

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

    // Clickable: eight entries, each answering to a click on its own row, and the
    // title and footer answering to nothing.
    int found[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int x = layout.contentLeft; x < layout.rightColumnRight; ++x) {
        for (int row = 0; row < 4; ++row) {
            const int y = layout.firstRowTop + row * layout.rowGap + layout.textHeight / 2;
            const int hit = gtc::native::cheatMenuAt(layout, x, y);
            if (hit >= 0) found[hit] = 1;
        }
    }
    bool allFound = true;
    for (int index = 0; index < 8; ++index) allFound = allFound && found[index] == 1;
    check(allFound, where + ": all eight entries can be clicked");
    check(gtc::native::cheatMenuAt(layout, layout.left + layout.width / 2, layout.titleTop) == -1,
          where + ": the title is not a button");
    // The footer is the close button, and it has to be one: the King pressed it.
    const auto close = gtc::native::cheatMenuRect(layout, gtc::native::kCheatMenuClose);
    check(close.width > layout.textHeight && close.height > 0, where + ": the close button has a box");
    check(gtc::native::cheatMenuAt(layout, close.left + close.width / 2, close.top + close.height / 2) ==
              gtc::native::kCheatMenuClose,
          where + ": a click on CLOSE closes");
    check(close.left >= layout.left && close.left + close.width <= layout.left + layout.width,
          where + ": the close button is inside the panel");
    check(close.top >= layout.firstRowTop + 3 * layout.rowGap,
          where + ": the close button is below the entries, not on one");
    check(gtc::native::cheatMenuAt(layout, layout.left + 2, close.top + 1) == -1,
          where + ": the margin beside the close button is not a button");
    // Hovering it marks it, and marks nothing else.
    Picture closing(width, height, windowOrder);
    gtc::native::drawCheatMenu(closing.frame, 62, 30, gtc::native::kCheatMenuClose);
    int closeInside = 0, closeOutside = 0;
    for (int y = layout.footerTop - layout.textHeight; y < layout.top + layout.height; ++y) {
        for (int x = layout.left + 1; x < layout.left + layout.width - 1; ++x) {
            if (worstChannel(closing.at(x, y), picture.at(x, y)) <= 1) continue;
            if (x >= close.left && x < close.left + close.width && y >= close.top &&
                y < close.top + close.height) ++closeInside;
            else ++closeOutside;
        }
    }
    check(closeInside > close.width, where + ": hovering CLOSE marks it");
    check(closeOutside == 0, where + ": hovering CLOSE marks nothing else");
    check(gtc::native::cheatMenuAt(layout, layout.left + 1, layout.firstRowTop) == -1,
          where + ": the panel margin is not a button");
    // Row to entry, read left to right then down: row r has entries 2r and 2r+1,
    // so index + '1' is the key printed on it. A click must not land on its
    // neighbour, which is what the two-column numbering used to do.
    for (int row = 0; row < 4; ++row) {
        const int y = layout.firstRowTop + row * layout.rowGap + layout.textHeight / 2;
        check(gtc::native::cheatMenuAt(layout, layout.contentLeft + 4, y) == row * 2,
              where + ": row " + std::to_string(row) + " left is entry " + std::to_string(row * 2));
        check(gtc::native::cheatMenuAt(layout, layout.rightKeyLeft + 4, y) == row * 2 + 1,
              where + ": row " + std::to_string(row) + " right is entry " + std::to_string(row * 2 + 1));
    }

    // Every entry, not just one of them: the first version marked entry 3 on the
    // fourth row because the row was computed as index % 4, which is the row for
    // the left column only. A check that samples a single entry can miss that, so
    // this walks all eight and asks where the mark actually landed.
    for (int entry = 0; entry < 8; ++entry) {
        const auto box = gtc::native::cheatMenuRect(layout, entry);
        check(box.width > layout.textHeight && box.height > layout.textHeight,
              where + ": entry " + std::to_string(entry) + " has a clickable box");
        check(gtc::native::cheatMenuAt(layout, box.left + box.width / 2, box.top + box.height / 2) == entry,
              where + ": entry " + std::to_string(entry) + " answers to a click in its own box");
        // Where the box IS, not just that the mark agrees with it -- the two come
        // from one function, so a check that only compares them agrees with any
        // mistake. Entries are read left to right then down.
        const int row = entry / 2, column = entry % 2;
        const int rowTop = layout.firstRowTop + row * layout.rowGap;
        check(std::abs((box.top + box.height / 2) - (rowTop + layout.textHeight / 2)) <= 2,
              where + ": entry " + std::to_string(entry) + " is on row " + std::to_string(row));
        const int middle = layout.left + layout.width / 2;
        check(column == 0 ? box.left + box.width <= middle + 2 : box.left >= middle - 2,
              where + ": entry " + std::to_string(entry) + " is in column " + std::to_string(column));
        // And its own key is inside it: the glyph for '1' + entry has to be in the
        // box the click uses, or the button does something other than it says.
        const std::string key(1, static_cast<char>('1' + entry));
        const int keyLeft = column == 0 ? layout.contentLeft : layout.rightKeyLeft;
        const int keyWidth = gtc::native::textWidthPixels(key, layout.textHeight);
        check(keyLeft >= box.left && keyLeft + keyWidth <= box.left + box.width,
              where + ": entry " + std::to_string(entry) + " covers its own key");
        Picture one(width, height, windowOrder);
        gtc::native::drawCheatMenu(one.frame, 62, 30, entry);
        int inside = 0, outside = 0;
        for (int y = layout.firstRowTop - layout.textHeight; y < layout.firstRowTop + 4 * layout.rowGap; ++y) {
            for (int x = layout.left + 2; x < layout.left + layout.width - 2; ++x) {
                const bool differs = worstChannel(one.at(x, y), picture.at(x, y)) > 1;
                if (!differs) continue;
                if (x >= box.left && x < box.left + box.width && y >= box.top && y < box.top + box.height) ++inside;
                else ++outside;
            }
        }
        check(inside > box.width, where + ": entry " + std::to_string(entry) + " is marked (" +
                                      std::to_string(inside) + " px)");
        check(outside == 0, where + ": entry " + std::to_string(entry) + " marks nothing else (" +
                                std::to_string(outside) + " px)");
    }

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

    // Clickable: every column of the strip belongs to a block, so there is no
    // dead space to hit, and a point off the strip belongs to nothing.
    int seen[3] = {0, 0, 0};
    for (int x = layout.left; x < layout.left + layout.width; ++x) {
        const int hit = gtc::native::modeBarAt(layout, x, layout.top + layout.height / 2);
        check(hit >= 0, where + ": column " + std::to_string(x) + " clicks nothing inside the strip");
        if (hit >= 0) seen[hit] = 1;
    }
    check(seen[0] && seen[1] && seen[2], where + ": all three blocks can be clicked");
    check(gtc::native::modeBarAt(layout, layout.left + layout.width / 2, layout.top - 8) == -1,
          where + ": a click above the strip is not a mode change");
    check(gtc::native::modeBarAt(layout, 2, layout.top + 2) == -1,
          where + ": a click at the left of the screen is not a mode change");
    for (int index = 0; index < 3; ++index) {
        const int centre = layout.blockLeft[index] + layout.blockWidth[index] / 2;
        check(gtc::native::modeBarAt(layout, centre, layout.top + layout.height / 2) == index,
              where + ": block " + std::to_string(index) + " answers to a click on itself");
    }

    // Hovering the block that is not running lights it up, without moving any of
    // the words: a hover mark that shifts the type would be worse than none.
    const int litBefore = meanLuminance(picture, layout.blockLeft[2], layout.top, layout.blockWidth[2],
                                        layout.height);
    Picture hovered(width, height, windowOrder);
    gtc::native::drawModeBar(hovered.frame, gtc::native::ViewMode::ThirdPerson, 2);
    const int litAfter = meanLuminance(hovered, layout.blockLeft[2], layout.top, layout.blockWidth[2],
                                       layout.height);
    check(litAfter > litBefore + 1, where + ": the hovered block brightens");
    // The block that is running keeps its rule, and the one on the far left is
    // untouched: hover is a mark, not a mode change.
    const int litLeft = meanLuminance(hovered, layout.blockLeft[0], layout.top, layout.blockWidth[0],
                                      layout.height);
    const int litLeftBefore = meanLuminance(picture, layout.blockLeft[0], layout.top, layout.blockWidth[0],
                                           layout.height);
    check(litLeft == litLeftBefore, where + ": hovering one block leaves the others alone");

    if (!windowOrder) {
        zoom(picture, layout.left, layout.top, layout.width, layout.height, 4,
             "build/native-client/interface-modes-" + std::to_string(width) + "x" + std::to_string(height) + ".png");
    }
    std::printf("  %-26s bar %dx%d at %d,%d, text %d px, bottom margin %d\n", where.c_str(), layout.width,
                layout.height, layout.left, layout.top, layout.textHeight, bottomMargin);
}

}  // namespace interface_check
