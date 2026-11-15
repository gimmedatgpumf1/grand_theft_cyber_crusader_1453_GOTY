#pragma once

// The rope, and the ball hanging on it.
//
// It is here rather than inside the room's own header because it is a thing of its
// own: the room holds one, steps it, and draws it, and none of the three needs to
// know what the other two do with it.
//
// A rope is not a solid. Nothing collides with it -- a ball thrown across it passes
// through it -- and what it is instead is a constraint: the bob may be anywhere
// inside a sphere of radius `length` about `anchor`, and nowhere outside it. In this
// room a constraint is a force and not a rule of the arithmetic. The rope can pull
// and it cannot push, so its tension is never negative; wherever holding the bob on
// that sphere would need a negative tension the rope simply goes slack, and the bob
// falls free until the rope catches it again. That is what a real rope does.

#include <cstddef>

#include "native.h"

namespace gtc::native {

struct Pendulum {
    Vec3 anchor{3.60, 2.60, 0};
    double length = 1.10;  // metres of rope
    Vec3 position{}, previous{}, shown{}, velocity{};
    double radius = 0.10, mass = 0.85;  // a 20 cm medicine ball on a rope
    // The tick's path when the rope changed its mind part-way through it -- tautened
    // or went slack -- so the drawing can show the two pieces it really took instead
    // of a chord across them.
    bool bent = false;
    double bendAt = 0.0;
    Vec3 bendPoint{}, bendIn{}, bendOut{};
    // What the rope is doing at the end of the tick, and the tension in newtons when
    // it is doing anything. A slack rope pulls with nothing.
    bool taut = false;
    double tension = 0.0;
    // Scene batches: the rope, the bob, the anchor plate, and the mark of where the
    // bob was a tick ago.
    std::size_t ropeBatch = 0, bobBatch = 0, anchorBatch = 0, ghostBatch = 0;
    bool awake = false;
};

}  // namespace gtc::native
