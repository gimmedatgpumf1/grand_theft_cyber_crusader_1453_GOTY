#pragma once

// A ball, and everything the room writes down about one while it is being ticked.
//
// There is one kind of ball in this room and it is this. The ball that is thrown and
// the ball that hangs on the rope are the same struct, stepped by the same tick,
// answered by the same contacts and drawn by the same code; the rope is a constraint
// *on* a ball and is not part of it. Nothing about a ball that is on a rope is
// special, which is why none of this is.
//
// The book-keeping of a ball's own tick lives with the ball and not with the room --
// what it is against this tick, what each of those faces is carrying, what was dealt
// to it where, what is holding it up. A room that keeps one of each can only have one
// ball, and this room has two.

#include <array>
#include <cstddef>

#include "native.h"

namespace gtc::native::playground_ball {

// A surface a ball is against this tick, said once.
//
// It is not a copy of a contact: it is what the ball's grip is solved from, all of
// the ball's faces together, because a grip at one surface changes what is left to
// slip at every other one.
struct BallTouch {
    Vec3 normal{}, surfaceVelocity{};
    // What this face is carrying, in newton seconds. It is not a property of the face
    // on its own: it is what the face has to hold up, which is a question about the
    // whole stack -- see settleBallFriction().
    double pressed = 0.0;
    // Which body is on the other side, or the floor's slot for the floor. A face over
    // the ball is carrying the weight of what stands on it, and that is read from the
    // body: a floor patch beneath a ball carries the ball, but a cube resting on top
    // of one carries the cube.
    std::size_t body = 0;
    // What the meeting dealt the ball, in newton seconds, and the momentum the arrival
    // was worth. Bounciness is not a fraction of a speed: it is the impulse a surface
    // gives back over the impulse that would just have stopped the ball, and it is
    // struck at one instant -- so both numbers are the ones the solver used at that
    // instant, and neither is read across a tick, where gravity alone is half a metre
    // a second of momentum that has nothing to do with the surface. See ballContact().
    double given = 0.0;
    double arrival = 0.0;
};

// A surface that is holding the ball up, as the constraint it is.
//
// The ball is one rigid body, and the shapes are solved against it before its own tick
// runs, so for the length of that solve the question "may the ball move that way" has
// to have a real answer. It used to have a fraction -- the share of a push along the
// surface under the ball -- and using a fraction of a *vector* is not the same thing as
// taking one component out of it. So the surfaces under the ball are gathered as
// normals, and both halves of the answer are read off one projection of them: the
// translation is the push with those normals taken out, and the turn is the turn of the
// whole push (a support's answer runs through the middle and adds no moment).
//
// The floor is one of these. So is a taut rope: a rope that holds a ball up is exactly
// as much a surface the ball may not be pushed into, and saying so is what lets a shape
// press a hanging ball and be answered by the rope instead of by nothing.
struct BallSupport {
    Vec3 normal{};  // out of the surface, into the ball
    std::size_t body = 0;
};

// One ball: one point with a radius, its turn, and its own account of the tick.
struct Ball {
    Vec3 position{}, previous{}, shown{}, velocity{};
    // The speed the ball had when the tick began, before this tick's forces. A bounce
    // is the answer to hitting something, so it is measured against what the ball
    // arrived with -- not against the speed gravity lent it on the way, which a ball
    // already lying on the floor has too.
    Vec3 arrival{};
    // And the speed it had when its *own* tick began, which is not the same number once
    // something else has already dealt with it this tick. The arrival is read for the
    // whole room before anything moves; a ball struck by the other ball during that
    // other ball's walk is doing the blow's speed by the time its own turn comes, and
    // that -- not the arrival -- is where its own tick starts from.
    Vec3 began{};
    // Whether the ball *arrived* at something this tick -- met a surface it was not
    // already against, or a hand, or a sleep that nothing woke. That is the difference
    // between a blow and a contact being kept, and it is the one question the rope
    // asks: a blow is the room's answer and the rope may not throw it away, while a
    // contact being kept is a force the pendulum carries and the rope's own arc is
    // still the motion. Nothing else in the room reads it.
    //
    // Read as "the ball was touched this tick" it was wrong in the way that matters:
    // a ball hanging on its rope a millimetre off the triangle's slope is touched every
    // tick and never arrives at anything, and handing every one of those ticks to the
    // room left the contact answering the ball's fall again and again -- the ball was
    // answered at 0.35 m/s for a meeting it came to at 0.039, the rope put it back on
    // its circle each tick, and the swing grew by a quarter of a metre a second a tick
    // out of nothing. A kept contact is not an arrival, and the rope is what says so.
    bool answered = false;
    // A new impact, as distinct from a contact that was already being kept.
    bool impacted = false;
    // The speed this tick's own account leaves the ball with: its weight and the air, and
    // nothing else. Whatever the ball's speed has beyond this was given to it by something
    // in the room -- a face, the floor, the other ball, a hand, a shape's own solver -- and
    // that is the difference between a tick the ball spent falling and a tick something
    // answered it. The rope reads it, and nothing else does.
    //
    // It is a separate question from `answered` on purpose: the walk below is not the only
    // place a ball is answered. A shape leaning on the ball, a shape standing on it, the
    // pair being solved against each other -- all of that happens in the shapes' half of
    // the tick, before the ball's own, and none of it passes through the walk. Read as
    // "the walk met something", the rope re-derived the tick and threw away the solver's
    // answer, which is a ball that keeps its arc while the shape it is against is handed
    // the blow: infinite force in one direction and none in the other. So the test is the
    // one the room can always make about itself: is the ball's speed still the speed its
    // own weight and the air would have left it with?
    Vec3 alone{};
    // Where the tick bent. A ball that meets something takes the answer there and
    // spends the rest of the tick going elsewhere, so its path through a tick is two
    // straight pieces and not one. Drawn as one piece it never goes near the surface: a
    // ball landing at eight metres a second is at eight tenths of a metre above the
    // face at the frame before the meeting and a quarter of a metre above it at the
    // frame after, and it looks for all the world as if it bounced on air. It did not
    // -- it bounced where it was told to, between two frames. This is the record of
    // that meeting, so the drawing can follow the path that was actually taken. The
    // physics does not read any of it.
    bool bent = false;
    double bendAt = 0.0;  // the fraction of the tick the meeting happened at
    Vec3 bendPoint{};     // where the ball was when it happened
    Vec3 bendIn{};        // what it was doing up to the meeting
    Vec3 bendOut{};       // and what it was doing after it
    double radius = 0.12;  // basketball: 0.24 m across
    double mass = 0.62;    // kg, a real basketball
    double spin = 0.0;     // radians per second about z
    // The angle the simulation has actually turned through, and the one the screen is
    // shown. They are separate because the drawing spreads a tick over the frames
    // between ticks; writing that back into `angle` would throw away most of every
    // tick's turn.
    double angle = 0.0, previousAngle = 0.0, shownAngle = 0.0;
    // The forces on the ball this tick, in newtons, before the integrator.
    Vec3 force{};
    bool frozen = false, touching = false;
    // Whether static friction could hold the ball at every surface it touched this
    // tick. A slope cannot, so a ball on one is never at rest.
    bool standing = true;
    // The way out of the surface that is under it, from the last contact that could
    // carry it. A ball at rest is pressed at that point and nowhere else, so this is
    // where its weight comes back up, and the arrow on F is drawn there -- not under
    // the middle of a ball that may be sitting on a slope.
    Vec3 supportNormal{0, 1, 0};
    std::size_t faceBatch = 0, markBatch = 0;

