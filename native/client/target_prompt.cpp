#include "client/target_prompt.h"

#include "client/interface_style.h"

#include <algorithm>
#include <string>

namespace gtc::native {
namespace {

constexpr const char* kAction = "[E] Interact";

// The words are drawn twice, once in near-black a pixel or two off, once in the
// reading colour on top. That is what keeps them legible over a pale floor and a
// dark wall without a panel behind them -- and it is the same trick the reticle
// uses, so the two look like parts of one thing.
void shadowedText(NativeFrame& frame, int left, int top, const std::string& text, int textHeight,
                  const Color& ink) {
    const int offset = std::max(1, textHeight / 12);
    const Color shadow = colorHex("#020604");
    for (int dy = -offset; dy <= offset; dy += offset) {
        for (int dx = -offset; dx <= offset; dx += offset) {
            if (dx == 0 && dy == 0) continue;
            drawTextPixels(frame, left + dx, top + dy, text, shadow, textHeight);
        }
    }
    drawTextPixels(frame, left, top, text, ink, textHeight);
}

}  // namespace

bool pointerOnTarget(int frameWidth, int frameHeight, double screenX, double screenY, double pointerX,
                     double pointerY) {
    const double scale = std::min(frameWidth / 640.0, frameHeight / 480.0);
    return std::hypot(screenX - pointerX, screenY - pointerY) <= 75.0 * scale;
}

TargetPromptLayout layoutTargetPrompt(int frameWidth, int frameHeight, int reticleY) {
    TargetPromptLayout layout;
    layout.textHeight = interfaceTextHeight(frameHeight, 11.0);
    const int lineHeight = textHeightPixels(layout.textHeight);
    // Clear of the reticle's lower arm, which reaches seven units down.
    const int clearance = std::max(10, layout.textHeight);
    layout.firstTop = reticleY + clearance;
    layout.secondTop = layout.firstTop + lineHeight + lineGap(layout.textHeight) / 2;
    layout.firstLeft = frameWidth / 2 - textWidthPixels(kAction, layout.textHeight) / 2;
    return layout;
}

void drawTargetPrompt(NativeFrame& frame, const TargetPromptLayout& layout, const char* name) {
    if (name == nullptr || *name == 0) return;
    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;

    shadowedText(frame, layout.firstLeft, layout.firstTop, kAction, layout.textHeight, colorHex(kMutedColour));
    const std::string label(name);
    const int left = frame.width / 2 - textWidthPixels(label, layout.textHeight) / 2;
    shadowedText(frame, left, layout.secondTop, label, layout.textHeight, colorHex(kInkColour));

    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

}  // namespace gtc::native
