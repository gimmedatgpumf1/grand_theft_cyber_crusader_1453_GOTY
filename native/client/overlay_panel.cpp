#include "client/overlay_panel.h"

#include "client/interface_style.h"

#include <algorithm>
#include <string>

namespace gtc::native {
namespace {

int paddingFor(int textHeight) { return std::max(8, textHeight); }
int keyGapFor(int textHeight) { return textHeight * 3 / 5; }
int columnGapFor(int textHeight) { return textHeight * 2; }

// A row is a line of text plus air above and below, and the whole of that band is
// what a click on the row means.
int rowGapFor(int textHeight) { return textHeight + lineGap(textHeight); }

int widthOf(const char* text, int textHeight) {
    return text == nullptr ? 0 : textWidthPixels(text, textHeight);
}

}  // namespace

OverlayPanelLayout layoutOverlayPanel(int frameWidth, int frameHeight, const OverlayRow* rows, int rowCount,
                                      int columns, const char* title, const char* footer) {
    OverlayPanelLayout layout;
    columns = std::clamp(columns, 1, 4);
    rowCount = std::max(0, rowCount);
    layout.columns = columns;
    layout.rowsPerColumn = rowCount == 0 ? 0 : (rowCount + columns - 1) / columns;
    layout.textHeight = interfaceTextHeight(frameHeight, 10.0);
    layout.titleHeight = interfaceTextHeight(frameHeight, 14.0);
    layout.rowGap = rowGapFor(layout.textHeight);
    const int gap = lineGap(layout.textHeight);
    const int pad = paddingFor(layout.textHeight);
    const int keyGap = keyGapFor(layout.textHeight);
    const int columnGap = columnGapFor(layout.textHeight);

    // The widest key and the widest label, once: a proportional face has no grid
    // to snap to, so every column is measured rather than assumed.
    int keyWidth = 0, labelWidth = 0;
    for (int index = 0; index < rowCount; ++index) {
        keyWidth = std::max(keyWidth, widthOf(rows[index].key, layout.textHeight));
        labelWidth = std::max(labelWidth, widthOf(rows[index].label, layout.textHeight));
    }
    layout.columnKeyWidth = keyWidth;
    layout.columnWidth = keyWidth + keyGap + labelWidth;

    const int contentWidth = columns * layout.columnWidth + (columns - 1) * columnGap;
    int width = contentWidth + pad * 2;
    width = std::max(width, widthOf(title, layout.titleHeight) + pad * 2);
    width = std::max(width, std::max(widthOf(footer, layout.textHeight), widthOf(title, layout.textHeight)) +
                                pad * 2);
    layout.width = width + (width % 2);

    int height = pad;
    if (title != nullptr) height += layout.titleHeight + gap;
    height += layout.rowsPerColumn * layout.rowGap;
    if (footer != nullptr) height += gap + layout.textHeight;
    height += pad;
    layout.height = height;

    layout.left = (frameWidth - layout.width) / 2;
    layout.top = (frameHeight - layout.height) / 2;

    int y = layout.top + pad;
    if (title != nullptr) {
        layout.titleTop = y;
        y += layout.titleHeight + gap;
    }
    layout.firstRowTop = y;
    layout.clickableTop = y;
    layout.clickableHeight = layout.rowsPerColumn * layout.rowGap;
    y += layout.clickableHeight;
    if (footer != nullptr) layout.footerTop = y + gap;

    const int contentLeft = layout.left + (layout.width - contentWidth) / 2;
    for (int index = 0; index < columns; ++index) {
        layout.columnLeft[index] = contentLeft + index * (layout.columnWidth + columnGap);
    }
    return layout;
}

void drawOverlayPanel(NativeFrame& frame, const OverlayPanelLayout& layout, const OverlayRow* rows, int rowCount,
                      const char* title, const char* footer) {
    if (layout.width <= 0 || layout.height <= 0) return;
    fillPanel(frame, layout.left, layout.top, layout.width, layout.height, kMenuPanelOpacity);

    const Color ink = colorHex(kInkColour), muted = colorHex(kMutedColour);
    const auto centredAt = [&](const char* text, int top, int height, Color colour) {
        if (text == nullptr) return;
        const std::string line(text);
        drawTextPixels(frame, layout.left + (layout.width - textWidthPixels(line, height)) / 2, top, line, colour,
                       height);
    };
    centredAt(title, layout.titleTop, layout.titleHeight, ink);

    const int keyGap = keyGapFor(layout.textHeight);
    for (int index = 0; index < rowCount && index < 64; ++index) {
        const int column = layout.rowsPerColumn == 0 ? 0 : index / layout.rowsPerColumn;
        const int row = layout.rowsPerColumn == 0 ? 0 : index % layout.rowsPerColumn;
        if (column >= layout.columns) break;
        const int left = layout.columnLeft[column];
        const int top = layout.firstRowTop + row * layout.rowGap;
        drawTextPixels(frame, left, top, rows[index].key, ink, layout.textHeight);
        drawTextPixels(frame, left + layout.columnKeyWidth + keyGap, top, rows[index].label, muted,
                       layout.textHeight);
    }

    centredAt(footer, layout.footerTop, layout.textHeight, muted);
}

namespace {

// Short keys and short labels: at the 20-pixel floor two columns of these are
// what fits a 640-wide frame, and a sheet that runs off the edge is a sheet that
// hides the keys it is there to explain.
constexpr OverlayRow kManualRows[] = {
    {"1 2 3", "View mode"},
    {"WASD", "Move, Shift run"},
    {"Space", "Jump"},
    {"Ctrl", "Grab, crouch"},
    {"Alt", "Prone with Ctrl"},
    {"E", "Interact, toilet"},
    {"T", "Zip / unzip"},
    {"LMB", "Attack, pee"},
    {"RMB", "Aim / block"},
    {"MMB", "Orbit camera"},
    {"G-LMB", "Attack, advanced"},
    {"G-RMB", "Path, Shift run"},
    {"C", "Test room"},
    {"F1", "Needs"},
    {"H", "This sheet"},
    {"F12", "Screenshot"},
};
constexpr int kManualCount = static_cast<int>(sizeof(kManualRows) / sizeof(kManualRows[0]));

}  // namespace

OverlayPanelLayout manualLayout(int frameWidth, int frameHeight) {
    return layoutOverlayPanel(frameWidth, frameHeight, kManualRows, kManualCount, 2, "CONTROLS", "H CLOSES");
}

void drawManual(NativeFrame& frame) {
    const OverlayPanelLayout layout = manualLayout(frame.width, frame.height);
    drawOverlayPanel(frame, layout, kManualRows, kManualCount, "CONTROLS", "H CLOSES");
}

int overlayRowAt(const OverlayPanelLayout& layout, int rowCount, int x, int y) {
    if (layout.rowsPerColumn <= 0) return -1;
    if (y < layout.clickableTop || y >= layout.clickableTop + layout.clickableHeight) return -1;
    const int row = (y - layout.clickableTop) / layout.rowGap;
    for (int column = 0; column < layout.columns; ++column) {
        if (x < layout.columnLeft[column] || x >= layout.columnLeft[column] + layout.columnWidth) continue;
        const int index = column * layout.rowsPerColumn + row;
        return index < rowCount ? index : -1;
    }
    return -1;
}

}  // namespace gtc::native
