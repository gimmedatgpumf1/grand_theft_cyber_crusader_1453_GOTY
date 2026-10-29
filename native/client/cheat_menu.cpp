#include "client/cheat_menu.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace gtc::native {
namespace {

// Eight keys, four rows. Two columns of (key, what it does), because a list that
// runs off the bottom of a 480-line screen is a list nobody reads.
struct Row {
    const char* leftKey;
    const char* leftLabel;
    const char* rightKey;
    const char* rightLabel;
};

constexpr Row kRows[] = {
    {"1", "Bladder +25", "2", "Bladder -25"},
    {"3", "Empty bladder", "4", "Fill bladder"},
    {"5", "Bowel +25", "6", "Bowel -25"},
    {"7", "Empty bowel", "8", "Fill bowel"},
};
constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));
constexpr const char* kTitle = "NEEDS";
constexpr const char* kFooter = "F1 CLOSE";

int widest(const char* const* lines, int count, int textHeight) {
    int width = 0;
    for (int index = 0; index < count; ++index) {
        width = std::max(width, textWidthPixels(lines[index], textHeight));
    }
    return width;
}

std::string statusLine(int bladderPercent, int bowelPercent) {
    char text[64];
    std::snprintf(text, sizeof(text), "Bladder %d%%   Bowel %d%%", bladderPercent, bowelPercent);
    return text;
}

}  // namespace

CheatMenuLayout layoutCheatMenu(int frameWidth, int frameHeight) {
    CheatMenuLayout layout;
    layout.textHeight = interfaceTextHeight(frameHeight, 11.0);
    layout.titleHeight = interfaceTextHeight(frameHeight, 16.0);
    const int gap = lineGap(layout.textHeight);
    layout.rowGap = layout.textHeight + gap;

    // Column widths from the actual face, not from an assumed monospaced grid:
    // this is a proportional sans and a key column of "1".."8" is not 8 columns.
    int keyWidth = 0, leftWidth = 0, rightWidth = 0;
    for (int index = 0; index < kRowCount; ++index) {
        keyWidth = std::max(keyWidth, textWidthPixels(kRows[index].leftKey, layout.textHeight));
        keyWidth = std::max(keyWidth, textWidthPixels(kRows[index].rightKey, layout.textHeight));
        leftWidth = std::max(leftWidth, textWidthPixels(kRows[index].leftLabel, layout.textHeight));
        rightWidth = std::max(rightWidth, textWidthPixels(kRows[index].rightLabel, layout.textHeight));
    }
    layout.keyWidth = keyWidth;
    const int keyGap = gap;
    const int columnGap = layout.textHeight * 2;
    const int contentWidth = keyWidth + keyGap + leftWidth + columnGap + keyWidth + keyGap + rightWidth;

    const std::string status = statusLine(100, 100);
    const char* const centred[] = {kTitle, kFooter, status.c_str()};
    const int contentHeight = layout.titleHeight + gap + layout.textHeight + gap + kRowCount * layout.rowGap +
        gap + layout.textHeight;
    const int padding = std::max(8, layout.textHeight);
    // One pixel of the panel is taken away so a strip that lands on a half pixel
    // cannot end up one wider on one side than the other.
    const int width = std::max(widest(centred, 3, layout.textHeight), contentWidth) + padding * 2;
    layout.width = width + (width % 2);
    layout.height = contentHeight + padding * 2;
    layout.left = (frameWidth - layout.width) / 2;
    layout.top = (frameHeight - layout.height) / 2;
    layout.contentLeft = layout.left + (layout.width - contentWidth) / 2;
    layout.leftColumnRight = layout.contentLeft + keyWidth + keyGap + leftWidth;
    layout.rightKeyLeft = layout.contentLeft + keyWidth + keyGap + leftWidth + columnGap;
    layout.rightColumnRight = layout.rightKeyLeft + keyWidth + keyGap + rightWidth;
    layout.titleTop = layout.top + padding;
    layout.statusTop = layout.titleTop + layout.titleHeight + gap;
    layout.firstRowTop = layout.statusTop + layout.textHeight + gap;
    layout.footerTop = layout.firstRowTop + kRowCount * layout.rowGap + gap;
    return layout;
}

