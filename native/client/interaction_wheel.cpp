#include "client/interaction_wheel.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace gtc::native {
namespace {

constexpr const char* kKeys[kWheelOptionCount] = {"1", "2", "3"};
constexpr const char* kLabels[kWheelOptionCount] = {"SIT", "SHIT", "PISS"};
// Every option's text, so the discs are sized by the longest of them rather than
// by whichever one happened to be measured.
constexpr const char* kTexts[kWheelOptionCount] = {"1 SIT", "2 SHIT", "3 PISS"};

// Up, lower right, lower left. Evenly spaced, so the middle is equally far from
// all three.
constexpr double kAngles[kWheelOptionCount] = {-90.0, 30.0, 150.0};

int widestText(int textHeight) {
    int width = 0;
    for (const char* text : kTexts) width = std::max(width, textWidthPixels(text, textHeight));
    return width;
}

}  // namespace

InteractionWheelLayout layoutInteractionWheel(int frameWidth, int frameHeight, const char* subtitle) {
    InteractionWheelLayout layout;
    layout.textHeight = interfaceTextHeight(frameHeight, 13.0);
    layout.titleHeight = interfaceTextHeight(frameHeight, 17.0);

    // A disc has to hold its own label with room to breathe; the floor is a tenth
    // of the smaller side so the wheel still reads on a small window.
    const int labelWidth = widestText(layout.textHeight);
    const int smaller = std::min(frameWidth, frameHeight);
    layout.optionRadius = std::max(labelWidth / 2 + layout.textHeight, smaller / 10);
    layout.ringRadius = layout.optionRadius * 7 / 4;
    layout.cancelRadius = std::max(layout.textHeight, layout.optionRadius / 2);
    layout.stroke = std::max(2, layout.textHeight / 8);

    const int reach = layout.ringRadius + layout.optionRadius;
    const int pad = std::max(10, layout.textHeight);
    const int gap = lineGap(layout.textHeight);
    int width = reach * 2 + pad * 2;
    width = std::max(width, textWidthPixels("CLICK THE MIDDLE TO CANCEL", layout.textHeight) + pad * 2);
    layout.panelWidth = width + (width % 2);
    const int wheelHeight = reach * 2;
    layout.panelHeight = pad + layout.titleHeight + gap / 2 + layout.textHeight + gap + wheelHeight + gap +
        layout.textHeight + pad;
    layout.panelLeft = (frameWidth - layout.panelWidth) / 2;
    layout.panelTop = (frameHeight - layout.panelHeight) / 2;
    (void)subtitle;

    layout.titleTop = layout.panelTop + pad;
    layout.subtitleTop = layout.titleTop + layout.titleHeight + gap / 2;
    // The wheel is centred in the frame, not in the panel: the panel has a title
    // above it and a footer below, and the circle is what the pointer aims at.
    layout.centreX = frameWidth / 2;
    layout.centreY = layout.subtitleTop + layout.textHeight + gap + wheelHeight / 2;
    layout.footerTop = layout.centreY + wheelHeight / 2 + gap;

    for (int index = 0; index < kWheelOptionCount; ++index) {
        const double radians = kAngles[index] * 3.141592653589793 / 180.0;
        layout.optionCentreX[index] =
            layout.centreX + static_cast<int>(std::lround(std::cos(radians) * layout.ringRadius));
        layout.optionCentreY[index] =
            layout.centreY + static_cast<int>(std::lround(std::sin(radians) * layout.ringRadius));
    }
    return layout;
}

int interactionWheelHit(const InteractionWheelLayout& layout, int x, int y) {
    const long dx = x - layout.centreX, dy = y - layout.centreY;
    // The cancel is tested first and by nearest: a click between the centre and an
    // option belongs to whichever of the two it is actually closer to, which keeps
    // the middle of the wheel a cancel button instead of a dead zone.
    for (int index = 0; index < kWheelOptionCount; ++index) {
        const long ox = x - layout.optionCentreX[index], oy = y - layout.optionCentreY[index];
        if (ox * ox + oy * oy <= static_cast<long>(layout.optionRadius) * layout.optionRadius) {
            return index;
        }
    }
    if (dx * dx + dy * dy <= static_cast<long>(layout.cancelRadius) * layout.cancelRadius) return kWheelCancel;
    return kWheelOutside;
}

