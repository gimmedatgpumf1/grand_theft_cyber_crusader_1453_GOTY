#pragma once

// The rope.
//
// A rope is the one thing in this room that is a constraint and not a solid. Nothing
// collides with it: a ball thrown across it passes through it. What it does is hold
// the ball at one end of it inside a sphere of radius `length` about `anchor`, and it
// does that the way every other constraint in this room is done -- with a force. The
// rope pulls and never pushes, so its tension is never negative, and wherever holding
// the ball on that sphere would need a negative tension the rope goes slack and the
// ball falls free until the rope catches it again.
//
// It is not a kind of ball and it is not a kind of body: it is a constraint, and the
// thing it holds is a ball -- the same ball the room throws, ticked by the same code
// and drawn by the same code. Nothing here knows that; it only knows a length, an
// anchor and which ball is on the end of it.

#include <cstddef>

#include "native.h"

namespace gtc::native {

struct Rope {
    Vec3 anchor{3.60, 2.60, 0};
    double length = 1.10;  // metres of rope
    // Which of the room's balls is on the end of it, and whether it is on it at all.
    std::size_t ball = 0;
    bool attached = true;
    // What the rope is doing at the end of the tick, and the tension in newtons when
    // it is doing anything. A slack rope pulls with nothing.
    bool taut = false;
    double tension = 0.0;
    // Scene batches: the rope itself and the plate it hangs from. The ball on the end
    // of it is drawn by the ball's own batches, because it is a ball.
    std::size_t ropeBatch = 0, anchorBatch = 0;
};

}  // namespace gtc::native
