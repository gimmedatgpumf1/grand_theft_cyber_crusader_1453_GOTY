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
inline constexpr double kDistanceTolerance = 1e-9;  // metres
// A corner counts as touching the plane when the solve left it there, which is
// within a nanometre of it: the clamp puts it at exactly the plane's height.
inline constexpr double kContactTolerance = 1e-9;  // metres
// How many times a tick's contacts are answered. A contact solve is iterative
// because every impulse changes what the others are closing at -- answering a face
// corner by corner overshoots the second corner by what the first one did, and the
// only way to take that back is to look again with what is already answered
// remembered. It converges in a handful for a face and in a dozen for a body on two
// contacts at once; this is a guard against a numerical corner case, not a tuning.
inline constexpr int kContactPasses = 32;

// What a force does to a speed over one tick, and the only place in the room that
// does it: a = F/m, and the speed gains a*dt.
//
// Gravity is not a separate rule here. Its weight is m*g newtons exactly like a
// contact is some number of newtons, it goes into the same accumulator as every
// other force, and it comes out of this same product. There is no line anywhere in
// the room that adds a speed because something is heavy -- a body that is heavy is
// a body a large force acts on, and that is all.
inline Vec3 accelerated(Vec3 velocity, Vec3 force, double mass) {
    return velocity + force * (kTick / mass);
}

}  // namespace gtc::native::playground_tick