void drawInteractionWheel(NativeFrame& frame, const InteractionWheelLayout& layout, const char* subtitle,
                          int pointerX, int pointerY) {
    if (layout.panelWidth <= 0 || layout.panelHeight <= 0) return;
    fillPanel(frame, layout.panelLeft, layout.panelTop, layout.panelWidth, layout.panelHeight, kMenuPanelOpacity);

    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;

    const Color ink = colorHex(kInkColour), muted = colorHex(kMutedColour), accent = colorHex("#d7ff54");
    const Color cancel = colorHex("#ff7a5e");
    const auto centredAt = [&](const std::string& text, int left, int width, int top, int height, Color colour) {
        drawTextPixels(frame, left + (width - textWidthPixels(text, height)) / 2, top, text, colour, height);
    };
    centredAt("TOILET", layout.panelLeft, layout.panelWidth, layout.titleTop, layout.titleHeight, ink);
    if (subtitle != nullptr) {
        centredAt(subtitle, layout.panelLeft, layout.panelWidth, layout.subtitleTop, layout.textHeight, muted);
    }

    // A dark disc under the ring: three outlined circles over a chequerboard floor
    // is a diagram, not a control. This is the one place the interface darkens the
    // whole circle rather than a strip of it, because the pointer has to find three
    // targets in the middle of the screen.
    fillDisc(frame, layout.centreX, layout.centreY, layout.ringRadius + layout.optionRadius,
             colorHex(kPanelColour), 0.55);

    const int hovered = pointerX < 0 || pointerY < 0 ? kWheelOutside
                                                     : interactionWheelHit(layout, pointerX, pointerY);

    for (int index = 0; index < kWheelOptionCount; ++index) {
        const int cx = layout.optionCentreX[index], cy = layout.optionCentreY[index];
        const bool lit = hovered == index;
        if (lit) {
            // Lit means filled, ringed and bright: colour alone is not enough to
            // tell which of three identical discs the pointer is on.
            fillDisc(frame, cx, cy, layout.optionRadius, accent, 0.26);
            ringDisc(frame, cx, cy, layout.optionRadius, layout.stroke * 2, accent, 0.9);
        } else {
            ringDisc(frame, cx, cy, layout.optionRadius, layout.stroke, muted, 0.55);
        }
        const std::string label = std::string(kKeys[index]) + " " + kLabels[index];
        const int textHeight = layout.textHeight;
        centredAt(label, cx - layout.optionRadius, layout.optionRadius * 2,
                  cy - textHeightPixels(textHeight) / 2, textHeight, lit ? ink : muted);
    }

    // The middle: a ring, a cross, and a warning colour when it is hovered. This
    // is the control that leaves the wheel.
    const bool cancelLit = hovered == kWheelCancel;
    if (cancelLit) fillDisc(frame, layout.centreX, layout.centreY, layout.cancelRadius, cancel, 0.42);
    ringDisc(frame, layout.centreX, layout.centreY, layout.cancelRadius, layout.stroke, cancelLit ? cancel : muted,
             cancelLit ? 0.95 : 0.6);
    const int cross = layout.cancelRadius * 2 / 3;
    const int thickness = std::max(2, layout.stroke);
    for (int step = -cross; step <= cross; ++step) {
        frame.rectangle(layout.centreX + step, layout.centreY + step, thickness, thickness,
                        cancelLit ? ink : muted);
        frame.rectangle(layout.centreX - step, layout.centreY + step, thickness, thickness,
                        cancelLit ? ink : muted);
    }

    centredAt("CLICK THE MIDDLE TO CANCEL", layout.panelLeft, layout.panelWidth, layout.footerTop,
              layout.textHeight, muted);

    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

}  // namespace gtc::native
