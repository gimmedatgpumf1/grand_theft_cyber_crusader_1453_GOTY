#pragma once

// The interaction circle: one disc, cut into three equal sectors, with a cancel
// button where the three meet.
//
// It is one shape, not three circles on a ring: dividing a circle is what makes it
// read as a choice you make by direction, and it means the pointer is always over
// exactly one option -- there is no gap to fall into and no panel to explain it.
// Nothing is written anywhere except the three names, because the only thing a
// player needs to know is which of the three the pointer is on.

#include "client/raster.h"

namespace gtc::native {

constexpr int kWheelOptionCount = 3;

struct InteractionWheelLayout {
    int centreX = 0;
    int centreY = 0;
    int radius = 0;       // the outer edge of the disc
    int cancelRadius = 0; // the cancel button where the sectors meet
    int textHeight = 0;
    int stroke = 0;       // outline thickness
    // Where each name sits: out along the middle of its own sector.
    int labelX[kWheelOptionCount] = {0, 0, 0};
    int labelY[kWheelOptionCount] = {0, 0, 0};
};

InteractionWheelLayout layoutInteractionWheel(int frameWidth, int frameHeight);

// What is under this point: a sector index, kWheelCancel in the middle, or
// kWheelOutside beyond the disc.
constexpr int kWheelCancel = -1;
constexpr int kWheelOutside = -2;
int interactionWheelHit(const InteractionWheelLayout& layout, int x, int y);

// `pointerX`/`pointerY` are the mouse in window pixels; negative means no pointer,
// and then nothing is highlighted. Draws the disc, the sector under the pointer
// lit, and the cancel button.
void drawInteractionWheel(NativeFrame& frame, const InteractionWheelLayout& layout, int pointerX, int pointerY);

}  // namespace gtc::native
