#pragma once

// The numbers a body's tick is solved with.
//
// They live here rather than in one of the two files that solve a body, because
// a body's step and the room that steps it have to agree about how long a tick
// is and how close to its own length a fixed distance has to be before it counts
// as solved. Two copies of that agreement would drift apart, and the symptom
// would be a shape that is a fraction of a millimetre different depending on
// which file happened to look at it.

namespace gtc::native::playground_tick {

// One tick is 1/20 s and it is the only time step in this room. A velocity in
// m/s covers v/20 metres in a tick, because a tick is a twentieth of a second.
inline constexpr double kTick = 1.0 / 20.0;
inline constexpr double kMass = 1.0;      // kg
inline constexpr double kGravity = 9.8;   // m/s^2
inline constexpr double kSurfaceY = 0.0;  // the plane, y = 0
// The distance solve runs until every distance is back to its rest length,
// not for a chosen number of passes. The bound is only a guard against a
// numerical corner case looping forever.
inline constexpr int kMaxDistancePasses = 256;
// How many times the tick's step is re-swept along a surface it has met. Each pass
// spends the part of the tick up to the first place anything is met, so a body that
// is leaning on something keeps sliding along it for the rest of the tick instead
// of stopping dead at the first contact.
inline constexpr int kSlidePasses = 8;
inline constexpr double kDistanceTolerance = 1e-9;  // metres
// A corner counts as touching the plane when the solve left it there, which is
// within a nanometre of it: the clamp puts it at exactly the plane's height.
inline constexpr double kContactTolerance = 1e-9;  // metres

}  // namespace gtc::native::playground_tick