    // --- this tick's account, cleared at the start of it ---------------------------

    // How many surfaces a ball can be against at once, and how many the room's bodies
    // plus the floor can be: the meeting records are kept per surface, and a ball may
    // not be able to keep one for every surface it could meet.
    static constexpr std::size_t kMaxTouches = 8;
    static constexpr std::size_t kMaxSupports = 4;
    static constexpr std::size_t kSurfaces = 8;
    std::array<BallTouch, kMaxTouches> touches{};
    std::size_t touchCount = 0;
    // The meeting this tick that dealt the ball the most, kept in force past the end of
    // the tick: what it gave the ball and what the ball's arrival was worth, in newton
    // seconds, one record per surface it met -- the floor at its own slot and a shape at
    // its index. The touch list is consumed by the grip before the tick is over, and a
    // bounce is a thing you have to be able to look at afterwards; this is the one
    // record of it that survives.
    std::array<double, kSurfaces> meetingGiven{};
    std::array<double, kSurfaces> meetingArrival{};
    // The surfaces under it, as normals.
    std::array<BallSupport, kMaxSupports> supports{};
    std::size_t supportCount = 0;
    // Where every shape was when this ball's half of the tick began: its speed and its
    // turn, as the tick's own solve of the shapes left them. A face the ball's grip is
    // measured against is the face the ball met, and that is where it was standing when
    // the ball arrived at it -- not where the ball's own blow had pushed it a moment
    // later. Read live, the grip sees its own push coming back at it.
    std::array<Vec3, kSurfaces> tickSpeed{};
    std::array<double, kSurfaces> tickSpin{};
    // Every blow the ball has been given this tick, summed, in newton seconds. The
    // ball's account is closed against this: what actually happened to it, as opposed
    // to what any one contact says it should have.
    Vec3 blows{};
    // How many times this ball was found already through a shape at the start of its
    // own tick, and lifted out sideways. Nothing in this room is ever driven out of
    // anything -- it never gets in -- so both of these exist to be counted by the
    // checks, and a tick that increments one has failed at something.
    std::size_t foundThrough = 0, liftedOut = 0;
};

}  // namespace gtc::native::playground_ball
