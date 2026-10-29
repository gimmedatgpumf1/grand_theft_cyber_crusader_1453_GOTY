#include "client/mode_bar.h"

#include "client/interface_style.h"

#include <algorithm>

namespace gtc::native {
namespace {

constexpr const char* kKeys[3] = {"1", "2", "3"};
constexpr const char* kLabels[3] = {"3RD PERSON", "GOD", "1ST PERSON"};

int indexOf(ViewMode mode) {
    switch (mode) {
        case ViewMode::ThirdPerson: return 0;
        case ViewMode::God: return 1;
        case ViewMode::FirstPerson: return 2;
    }
    return 0;
}

int keyGap(int textHeight) { return textHeight / 2; }
int blockGap(int textHeight) { return textHeight * 3 / 2; }
int padding(int textHeight) { return std::max(6, textHeight / 2); }

}  // namespace

ModeBarLayout layoutModeBar(int frameWidth, int frameHeight) {
    ModeBarLayout layout;
    layout.textHeight = interfaceTextHeight(frameHeight, 11.0);
    const int gap = keyGap(layout.textHeight);
    const int between = blockGap(layout.textHeight);
    const int pad = padding(layout.textHeight);

    int keyWidth = 0;
    for (const char* key : kKeys) keyWidth = std::max(keyWidth, textWidthPixels(key, layout.textHeight));
    int width = pad * 2;
    for (int index = 0; index < 3; ++index) {
        layout.blockWidth[index] = keyWidth + gap + textWidthPixels(kLabels[index], layout.textHeight);
        width += layout.blockWidth[index];
        if (index < 2) width += between;
    }
    layout.width = width + (width % 2);
    // One line of text and the same again for breathing room above and below.
    layout.height = layout.textHeight + pad * 2;
    layout.left = (frameWidth - layout.width) / 2;
    // Against the bottom edge, not floating above it: the strip is an anchor.
    layout.top = frameHeight - layout.height - std::max(8, layout.textHeight / 2);

    // The even-width rounding leaves at most one pixel over; it goes on the right,
    // so the strip never sits a pixel off centre.
    int x = layout.left + pad + (layout.width - width) / 2;
    for (int index = 0; index < 3; ++index) {
        layout.blockLeft[index] = x;
        x += layout.blockWidth[index] + between;
    }
    layout.underlineTop = layout.top + pad + layout.textHeight + layout.textHeight / 4;
    layout.underlineHeight = std::max(1, layout.textHeight / 10);
    return layout;
}

void drawModeBar(NativeFrame& frame, ViewMode mode) {
    const ModeBarLayout layout = layoutModeBar(frame.width, frame.height);
    if (layout.width <= 0 || layout.height <= 0) return;

    fillPanel(frame, layout.left, layout.top, layout.width, layout.height, kPanelOpacity);

    const Color ink = colorHex(kInkColour), muted = colorHex(kMutedColour);
    const int active = indexOf(mode);
    const int gap = keyGap(layout.textHeight);
    for (int index = 0; index < 3; ++index) {
        const bool isActive = index == active;
        const Color colour = isActive ? ink : muted;
        const int keyWidth = textWidthPixels(kKeys[index], layout.textHeight);
        drawTextPixels(frame, layout.blockLeft[index], layout.top + padding(layout.textHeight), kKeys[index], colour,
                       layout.textHeight);
        drawTextPixels(frame, layout.blockLeft[index] + keyWidth + gap, layout.top + padding(layout.textHeight),
                       kLabels[index], colour, layout.textHeight);
        if (!isActive) continue;
        // A rule under the running mode. Colour alone is not enough to find at a
        // glance on a strip this small, and a filled background would turn the
        // bottom of the screen into a toolbar.
        const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
        frame.uiScale = 1.0;
        frame.uiOffsetX = frame.uiOffsetY = 0.0;
        frame.rectangle(layout.blockLeft[index], layout.underlineTop, layout.blockWidth[index],
                        layout.underlineHeight, ink);
        frame.uiScale = scale;
        frame.uiOffsetX = offsetX;
        frame.uiOffsetY = offsetY;
    }
}

}  // namespace gtc::native
