#pragma once

// The prompt under the reticle: what you are looking at, and the key that does
// something to it. Two lines of text, no panel -- the reticle is already the
// pointer, and a box around the words would be a second frame inside the picture.

#include "client/raster.h"

namespace gtc::native {

struct TargetPromptLayout {
    int textHeight = 0;
    int firstLeft = 0;    // "[E] Interact", centred
    int firstTop = 0;
    int secondTop = 0;    // the name of the thing, centred under it
};

// Whether a pointer at `pointerX, pointerY` is on the thing drawn at
// `screenX, screenY`. One radius for the whole game: the world highlight, the
// words under the reticle and the E key all ask this, so none of them can
// disagree about what is being pointed at. Being in range is a separate question.
bool pointerOnTarget(int frameWidth, int frameHeight, double screenX, double screenY, double pointerX,
                     double pointerY);

// `frameWidth`, `frameHeight` are the window; `reticleY` is where the reticle is,
// which is the middle of the window unless a camera is being orbited.
TargetPromptLayout layoutTargetPrompt(int frameWidth, int frameHeight, int reticleY);

// Draws "[E] Interact" and `name` under the reticle. `name` may be null, in which
// case nothing is drawn at all: no target, no prompt.
void drawTargetPrompt(NativeFrame& frame, const TargetPromptLayout& layout, const char* name);

}  // namespace gtc::native
