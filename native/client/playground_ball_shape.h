#pragma once

// The ball's geometry, and the numbers it is made of.
//
// One physical constant lives here and nowhere else. This file exists because the
// one it used to sit in grew past the size a file is allowed to be, and the first
// thing that happens to a constant copied into two files is that the copies
// disagree: a ball with two ideas of how much of a blow it gives back bounces
// differently off the floor than off a shape, for no reason anyone could point at.

#include "client/playground_room.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gtc::native::ball_shape {

constexpr double kTick = 1.0 / 20.0;
constexpr double kGravity = 9.8;
constexpr double kMass = 1.0;  // the dot's mass, and a body corner's
constexpr double kSurfaceY = 0.0;
// The same contact guard the dot uses: a walk that keeps finding contacts stops
// here rather than looping.
constexpr int kMaxContacts = 4;

// The ball's material. Rubber on a wooden floor grips, which is why a ball rolls
// instead of skidding for ever.
constexpr double kBallStaticFriction = 0.70;
constexpr double kBallKineticFriction = 0.55;
// Rolling resistance, F = Crr * N. Rubber on a wooden floor: the ball does not
// roll for ever on a level floor, and this is the coefficient that says how fast
// a roll dies away. It is a property of the pair of materials, measured like mu,
// not a speed below which something changes.
constexpr double kBallRollingResistance = 0.010;
// A basketball is a gas-filled skin: it gives back three quarters of the speed it
// lands with, which is a little over half the height.
constexpr double kBallRestitution = 0.75;
// Air, from the drag equation: half the density of air, the drag coefficient of a
// sphere, and the area the ball presents. F = 0.5 * rho * Cd * A * v^2.
constexpr double kAirDensity = 1.225;  // kg/m^3
constexpr double kBallDragCoefficient = 0.47;
constexpr double kPi = 3.14159265358979323846;

// Is this point inside the outline? The same test the point uses to decide it has
// been left inside a body: it is inside when it is on the inner side of every
// side. Nothing is assumed about the order the sides are wound in beyond that.
bool insideOutline(Vec3 point, const Vec3* corners, std::size_t count);

// Which piece of a body's outline a point is nearest to, and where along it. A
// shape is held up by the piece of it that is touching, not by the shape as a
// whole: a ball under the middle of a long edge carries that edge, and the rest
// of the shape hangs off it.
struct OutlineTouch {
    Vec3 point{};              // the nearest point on the outline
    std::size_t a = 0, b = 0;  // the two corners of that piece
    double t = 0.0;            // how far along it, 0 at a, 1 at b
};
OutlineTouch outlineTouch(Vec3 point, const Vec3* corners, std::size_t count);

// How far a point is from a body's outline as it stands at one fraction of the
// tick, and which way the nearest piece of it faces. Negative means inside.
struct OutlineGap {
    double gap = 0.0;
    Vec3 normal{0, 1, 0};
};
OutlineGap outlineGap(Vec3 point, const Vec3* previous, const Vec3* current, std::size_t count,
                      double fraction);

// How close two surfaces have to be before they are touching rather than merely
// near: nearer than the room resolves. One number for the whole room -- the dot
// and the shapes use it too (playground_room_tick.h) -- because "touching" is one
// question with one answer, and two answers would be a seam to fall through.
constexpr double kTouch = 1e-6;  // metres: a micrometre
// And how far inside counts as inside: a micrometre. The sweep closes in by
// bisection, so where it stops is good to a fraction of the step, not to the last
// bit, and an atom inside must not read back as "inside all along" or every tick
// spends itself on a contact it has already answered.
constexpr double kSkin = 1e-6;
// How far a bracket is closed in on once it is known. The meeting itself is solved
// for outright, so this is only ever used to find a place along a surface, never
// to find a time: a time found by sampling is a time that can be missed between two
// samples, and the ball would be a hair into whatever it passed too fast to see.
constexpr int kBisections = 24;

// The most corners a body in this room has. A scratch array of corners is sized by
// it in the swept tests, so that nothing in a contact allocates.
constexpr std::size_t kMaxOutlineCorners = 4;

// One meeting of the ball's circle with a body's outline.
//
// This is the whole of the ball's collision detection, and it is a hitscan: the
// circle is walked along the tick against the outline where the body is, and what
// comes back is the first instant the two touch. The body is where it is, not where
// it was: the bodies are stepped before the ball and each already swept itself
// against it, so interpolating the body across the ball's sweep as well counted
// that step twice and left the ball reading itself inside a shape that had already
// been pushed clear of it.
struct OutlineHit {
    double along = 2.0;    // where in the tick, 0..1
    Vec3 normal{0, 1, 0};  // which way the surface faces, out of the body
    Vec3 contact{};        // where the two surfaces touch
    double depth = 0.0;    // how far the ball's surface is already through it
    // The ball was already on this surface when the tick began. It is not on its
    // way into it, it is on it: the ball rolls along, and the surface is a contact
    // for the whole tick rather than a meeting part-way through it.
    bool resting = false;
};

// The first fraction of the travel at which the ball's middle comes within `reach`
// of the outline, or 2 if it never does. A face is met where its line comes that
// close; a corner where the middle comes that close to the corner. Both are solved
// outright. Nothing is sampled, so no speed is too fast to see: a ball crossing the
// room in a tick meets whatever lies across its path.
double firstTouch(Vec3 from, Vec3 travel, const Vec3* corners, std::size_t count, double reach);

// Whether the ball's circle meets the outline during this travel, and where. A ball
// already on the surface is a contact for the whole tick: what is swept for then is
// going deeper than the rounding allowance it is already inside by.
bool circleHitsBody(Vec3 from, Vec3 travel, const Vec3* corners, std::size_t count, double radius,
                    OutlineHit& hit);

// The same question asked of a body that is *moving*: the circle travels at
// `ballVelocity`, and the outline travels from `previous` to `current` over one
// tick -- every corner along its own step, which is the body's translation and its
// turn together. Two moving things meet where their relative motion closes, and
// that is the only question a contact may be found with: a shape turning into a
// ball at a radian a second sweeps its outline through twenty millimetres of it in
// a tick, and a sweep that only looks at where the shape finished cannot see the
// ball it has already swallowed.
//
// It is solved by conservative advancement, and it is conservative in the sense the
// word is for: every step is the time the distance has left to close at the speed
// it is closing, so it can stop early (and then it looks again) but never late. The
// ball cannot be overtaken by a face, at any speed either of them is going.
bool sweptBallAgainstMoving(const Vec3* previous, const Vec3* current, std::size_t count,
                            Vec3 ballPosition, Vec3 ballVelocity, double radius, double tick, OutlineHit& hit);

}  // namespace gtc::native::ball_shape
