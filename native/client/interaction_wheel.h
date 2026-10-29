#pragma once

// The interaction circle: the throne's three choices on a ring around a cancel
// button, with the one under the pointer lit up.
//
// It is a real circle and not a row of discs because the middle of it is a
// control: clicking there cancels. A row has no middle, which is why the previous
// version could only be left with a key. The options are placed at a hundred and
// twenty degrees apart so that no two of them are ever nearer to the pointer than
// the cancel in the centre.

#include "client/raster.h"

namespace gtc::native {

constexpr int kWheelOptionCount = 3;

struct InteractionWheelLayout {
    int panelLeft = 0;
    int panelTop = 0;
    int panelWidth = 0;
    int panelHeight = 0;
    int centreX = 0;
    int centreY = 0;
    int ringRadius = 0;      // centre to the middle of an option disc
    int optionRadius = 0;    // the option discs themselves
    int cancelRadius = 0;    // the cancel button in the middle
    int stroke = 0;          // outline thickness
    int textHeight = 0;
    int titleHeight = 0;
    int titleTop = 0;
    int subtitleTop = 0;
    int footerTop = 0;
    int optionCentreX[kWheelOptionCount] = {0, 0, 0};
    int optionCentreY[kWheelOptionCount] = {0, 0, 0};
};

// `subtitle` is "STANDING" or "SEATED" -- what the choice will do to this body.
InteractionWheelLayout layoutInteractionWheel(int frameWidth, int frameHeight, const char* subtitle);

// What is under this point: an option index, kWheelCancel when it is the middle,
// and kWheelOutside when it is neither.
constexpr int kWheelCancel = -1;
constexpr int kWheelOutside = -2;
int interactionWheelHit(const InteractionWheelLayout& layout, int x, int y);

// `pointerX`/`pointerY` are the mouse in window pixels, or anything negative for
// no pointer at all: whatever is under it is drawn lit.
void drawInteractionWheel(NativeFrame& frame, const InteractionWheelLayout& layout, const char* subtitle,
                          int pointerX, int pointerY);

}  // namespace gtc::native