CheatMenuRect cheatMenuRect(const CheatMenuLayout& layout, int entry) {
    CheatMenuRect box;
    if (entry < 0 || entry >= kRowCount * 2 || layout.width <= 0) return box;
    const int gap = lineGap(layout.textHeight);
    // Entries are numbered the way they are read: left to right, then down. The
    // row is the index halved and the column is its parity, so entry 3 is the
    // fourth key -- "4 Fill bladder" -- on the second row, right-hand side. The
    // first version numbered the left column 0..3 and the right 4..7, which put a
    // mark three rows away from the pointer and sent the wrong key when clicked.
    const int row = entry / 2;
    const bool right = (entry % 2) == 1;
    const int slack = std::max(2, gap / 4);
    const int textLeft = right ? layout.rightKeyLeft : layout.contentLeft;
    const int textRight = right ? layout.rightColumnRight : layout.leftColumnRight;
    box.left = textLeft - slack;
    box.top = layout.firstRowTop + row * layout.rowGap - slack;
    box.width = textRight - textLeft + slack * 2;
    box.height = layout.textHeight + slack * 2;
    return box;
}

int cheatMenuAt(const CheatMenuLayout& layout, int x, int y) {
    if (layout.width <= 0 || layout.height <= 0) return -1;
    for (int entry = 0; entry < kRowCount * 2; ++entry) {
        const CheatMenuRect box = cheatMenuRect(layout, entry);
        if (x >= box.left && x < box.left + box.width && y >= box.top && y < box.top + box.height) return entry;
    }
    return -1;
}

void drawCheatMenu(NativeFrame& frame, int bladderPercent, int bowelPercent, int hovered) {
    const CheatMenuLayout layout = layoutCheatMenu(frame.width, frame.height);
    if (layout.width <= 0 || layout.height <= 0) return;
    bladderPercent = std::clamp(bladderPercent, 0, 100);
    bowelPercent = std::clamp(bowelPercent, 0, 100);

    fillPanel(frame, layout.left, layout.top, layout.width, layout.height, kMenuPanelOpacity);

    const Color ink = colorHex(kInkColour), muted = colorHex(kMutedColour);
    const auto centredAt = [&](const std::string& text, int top, int height, Color colour) {
        const int width = textWidthPixels(text, height);
        drawTextPixels(frame, layout.left + (layout.width - width) / 2, top, text, colour, height);
    };
    centredAt(kTitle, layout.titleTop, layout.titleHeight, ink);

    const std::string status = statusLine(bladderPercent, bowelPercent);
    centredAt(status, layout.statusTop, layout.textHeight, muted);

    const int gap = lineGap(layout.textHeight);
    const int keyWidth = layout.keyWidth;

    // The entry the pointer is on, marked before the words so it sits behind them.
    // Only that entry's own box: a wash across the whole row cannot tell you which
    // of the two things on it you are about to press.
    if (hovered >= 0 && hovered < kRowCount * 2) {
        const CheatMenuRect box = cheatMenuRect(layout, hovered);
        fillBand(frame, box.left, box.top, box.width, box.height, colorHex(kInkColour), 0.14);
    }

    for (int index = 0; index < kRowCount; ++index) {
        const int top = layout.firstRowTop + index * layout.rowGap;
        // Keys in the reading colour, what they do in the quieter one: the eye
        // finds "3" without reading "Empty bladder" again.
        drawTextPixels(frame, layout.contentLeft, top, kRows[index].leftKey, ink, layout.textHeight);
        drawTextPixels(frame, layout.contentLeft + keyWidth + gap, top, kRows[index].leftLabel, muted,
                       layout.textHeight);
        drawTextPixels(frame, layout.rightKeyLeft, top, kRows[index].rightKey, ink, layout.textHeight);
        drawTextPixels(frame, layout.rightKeyLeft + keyWidth + gap, top, kRows[index].rightLabel, muted,
                       layout.textHeight);
    }

    centredAt(kFooter, layout.footerTop, layout.textHeight, muted);
}

}  // namespace gtc::native
