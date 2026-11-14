#pragma once

#include <cmath>

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
// The room's resolution: one micrometre.
//
// Nothing in this room is smaller than this. A displacement shorter than it is
// no displacement at all, and two surfaces nearer than it are not merely close,
// they are touching. It is a statement about what the room can resolve, not a
// tolerance: a point at the foot of a slope that "closes" on the edge it is
// sliding along by a fraction of a micrometre in a whole tick is not arriving at
// anything, and a shape that has crept a thousandth of a micron in a tick has not
// moved. Every argument this room has ever had with itself has come from the last
// few significant figures of a double, and they are all below this line.
inline constexpr double kQuantum = 1e-6;  // metres
// How near a surface counts as resting on it. Two surfaces are touching when they are
// nearer than the room can resolve -- a micrometre -- and the band a contact is
// gathered with is the same number, because it is the same question: a face a
// hundredth of a millimetre off the floor is not touching the floor, and answering it
// as a contact is answering something that is not there.
// And the slowest motion that exists: the speed that covers a quantum in a tick.
// Anything slower does not move this tick, so its speed is nothing.
inline constexpr double kQuantumSpeed = kQuantum / kTick;  // m/s
inline constexpr double kContactBand = kQuantum;  // metres
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
// How far apart the two ends of one face may stand above a surface and still be the
// same contact rather than a shape coming down on a corner. A millimetre on a shape
// seven hundred of them across is a seventh of a degree: the noise a resting pair
// sits in, and not a shape landing. Anything more is a corner.
//
// It is also the thickness within which a corner counts as being *at* a surface for
// the purpose of gathering a patch, which is why it is here and not in the solver:
// a face lying on the floor has one end a ten-thousandth of a millimetre above it,
// and that end belongs to the contact -- the face is one contact, and answering it
// at one corner makes it rock from corner to corner for ever.
inline constexpr double kFaceFlat = 1e-3;  // metres

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

// What is left of a speed once the room's resolution is applied: a speed that
// cannot carry something a whole micrometre in a tick is not a speed, it is the
// arithmetic talking to itself.
inline Vec3 resolved(Vec3 velocity) {
    return length(velocity) * kTick < kQuantum ? Vec3{} : velocity;
}

// And a shape's turn, measured where it shows: the furthest corner of a body is
// the furthest anything on it travels for a given angle. A turn that carries that
// corner less than a micrometre has not turned.
inline double resolvedTurn(double spin, double furthest) {
    return std::abs(spin) * furthest * kTick < kQuantum ? 0.0 : spin;
}

// Where a move of less than a micrometre goes: nowhere. The position it started
// at *is* the position it arrives at.
inline Vec3 resolvedMove(Vec3 from, Vec3 to) {
    return length(to - from) < kQuantum ? from : to;
}

// Whether two surfaces are touching: nearer than the room can resolve.
inline bool touching(double gap) {
    return gap < kQuantum;
}

}  // namespace gtc::native::playground_tick
