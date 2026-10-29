#include "client/status_panel.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace gtc::native {
namespace {

constexpr const char* kLabels[kNeedsRowCount] = {"PISS", "SHIT", "HP"};
// Bladder is the near-white green the rest of the interface uses, bowel is the
// colour of the thing itself, health is the one that means trouble.
constexpr const char* kColours[kNeedsRowCount] = {"#d7ff54", "#b07a3c", "#ff7a5e"};

int paddingFor(int textHeight) { return std::max(6, textHeight / 2); }

std::string percent(int value) {
    char text[8];
    std::snprintf(text, sizeof(text), "%d", std::clamp(value, 0, 100));
    return text;
}

}  // namespace

NeedsPanelLayout layoutNeedsPanel(int frameWidth, int frameHeight, int modeBarTop, int modeBarWidth) {
    NeedsPanelLayout layout;
    layout.textHeight = interfaceTextHeight(frameHeight, 10.0);
    const int pad = paddingFor(layout.textHeight);
    layout.rowGap = std::max(3, layout.textHeight / 4);
    layout.rowHeight = layout.textHeight;
    layout.barHeight = std::max(4, layout.textHeight / 2);
    layout.barWidth = layout.textHeight * 4;

    int labelWidth = 0;
    for (const char* label : kLabels) labelWidth = std::max(labelWidth, textWidthPixels(label, layout.textHeight));
    const int valueWidth = textWidthPixels("100", layout.textHeight);
    const int gap = layout.textHeight / 2;

    layout.width = pad * 2 + labelWidth + gap + layout.barWidth + gap + valueWidth;
    layout.height = pad * 2 + kNeedsRowCount * layout.rowHeight + (kNeedsRowCount - 1) * layout.rowGap;

    // Bottom right, on the same line as the mode bar along the bottom edge.
    const int margin = std::max(8, layout.textHeight / 2);
    layout.left = frameWidth - layout.width - margin;
    layout.top = frameHeight - layout.height - margin;
    // Only when they would actually touch: a wide window has room for both along
    // the bottom edge, and moving the panel up there would leave it floating.
    const int modeBarLeft = (frameWidth - modeBarWidth) / 2;
    const bool sideBySide = layout.left >= modeBarLeft + modeBarWidth || layout.left + layout.width <= modeBarLeft;
    if (modeBarTop >= 0 && !sideBySide && layout.top + layout.height + margin > modeBarTop) {
        // Above the mode bar instead of through it. On a 640-wide window there is
        // room for one or the other along the bottom edge, not both.
        layout.top = modeBarTop - margin - layout.height;
    }

    layout.barLeft = layout.left + pad + labelWidth + gap;
    layout.valueRight = layout.left + layout.width - pad;
    for (int index = 0; index < kNeedsRowCount; ++index) {
        layout.rowTop[index] =
            layout.top + pad + index * (layout.rowHeight + layout.rowGap);
    }
    return layout;
}

void drawNeedsPanel(NativeFrame& frame, const NeedsPanelLayout& layout, const NeedsStatus& status) {
    if (layout.width <= 0 || layout.height <= 0) return;
    fillPanel(frame, layout.left, layout.top, layout.width, layout.height, kPanelOpacity);

    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;

    const int values[kNeedsRowCount] = {status.bladder, status.bowel, status.health};
    const Color track = colorHex("#1b2a24");
    for (int index = 0; index < kNeedsRowCount; ++index) {
        const int value = std::clamp(values[index], 0, 100);
        const int top = layout.rowTop[index];
        // The label sits on the row's own baseline, and the bar is centred on it:
        // three rows of mixed heights would look like three unrelated widgets.
        const int textTop = top + (layout.rowHeight - textHeightPixels(layout.textHeight)) / 2;
        drawTextPixels(frame, layout.left + paddingFor(layout.textHeight), textTop, kLabels[index],
                       colorHex(kMutedColour), layout.textHeight);

        const int barTop = top + (layout.rowHeight - layout.barHeight) / 2;
        frame.rectangle(layout.barLeft, barTop, layout.barWidth, layout.barHeight, track);
        const int filled = layout.barWidth * value / 100;
        if (filled > 0) {
            frame.rectangle(layout.barLeft, barTop, filled, layout.barHeight, colorHex(kColours[index]));
        }

        const std::string text = percent(value);
        drawTextPixels(frame, layout.valueRight - textWidthPixels(text, layout.textHeight), textTop, text,
                       colorHex(kInkColour), layout.textHeight);
    }

    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

}  // namespace gtc::native
