#pragma once

#include "client/raster.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace gtc::native {
class RoomFreecam;
struct ViewState;

// The room's physics.
//
// A solid in this room is a rigid body: one mass, one moment of inertia, a place
// and a turn, and a speed and a spin. Its corners are not simulated points that
// have to be kept at a fixed distance from each other -- they are computed from
// the shape and from where the body is, so a body cannot be out of shape. Nothing
// in the room ever moves a corner: an impulse is given to the body at a place,
// and what that does to the body is
//
//     v += J/m           w += (r x J)/I,        r = the place -- the centre of mass
//
// and the place of a corner is then read off. Gravity is the same: m*g newtons at
// every corner, which is m*g at the centre of mass and no torque at all, because
// the weight of each corner runs through the middle.
//
// A tick is one step, and the step is the tick. The forces are given to the bodies
// as impulses, every contact the tick's own travel can reach is gathered and solved
// -- each end of a contact against the distance it still has to travel, so a shape
// arriving at a surface is left *on* it -- and then every body is carried by the
// speed it is left with for exactly one tick. Because the set of contacts is
// everything that could arrive before the tick ends, nothing can be walked into
// anything, and the only thing that ever moves a body along is its own speed. There
// is no velocity read back out of a position and no state in which a shape is held
// still while its speed keeps growing. The one exception is a corner carried into a
// surface by a *turn*, which a straight step cannot see: that is put back out by
// position alone, at the end of the tick, and there is normally nothing for it to do.
class PlaygroundRoom {
public:
    // The bodies in the room, in the order they are simulated.
    static constexpr std::size_t kCube = 0, kTriangle = 1, kSeesaw = 2, kBodyCount = 3;
    // The most corners a shape in the room has. A body's corners are the shape, and
    // the load it stands on is worked out one per corner, so this is also the number
    // of contacts a resting body can carry.
    static constexpr std::size_t kMaxCorners = 4;
    // Puts the point back at the top of the room, with the velocity it starts
    // with. Zero velocity is the plain drop; anything else is a launch.
    bool reset(Vec3 start = {0, 1.9, 0}, Vec3 startVelocity = {});
    // Moves the point alone, leaving the solids where they are.
    void resetPoint(Vec3 start, Vec3 startVelocity = {});
    // Hands the frame's elapsed time to the fixed 20 Hz tick accumulator. No
    // tick is ever split and no tick is ever run twice.
    void advance(double seconds);
    void setDragRay(Vec3 origin, Vec3 direction);
    void beginDrag();
    void endDrag() { dragTarget_ = DragTarget::None; }
    bool render(NativeFrame& frame, const ViewState& view, const RoomFreecam& camera);
    // What is on screen this frame: the point interpolated between the two
    // ticks that bracket it, at the fraction of the tick the frame falls on.
    Vec3 displayed() const { return shown_; }
    // Where the point is and how fast it is moving, for the visual harness.
    Vec3 point() const { return point_; }
    Vec3 velocity() const { return velocity_; }
    bool resting() const { return resting_; }
    bool pointFrozen() const { return pointFrozen_; }
    // A body, as drawn and as simulated: where its corners are, how many it has,
    // and whether it has come to rest.
    Vec3 bodyCorner(std::size_t body, std::size_t index) const;
    Vec3 bodySimulatedCorner(std::size_t body, std::size_t index) const;
    Vec3 bodyCornerVelocity(std::size_t body, std::size_t index) const;
    std::size_t bodyCorners(std::size_t body) const { return body < kBodyCount ? bodies_[body].count : 0; }
    Vec3 bodyMiddle(std::size_t body) const;
    // The body's own inertia and how fast its centre is going, so that what the
    // ledger says a body feels can be checked against what the tick did to it.
    double bodyMass(std::size_t body) const { return body < kBodyCount ? bodies_[body].mass : 0.0; }
    Vec3 bodyCentreVelocity(std::size_t body) const {
        return body < kBodyCount ? bodies_[body].velocity : Vec3{};
    }
    static constexpr std::size_t kBallOwner = kBodyCount, kDotOwner = kBodyCount + 1;
    bool bodyFrozen(std::size_t body) const { return body < kBodyCount ? bodies_[body].frozen : true; }
    // Forces, as they are applied, and the room's energy.
    //
    // Every tick the room writes down every force it applies: where it acts, which
    // way, and how hard in newtons. Nothing in the simulation is allowed to move a
    // corner except a force written here, so this is the room's own account of what
    // it is doing -- not a separate diagnostic that could drift from the physics.
    // The arrows drawn on F are these numbers, and the figures on N are the same
    // numbers again, so what is on screen cannot disagree with what was solved.
    // Friction is its own line in the ledger and not part of the contact's. It is a
    // different force -- one along the surface, holding or sliding -- so it gets its
    // own arrow, with its own length and its own account, and the two can be read
    // apart: the normal is what presses, the friction is what holds.
    // Rolling resistance is its own kind because it is its own thing. Friction is
    // a force along a surface that acts to stop one face sliding on another;
    // rolling resistance is a *couple* that exists because a surface and a ball
    // both give a little where they meet, so the ground's push is not centred on
    // the bottom of the ball but a little ahead of it, and that offset is a moment
    // that turns the roll slower. It acts on a ball that is rolling perfectly, with
    // no sliding anywhere to rub, which is exactly why friction can never account
    // for it.
    enum class ForceKind {
        Gravity = 0, Contact = 1, Friction = 2, Pair = 3, Ball = 4, Drag = 5, Rolling = 6, KindCount = 7
    };
    // Room for a whole tick's worth of every kind at once: two bodies of four
    // corners, the dot, the ball, and the contacts and pairs between them. The
    // number is a bound on the array, not a cut: nothing is ever dropped quietly,
    // because a ledger that loses entries is worse than no ledger at all.
    static constexpr std::size_t kMaxForceSamples = 256;
    static constexpr std::size_t kArrowsPerKind = 20;
    struct ForceSample {
        Vec3 at{};     // where it acts, in metres
        Vec3 force{};  // newtons
        ForceKind kind = ForceKind::Gravity;
        // Which solid feels it: 0..kBodyCount-1 for the room's bodies, kBodyCount
        // for the ball, kBodyCount+1 for the dot. Without this a pair contact reads
        // as two identical arrows in two places -- one on each shape -- and there is
        // no way to add up what any one of them feels.
        std::size_t owner = 0;
        // A couple, in newton metres about z, for the one kind of thing in this
        // room that is a moment and not a force. `force` is then only how the
        // moment is drawn: a couple has no point to be drawn at, so it is shown as
        // the force that would be its equal at the rolling radius, M/r, at the
        // place the ball meets the ground. `couple` says as much, so nothing that
        // adds the ledger up mistakes it for a force.
        double moment = 0.0;
        bool couple = false;
    };
    void toggleForceArrows() { showForces_ = !showForces_; }
    void toggleForceNewtons() { showNewtons_ = !showNewtons_; }
    bool forceArrowsShown() const { return showForces_; }
    bool forceNewtonsShown() const { return showNewtons_; }
    const std::array<ForceSample, kMaxForceSamples>& forces() const { return forces_; }
    std::size_t forceCount() const { return forceCount_; }
    // What the room's energy is made of: the motion of every corner and the ball,
    // and the height each of them is at. A tick may move energy between the two; it
    // may never make any. The sum is what N shows, so a solver that invents motion
    // is visible as a rising total and nothing else has to be trusted.
    void energyBudget(double& kinetic, double& potential) const;
    double totalEnergy() const;

    // How many times the point has struck a body, for the visual harness.
    int bodyContacts(std::size_t body) const { return body < kBodyCount ? bodies_[body].contacts : 0; }
    // Which body, if any, is holding the point up; -1 when the plane does.
    int supportBody() const { return restingBody_; }
    // Which surface is holding the point up: a ball, as opposed to a body or the floor.
    bool supportedByBall() const { return restingOnBall_; }
    // The ball: one point with a radius, its spin, and the angle it is drawn at.
    Vec3 ballPosition() const { return ball_.position; }
    Vec3 ballShown() const { return ball_.shown; }
    Vec3 ballVelocity() const { return ball_.velocity; }
    double ballRadius() const { return ball_.radius; }
    double ballSpin() const { return ball_.spin; }
    double ballAngle() const { return ball_.angle; }
    double ballShownAngle() const { return ball_.shownAngle; }
    bool ballFrozen() const { return ball_.frozen; }

private:
    // What the mouse is holding, if anything.
    enum class DragTarget { None, Point, Body, Ball };

    // A ball is one point with a radius, not a set of corner points. It carries
    // spin as well as speed: a sphere that slid without turning would be slowed
    // like a brick, which is not what a ball does.
    struct Ball {
        Vec3 position{}, previous{}, shown{}, velocity{};
        // The speed the ball had when the tick began, before this tick's forces.
        // A bounce is the answer to hitting something, so it is measured against
        // what the ball arrived with -- not against the speed gravity lent it on
        // the way, which a ball already lying on the floor has too.
        Vec3 arrival{};
        // Where the tick bent. A ball that meets something takes the answer there and
        // spends the rest of the tick going elsewhere, so its path through a tick is
        // two straight pieces and not one. Drawn as one piece it never goes near the
        // surface: a ball landing at eight metres a second is at eight tenths of a
        // metre above the face at the frame before the meeting and a quarter of a
        // metre above it at the frame after, and it looks for all the world as if it
        // bounced on air. It did not -- it bounced where it was told to, between two
        // frames. This is the record of that meeting, so the drawing can follow the
        // path that was actually taken. The physics does not read any of it.
        bool bent = false;
        double bendAt = 0.0;   // the fraction of the tick the meeting happened at
        Vec3 bendPoint{};      // where the ball was when it happened
        Vec3 bendIn{};         // what it was doing up to the meeting
        Vec3 bendOut{};        // and what it was doing after it
        double radius = 0.12;   // basketball: 0.24 m across
        double mass = 0.62;     // kg, a real basketball
        double spin = 0.0;      // radians per second about z
        // The angle the simulation has actually turned through, and the one
        // the screen is shown. They are separate because the drawing spreads a
        // tick over the frames between ticks; writing that back into `angle`
        // would throw away most of every tick's turn.
        double angle = 0.0, previousAngle = 0.0, shownAngle = 0.0;
        // The forces on the ball this tick, in newtons, before the integrator.
        Vec3 force{};
        bool frozen = false, touching = false;
        // Whether static friction could hold the ball at every surface it touched
        // this tick. A slope cannot, so a ball on one is never at rest.
        bool standing = true;
        // The way out of the surface that is under it, from the last contact that
        // could carry it. A ball at rest is pressed at that point and nowhere else,
        // so this is where its weight comes back up, and the arrow on F is drawn
        // there -- not under the middle of a ball that may be sitting on a slope.
        Vec3 supportNormal{0, 1, 0};
        std::size_t faceBatch = 0, markBatch = 0;
    };
    // A solid: one rigid body, with a mass, a moment of inertia, a place, a turn,
    // a speed and a spin. Its corners are the shape, not the state.
    struct Body {
        std::size_t count = 0;
        // The shape about the body's own centre of mass, at zero turn. It never
        // changes: a body is not stretched, bent or repaired, ever.
        std::array<Vec3, kMaxCorners> local{};
        // Where it is and how far it has turned. These two are the whole of its
        // position, and the corners are computed from them on demand.
        Vec3 centre{}, previousCentre{};
        double angle = 0.0, previousAngle = 0.0;
        // What it is doing: the speed of its centre of mass, and its turn about
        // that centre in radians per second.
        Vec3 velocity{};
        double spin = 0.0;
        // What it arrived with, before this tick's forces changed it. A bounce is
        // the answer to an arrival: the weight it gains on the way down is what
        // carried it here, and bouncing that back would be bouncing it off nothing --
        // and it is not nothing at rest, where the whole of the speed is the tick's
        // own gravity, so a shape resting on the ball was bounced a little every
        // tick and stood humming a centimetre above it.
        Vec3 arrivalVelocity{};
        double arrivalSpin = 0.0;
        // What it is: the mass of the whole body and the moment of inertia about
        // its centre of mass, both from the shape as a uniform plate.
        double mass = 1.0, moment = 1.0;
        // The corners of this tick and of the last, and the ones the screen shows.
        // They are placeCorners() of the two above, kept so that the contacts, the
        // dot and the drawing can read them without recomputing.
        std::array<Vec3, kMaxCorners> position{}, previous{}, shown{};
        // This body's own friction, from its shape's material.
        double staticFriction = 0, kineticFriction = 0;
        bool frozen = false;
        // Woken during this tick, by a load arriving on it or by its support going
        // away. It was stepped, so its forces are the ones the solver wrote and not a
        // replay of its last waking tick -- counting both would draw its weight twice.
        bool woken = false;
        int contacts = 0;
        // Whether the ball is one of the things holding this body up this tick, and
        // whether it was last tick. A bounce is an *event*: it is the answer to
        // arriving at a surface, and the coefficient of restitution is what turns the
        // arrival round. A contact that goes on being a contact is not arriving any
        // more -- it is resting -- and asking it to give the arrival back again every
        // tick is a pump. It is easy to miss, because each tick's increment is small:
        // a weight settling onto the ball gains the tick's gravity, the ball gives
        // three quarters of it straight back, the body falls onto the ball again with
        // that much more, and the pair climb for ever. Whatever comes out of the
        // bounce then has to obey the grip, which turns it into a spin, which is the
        // squeeze the King saw.
        bool onBall = false, wasOnBall = false;
        // Whether this body is pinned at a point and can only turn about it. A
        // seesaw is a plank on an axle: the axle holds its middle in one place, so
        // the plank cannot be carried anywhere, and the one thing it can do is turn.
        // That is not a rule bolted onto the motion -- it is the constraint the axle
        // is, and it enters the physics in the one place every constraint does, as
        // how much the body gives when it is pushed: nothing along any direction,
        // and its usual share of a turn about the pin.
        //
        // Nothing else needs to know. Its weight acts at its middle, which is the pin,
        // so its weight gives no torque and it hangs in neutral balance wherever it is
        // left -- which is exactly what a real seesaw on its centre does, and why they
        // need a push. A load on one end is what turns it, and the axle answers the
        // difference, which is a force at a place that never moves and therefore does
        // no work.
        bool pivoted = false;
        // Where the axle is. It never moves, and a pinned body's centre is always here.
        Vec3 pivotPoint{};
        // When, within the tick, this body's velocity was turned around by a blow
        // off the ball: the fraction of the tick that had passed when the two met.
        //
        // A blow lands at an instant, not over a whole tick. Advancing a body for a
        // full tick at the speed the blow left it with is a body that travelled the
        // whole tick upwards and not one instant of it downwards -- it is lifted off
        // the surface it bounced on, by exactly one tick of its own rebound, every
        // single bounce. A cube dropped half a metre onto the ball came back to
        // forty-one per cent more height than its own rebound allows, which reads as
        // a trampoline and is not one: no energy is made, the parabola is simply
        // started a tick too early. See advanceBodies().
        // Written while a patch is being gathered, which is a reading of the room
        // and not a change to it: what it records is when the blow it is about to
        // ask for lands. The move that spends it happens later in the same tick.
        mutable double hitAt = 1.0;
        mutable Vec3 hitArrival{};
        // How many contact patches this body has been answered in since the room was
        // reset. It is a counter and not a flag so that a tick can tell, by comparing
        // with what it started from, whether the body met anything at all while it
        // was being stepped -- which is what decides whether its travel this tick was
        // a free fall or a supported one.
        int met = 0;
        // What held it up this tick, worked out from the contacts it ended the tick
        // with: which way the contact patch runs, and whether what it stands on is
        // itself asleep. A body whose weight does not run between its supports is
        // not carried by them -- it tips instead, which is what a shape hanging
        // over an edge does.
        bool touchingPlane = false, onBody = false, supportFrozen = false;
        // Which body is under this one, when one is: `supportFrozen` alone cannot
        // tell a stack that was solved together from a shape propped against
        // something that has already gone to sleep and never felt it (see
        // carriedBySolvedSupport()).
        std::size_t supportBody = kBodyCount;
        double supportLow = 1e30, supportHigh = -1e30;
        // The box its pose has stayed inside, and for how many ticks: a shape that
        // has not been anywhere for a second is standing still, whether the
        // arithmetic of its contacts leaves it a hair of a rock or not.
        Vec3 restLow{1e30, 1e30, 0}, restHigh{-1e30, -1e30, 0};
        double restAngleLow = 1e30, restAngleHigh = -1e30;
        int restTicks = 0;
        // The contact forces this body was actually given in its last simulated
        // tick, as impulses, one per place they acted. A body that falls asleep has
        // no tick to be solved in, and a shape at rest is still standing on
        // something -- so what it is standing on is replayed exactly as it was last
        // solved, not invented afterwards. This is what the arrows show while a body
        // sleeps, and it is why a body on a slope shows its friction and a body on
        // the level floor shows none.
        static constexpr std::size_t kMaxRestForces = 24;
        struct Applied {
            Vec3 at{}, impulse{};
            ForceKind kind = ForceKind::Contact;
        };
        std::array<Applied, kMaxRestForces> applied{};
        std::size_t appliedCount = 0;
        std::size_t faceBatch = 0;
        std::array<std::size_t, kMaxCorners> edgeBatches{}, cornerBatches{};
    };

    // The corner of a body, in the room. The only way a corner's place is ever
    // obtained, and it is obtained from where the body is: a corner is not state.
    static Vec3 cornerAt(const Body& body, std::size_t index) {
        const Vec3 local = body.local[index];
        const double c = std::cos(body.angle), s = std::sin(body.angle);
        return {body.centre.x + local.x * c - local.y * s, body.centre.y + local.x * s + local.y * c,
                body.centre.z};
    }
    // The corners of a body, written down for everything that reads them.
    static void placeCorners(Body& body) {
        for (std::size_t i = 0; i < body.count; ++i) body.position[i] = cornerAt(body, i);
    }
    // The speed of the piece of the body that is at `at`: what the centre is doing,
    // plus what the turn carries that place along.
    static Vec3 pointSpeed(const Body& body, Vec3 at) {
        const Vec3 arm = at - body.centre;
        return {body.velocity.x - body.spin * arm.y, body.velocity.y + body.spin * arm.x, body.velocity.z};
    }
    // How much of a push a body takes as a step of its centre of mass: the reciprocal
    // of its mass, or nothing at all if it is pinned. Every compliance in the room is
    // read from this and not from 1/m, which is why a contact against the seesaw is
    // answered by the axle and not by a plank that cannot go anywhere.
    static double inverseMass(const Body& body) { return body.pivoted ? 0.0 : 1.0 / body.mass; }
    // How much a body gives when it is pushed at `at` along `direction`: the
    // reciprocal of its mass plus the lever's share about its centre. This is the
    // one number a contact is solved with.
    static double giveAt(const Body& body, Vec3 at, Vec3 direction) {
        const Vec3 arm = at - body.centre;
        const double lever = arm.x * direction.y - arm.y * direction.x;
        return inverseMass(body) + lever * lever / body.moment;
    }
    // The same number for a body that is standing on the floor and is being pushed
    // *down* into it. The floor is under the shape and the floor does not give, so
    // the part of the shape's response that would be a step downwards is not a
    // response at all -- the floor answers it. What is left of the shape's compliance
    // is the part of the push that runs *along* the floor, and the turn about its
    // centre, which the floor cannot take. Solving it as though the whole mass were
    // free hands the shape momentum the floor then quietly swallows: a ball dropped
    // on a cube resting on the floor lost a quarter of every bounce to a cube that
    // never moved at all.
    // What of a push on a body actually becomes a step of that body. A shape that is
    // standing on the floor cannot be driven into it: the floor is under it and the
    // floor does not give, so the part of the push that runs downwards is the floor's
    // to answer and is taken at the place it is given. This is the one rule for it --
    // pushBody() applies it and every compliance in the room is measured from it, so
    // that the solver's model of what a shape will do and what the shape then does are
    // the same model. Two models is a contact that never closes: the solver lays on
    // impulse after impulse for a speed the shape never takes, and the shape next to
    // it is handed all of it. That is the shape-against-shape pump.
    static Vec3 bodyFree(const Body& body, Vec3 push) {
        if (body.pivoted) return Vec3{};
        if (!body.touchingPlane || push.y >= 0.0) return push;
        return Vec3{push.x, 0.0, push.z};
    }
    static double giveAtHeld(const Body& body, Vec3 at, Vec3 direction, bool onFloor) {
        const Vec3 arm = at - body.centre;
        const double lever = arm.x * direction.y - arm.y * direction.x;
        const double step = (onFloor && direction.y > 0.0)
                                ? (1.0 - direction.y * direction.y) * inverseMass(body)
                                : inverseMass(body);
        return step + lever * lever / body.moment;
    }

    void tick();
    // One tick of every body at once. The forces are applied first, then the tick
    // is walked: the earliest meeting of anything with anything is solved for, the
    // bodies are carried exactly that far, the meeting is answered with an impulse
    // at the place it happens, and the walk goes on with what is left of the tick.
    void tickBodies();
    void applyBodyForces();
    // The air, as its own force: every moving piece of every body, and how hard it
    // is. See the file: it is applied before the weight and independently of it.
    void applyAirDrag();
    // Carries every body that is being simulated by `seconds` of its own speed and
    // turn. This is the only place a body's place or turn is ever changed by the
    // simulation, and it integrates -- it never corrects.
    double advanceBodies(double seconds);
    // Where a pinned body's corners are at a given angle, and how deep it would be
    // inside anything at that angle. Zero means clear: above the floor, out of every
    // other shape, and off the ball.
    void pivotCornersAt(const Body& body, double angle, std::array<Vec3, kMaxCorners>& corners) const;
    double pivotDepthAt(const Body& body, double angle) const;
    double pivotFloorDepthAt(const Body& body, double angle) const;
    // `withBall` asks whether the ball counts as something the plank may not have
    // turned into. It does when the question is whether the turn is allowed at all;
    // it does not when the question is what the room does about a turn already taken,
    // because the ball's own tick is what answers that -- see separateBodies().
    //
    // `wallsOnly` narrows that to the shapes a plank may not turn into at all -- see
    // shapeGivesWay(): everything the plank can carry out of the way is left out.
    double pivotObjectDepthAt(const Body& body, double angle, bool withBall = true, bool wallsOnly = false) const;
    // Whether a shape lying where the plank is turning can be carried out of the way
    // instead of stopping it. True when the shape is free to be pushed and pushing it
    // clear of the plank leaves it standing in the room.
    //
    // This is the difference between a rider and an obstacle, and it is the difference
    // the room was missing: every shape was a wall, so a plank with anything on it could
    // not turn. The load went down with the face, the face was refused by the shape
    // lying on it, the whole turn was handed back and the spin with it -- and the shape,
    // whose speed had already been solved against a face that was going to turn, was
    // left to fall through it. A shape the face can lift is a load: the patch carries it
    // and the face goes on. A shape pinched between the face and the floor cannot be
    // carried anywhere -- pushing it along the face drives it into the ground -- and
    // that one the plank stops on.
    bool shapeGivesWay(const Body& body, std::size_t other, double amount) const;
    // The first angle a pinned body's turn meets something, as a fraction of the turn
    // it was given. One means nothing was in the way. This is the hitscan the plank
    // needed and did not have: a turn is a piece of travel like any other, and the
    // room's rule is that nothing is ever placed inside anything -- not even for the
    // length of one tick.
    double pivotTurnAllowed(const Body& body, double turn) const;
    // A free body's turn against the floor. How deep it would be at an angle, the
    // rest of a turn taken about the corner it is standing on, and the whole turn
    // walked in steps so that no arc passes through the floor unseen.
    // The last pose along a body's own step that is still above the floor, as the
    // fraction of the step. Used where a step has already been stopped part-way.
    double keepAboveFloor(std::size_t index, Vec3 fromCentre, double fromAngle, Vec3 toCentre, double toAngle,
                          double along);
    // Whether two bodies' outlines are into each other at all, and by how much.
    bool bodiesIntoEachOther(std::size_t one, std::size_t two, Vec3& way, double& depth) const;
    // One patch: a face of a body resting on a surface. Its two ends are the ends
    // of the load that face carries, not two independent meetings -- a body whose
    // weight is answered at one corner alone cannot be held there without being
    // turned by it, and that is a body that rocks forever. The pressure between
    // the ends is what lets a flat face stand still, and what makes it lift off at
    // one end when its weight no longer runs between them.
    //
    // `other` is kBodyCount when the other side is the floor, which cannot move.
    // The impulses are accumulated over the solves of one tick and written into
    // the ledger once the tick has finished.
    static constexpr std::size_t kMaxPatchEnds = 2;
    struct Manifold {
        std::size_t body = 0, other = kBodyCount;
        bool ball = false;  // the other side is the ball rather than a body
        std::size_t ends = 0;
        std::size_t corner[kMaxPatchEnds] = {0, 0};
        Vec3 point[kMaxPatchEnds]{}, normal{};
        // How far each end still has to fall before it is on the surface. A patch
        // is answered against the distance it really has to travel, not against the
        // instant a tick happens to end, so an end that is a hair above the floor is
        // allowed to close exactly that hair over the tick -- and no more.
        double gap[kMaxPatchEnds]{};
        double normalImpulse[kMaxPatchEnds]{}, tangentImpulse[kMaxPatchEnds]{};
        // How fast this end has to be left *leaving* after the answer, in m/s. Zero
        // everywhere the room is inelastic, which is everywhere but the ball: the
        // ball is the one thing in the room with a bounciness, so a contact against
        // it is the one contact that has to give some of the approach back. It is
        // read once, from the speed the pair arrived at, and not again on each sweep,
        // or a contact solved twice would bounce twice.
        double rebound[kMaxPatchEnds]{};
        ForceKind kind = ForceKind::Contact;
    };
    static constexpr std::size_t kMaxTickManifolds = 12;
    struct ManifoldSet {
        std::array<Manifold, kMaxTickManifolds> items{};
        std::size_t count = 0;
        // A patch that is found again is the same patch: each of its ends keeps what
        // it has already carried, so a tick that solves more than once over the same
        // meeting converges on one answer instead of answering it twice.
        void add(const Manifold& patch) {
            for (std::size_t i = 0; i < count; ++i) {
                Manifold& known = items[i];
                if (known.body != patch.body || known.other != patch.other || known.ball != patch.ball)
                    continue;
                // An end is the same end when it is in the same *place*, which is
                // what it has always been: the place the load acts. Matching by
                // corner number looked equivalent and was not -- an end where two
                // edges cross is not any corner at all, so both ends of a shape
                // lying across another one matched the same corner, and each
                // inherited the whole of what that one had carried. The solver then
                // started every iteration believing a load was already there when
                // it was not, and answered it again: a shape rocking on another one
                // with an amplitude that grew instead of dying -- energy out of the
                // arithmetic, which is exactly what a warm start must never do.
                double carried[kMaxPatchEnds]{}, grip[kMaxPatchEnds]{};
                for (std::size_t k = 0; k < patch.ends; ++k) {
                    for (std::size_t j = 0; j < known.ends; ++j) {
                        if (patch.ball && j != k) continue;
                        const Vec3 apart = known.point[j] - patch.point[k];
                        if (std::hypot(apart.x, apart.y) > 1e-6) continue;
                        carried[k] = known.normalImpulse[j];
                        grip[k] = known.tangentImpulse[j];
                    }
                }
                known = patch;
                for (std::size_t k = 0; k < patch.ends; ++k) {
                    known.normalImpulse[k] = carried[k];
                    known.tangentImpulse[k] = grip[k];
                }
                return;
            }
            if (count < items.size()) items[count++] = patch;
        }
        void clear() { count = 0; }
    };
    // Every patch in the room at the pose the tick starts from, and the answer to all
    // of them at once: each patch is solved as one load over its two ends, and the
    // patches are swept until they agree with each other.
    void gatherContacts(ManifoldSet& patches, double band) const;
    bool solveContacts(ManifoldSet& patches);
    // A body's state of rest, decided from its own motion and from what is under
    // it rather than from how little it happened to move.
    void settleBodies();
    // A shape found overlapping another shape, or the floor, is moved out of it by
    // its position alone -- no speed is given it and none is taken away. This is the
    // one thing a velocity solver cannot do: a constraint on speed says "do not come
    // any closer", and a corner that is already inside never came closer, it is
    // there. Left to the velocities it stays there for ever, and a body whose step
    // is refused for being inside something never moves at all while its weight goes
    // on arriving -- which is a shape that gains energy without ever moving.
    void separateBodies();
    // Puts a shape back out of the ball by `through`, along `normal` (which runs
    // from the shape towards the ball), instead of moving the ball. The ball is the
    // one thing whose path may not jump: the shape is what arrived, and a shape that
    // has come into the ball is a shape that has overstepped. Returns false when the
    // shape cannot be the one to give way -- the mouse is holding it, or moving it
    // would put it through the floor -- and then it is the ball's turn.
    bool pushBodyOutOfBall(std::size_t index, Vec3 normal, double through);
    // Nothing ends a tick inside the ball: the sweep cannot see a shape turning into
    // it, so what a turn carried through it is put back out, by place alone. See the
    // file: it is the same rule a shape inside another shape gets.
    void separateBall();
    // How much of a body's step is clear, as the fraction of it that may be taken.
    //
    // A body's step is its middle travelling and its outline turning at once, and the
    // room's other sweeps do not cover it: the ball has its own, and a pinned body's
    // turn has its own, but a shape thrown across the room was simply put where its
    // speed said and answered afterwards -- so a shape at six metres a second went
    // through a plank seventy-seven millimetres deep in one tick, and the answer it
    // got afterwards was the recovery, which is a kick.
    //
    // This is that sweep. It is conservative advancement, not sampling: the step is
    // advanced by the time the clearance itself allows -- the gap divided by the
    // fastest any point of the body can be closing it -- so no speed and no turn can
    // carry the outline through anything between two looks.
    //
    // `mayBeginInContact` is the difference between the simulation and the hand. A
    // body's own step may begin against something: it is in contact then, and contact
    // is the solver's business -- refusing its step would hold it up for ever with the
    // speed it arrived with. The hand is not in contact with anything and it is not
    // moving anything: it puts a body somewhere, so a body resting on the floor could
    // be picked up and carried through a plank, which is exactly what it did. The
    // hand's sweep is therefore asked differently: it stops when anything would be
    // *inside* the body, not when it is merely touching it.
    // `blockedBy` comes back as the outward normal of whatever stopped the step, which
    // is what the caller needs to answer the motion the step was refused -- a refused
    // step is a contact, and a contact has a direction.
    double bodyStepAllowed(std::size_t index, Vec3 fromCentre, double fromAngle, Vec3 toCentre, double toAngle,
                           bool mayBeginInContact, Vec3* blockedBy = nullptr) const;

    // A piece of a body's step that was refused is a piece of motion the body does
    // not have. `kept` is the fraction of the step it was allowed -- one for the
    // whole of it -- and what is left of the speed and the spin is what it keeps.
    // Whatever took the rest took it at a contact, so it is written down as one, at
    // the body, which is what makes the arrows account for it.
    //
    // There are four places in the room that shorten a body's step after it was
    // taken: the swept step itself, the ball's arrival prevention, the ball's walk
    // carrying a shape back to the instant it met it, and a pinned body being turned
    // back out of something. Every one of them must give the motion back, or the body
    // keeps the speed it did not spend -- and then the next tick begins from a speed
    // the body does not have, which is a plank that stops and starts and a plank that
    // gathers a turn it never took.
    void absorbRefusedStep(Body& body, std::size_t index, double kept);

public:
    // Read by the checks and the probes: how much of this room is prevention and how
    // much is repair.
    std::size_t ballFoundThroughCount() const { return ballFoundThrough_; }
    std::size_t bodyStepClampedCount() const { return bodyStepClamped_; }
    std::size_t ballLiftedOutCount() const { return ballLiftedOut_; }
    void clearRepairCounts() { ballFoundThrough_ = bodyStepClamped_ = ballLiftedOut_ = 0; }
private:
    // Whether the body carrying `index` actually carries it: its own replayed forces
    // are where it would be felt, so a support with no load of this body in them was
    // never carrying anything. See the file.
    bool carriedBySolvedSupport(std::size_t index) const;
    // Whether the surface a sleeping body is lying on is still there. A body that
    // was put to rest by a floor, a shape or the ball, and that is touching nothing
    // at all, is a body that has nothing holding it up any more -- and it wakes to
    // fall, because that is what it is doing.
    void wakeUnsupportedBodies();
    // A sleeping body with a load arriving on it is a shape carrying a load, not a
    // wall: it wakes, so that what leans on it is answered against its mass. See the
    // file.
    void wakeLoadedBodies(double band);
    // One body's weight: the same pull at every corner, and the impulse it delivers
    // over the tick. Used by the gravity pass, and for a body woken after it.
    void applyBodyWeight(std::size_t index);
    // One impulse, given to one body at one place. The ledger is written here too:
    // this is the only door into a body's motion, so what is on screen cannot
    // disagree with what was solved.
    void pushBody(Body& body, Vec3 at, Vec3 impulse, ForceKind kind);
    // The ball is a rigid body too, so a blow given to it at a place changes its
    // speed and its turn by the same law -- m and I of a solid sphere.
    Vec3 ballPointSpeed(Vec3 at) const {
        const Vec3 arm = at - ball_.position;
        return {ball_.velocity.x - ball_.spin * arm.y, ball_.velocity.y + ball_.spin * arm.x, ball_.velocity.z};
    }
    double ballGiveAt(Vec3 at, Vec3 direction) const {
        const Vec3 arm = at - ball_.position;
        const double lever = arm.x * direction.y - arm.y * direction.x;
        const double moment = 0.4 * ball_.mass * ball_.radius * ball_.radius;
        return 1.0 / ball_.mass + lever * lever / moment;
    }
    void pushBall(Vec3 at, Vec3 impulse, ForceKind kind);
    void recordImpulse(Vec3 at, Vec3 impulse, ForceKind kind, std::size_t on);
    bool supportedBy(std::size_t body) const;
    bool bodyTopAt(std::size_t body, double x, double& height) const;
    bool bodyHeld(std::size_t body) const { return dragTarget_ == DragTarget::Body && dragBody_ == body; }
    // The point meets a body: the impulse the point takes along the normal is
    // returned so the dot's own speed can be given it back.
    double answerPointOnBody(std::size_t body, Vec3 pointPosition, Vec3 pointVelocity, Vec3 normal);
    void hitBody(std::size_t body, Vec3 normal, double into);
    static Vec3 outwardNormalAt(const Body& body, Vec3 point);
    // The gap between a body's outline and the ball's surface, which way the body
    // has to go to be clear of it, and the place they would touch. Negative when
    // the ball is already into the outline.
    double bodyBallGap(std::size_t index, Vec3& way, Vec3& at) const;
    void tickBall();
    // The surfaces the ball is against, holding it out of themselves.
    void settleBallSurfaces();
    // What the ball was against this tick, so the grip at each of them can be
    // worked out against the others instead of one after another.
    struct BallTouch {
        Vec3 normal{}, surfaceVelocity{};
        // What this face is carrying, in newton seconds. It is not a property of the
        // face on its own: it is what the face has to hold up, which is a question
        // about the whole stack -- see settleBallFriction().
        double pressed = 0.0;
        // Which body is on the other side, or kBodyCount for the floor. A face over
        // the ball is carrying the weight of what stands on it, and that is read from
        // the body: a floor patch beneath a ball carries the ball, but a cube resting
        // on top of one carries the cube.
        std::size_t body = kBodyCount;
    };
    // What is holding the ball up, as the constraint it is.
    //
    // The ball is one rigid body, and the shapes are solved against it before its own
    // tick runs, so for the length of that solve the question "may the ball move that
    // way" has to have a real answer. It used to have a fraction: `ballFreedom`, the
    // share of a push that is along the surface under the ball rather than into it,
    // applied as a factor on the impulse. The fraction is right and using it as a
    // factor is not. The impulse is one vector and the floor blocks one component of
    // it, so scaling the whole vector hands the ball the right answer along the normal
    // it was solved on and throws away the component along the floor -- the only part
    // that would have moved it. Measured on a cube set down on the ball's shoulder:
    // the shape took +0.73 N s of sideways momentum in one tick and the ball answered
    // with -0.04, and the missing 0.69 was made again every tick until the cube was
    // squeezed off at a metre a second with the meter reading above the drop.
    //
    // So the surfaces under the ball are gathered as normals, and both halves of the
    // answer are read off one projection of them:
    //
    //   * the translation the ball is given is the push with those normals taken out
    //     (ballFree) -- it slides along the floor and cannot be driven into it;
    //   * the turn it is given is the turn of the *whole* push (pushBallSupported),
    //     because a support's answer passes through the middle and adds no moment.
    //
    // Projecting before taking the moment is what spun a resting ball to two hundred
    // and eighty radians a second: a normal push through the middle has no lever, but
    // its projection along the floor does, and that lever is a force the floor never
    // gave.
    struct BallSupport {
        Vec3 normal{};                 // out of the surface, into the ball
        std::size_t body = kBodyCount; // kBodyCount for the floor, kDotOwner for the dot
    };
    static constexpr std::size_t kMaxBallSupports = 4;
    std::array<BallSupport, kMaxBallSupports> ballSupports_{};
    std::size_t ballSupportCount_ = 0;
    void gatherBallSupports();
    // `except` is a body that must not be counted among the ball's supports -- the
    // one this push is being solved *against*. Its own contact is what the solver is
    // working out, so counting it as a surface the ball is held by makes the answer
    // circular: the ball is declared unable to move along the way the shape is
    // pushing it, the whole impulse is handed to the shape, and the shape is given a
    // blow the ball never felt. A ball that rolled into the end of the seesaw's plank
    // took nothing from it and the plank was spun at fifteen radians a second by four
    // hundred and seventy newton seconds that came from nowhere. Every other surface
    // is still there: a wall on the other side of the ball is a wall.
    static constexpr std::size_t kNoBody = kBodyCount + 2;
    Vec3 ballFree(Vec3 push, std::size_t except = kNoBody) const {
        // The projection is onto the *orthogonal complement of all of them at once*,
        // which is not the same as taking them one after another. With one support
        // the two agree; with two they do not, and the difference is momentum.
        // Taking the floor's normal out of a push and then the cube's puts back a
        // little of the floor's -- the normals are not perpendicular, so removing one
        // does not preserve the other -- and a ball held by a floor and a shape was
        // handed 1.7 N s *upward* by an impulse that was pressing it down.
        //
        // It is solved and not swept towards. Sweeping the normals one after another
        // is a projection only in the limit, and it converges at the cosine of the
        // angle between them to the power of the number of sweeps: eight sweeps over a
        // floor and a plank's end a third of a turn apart leave seven parts in a
        // hundred of the push unaccounted for. Seven parts in a hundred is nothing
        // until the solve starts leaning on it -- a ball wedged between two surfaces
        // that give nothing has nowhere to go at all, so the impulse needed to stop
        // the closing is unbounded, and every pass that leaked a little of it put a
        // little more speed into the ball, sideways, where the leak pointed. That is
        // how a ball standing still under a seesaw's end left at eight metres a
        // second. The complement is the answer of a small linear system -- the
        // support normals' own Gram matrix -- and it is exact.
        // The supports that count: every one but the body being solved against.
        std::array<Vec3, kMaxBallSupports> normals{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < ballSupportCount_; ++i) {
            if (ballSupports_[i].body == except) continue;
            normals[count++] = ballSupports_[i].normal;
        }
        if (count == 0) return push;
        double m[kMaxBallSupports][kMaxBallSupports + 1]{};
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = 0; j < count; ++j) m[i][j] = dot(normals[i], normals[j]);
            m[i][count] = dot(normals[i], push);
        }
        // Gaussian elimination with partial pivoting. A row with no pivot left is a
        // support that another support already says -- two faces on the same line, or
        // the same face twice -- and it carries no share of its own.
        std::array<double, kMaxBallSupports> share{};
        for (std::size_t col = 0; col < count; ++col) {
            std::size_t pivot = col;
            for (std::size_t row = col + 1; row < count; ++row)
                if (std::abs(m[row][col]) > std::abs(m[pivot][col])) pivot = row;
            if (!(std::abs(m[pivot][col]) > 1e-12)) continue;
            if (pivot != col)
                for (std::size_t j = col; j <= count; ++j) std::swap(m[col][j], m[pivot][j]);
            for (std::size_t row = 0; row < count; ++row) {
                if (row == col) continue;
                const double factor = m[row][col] / m[col][col];
                if (factor == 0.0) continue;
                for (std::size_t j = col; j <= count; ++j) m[row][j] -= factor * m[col][j];
            }
        }
        for (std::size_t i = 0; i < count; ++i)
            if (std::abs(m[i][i]) > 1e-12) share[i] = m[i][count] / m[i][i];
        Vec3 out = push;
        for (std::size_t i = 0; i < count; ++i) out = out - normals[i] * share[i];
        return out;
    }
    // The same projection, as the share of the compliance a patch may count on: how
    // much of a push along `way` the ball is free to answer.
    double ballTake(Vec3 way) const {
        const double size = length(way);
        if (!(size > 0.0)) return 1.0;
        const Vec3 unit = way * (1.0 / size);
        return std::max(0.0, dot(unit, ballFree(unit)));
    }
    void pushBallSupported(Vec3 at, Vec3 impulse, ForceKind kind, std::size_t except = kNoBody);
    static constexpr std::size_t kMaxBallTouches = 8;
    std::array<BallTouch, kMaxBallTouches> ballTouches_{};
    std::size_t ballTouchCount_ = 0;
    // Where every shape was when the ball's half of the tick began: its speed and its
    // turn, as the tick's own solve of the shapes left them. A face the ball's grip is
    // measured against is the face the ball met, and that is where it was standing when
    // the ball arrived at it -- not where the ball's own blow had pushed it a moment
    // later. Read live, the grip sees its own push coming back at it: the ball presses
    // a face, the face moves, the grip reads the movement as the face running away and
    // drags the ball after it, and a ball wedged in a corner keeps a speed that carries
    // it nowhere for as long as it is left there.
    std::array<Vec3, kBodyCount> ballTickSpeed_{};
    std::array<double, kBodyCount> ballTickSpin_{};
    void settleBallFriction();
    // How much weight stands on a body, in newtons: its own, plus whatever the
    // solver found resting on it this tick. A face of the ball that has a body over
    // it is carrying this, and friction is mu times it.
    double loadOver(std::size_t body) const;
    // Whether the ball can be put down here: the mouse may carry it anywhere in
    // the room, but not inside a solid. A ball placed inside one would have to be
    // driven back out, and nothing in this room is driven out of anything -- it
    // never gets in.
    bool ballPlacementClear(Vec3 at) const;
    // Whether a body can be put down where its corners would be: the mouse may carry
    // it anywhere in the room, but not into another shape, another body's corner, or
    // the ball. Nothing in this room is ever driven out of anything -- it never gets
    // in -- and a body the mouse let go of inside another one is the state that no
    // amount of solving can answer without inventing speed.
    bool bodyPlacementClear(std::size_t index, const std::array<Vec3, kMaxCorners>& wanted) const;
    // Whether anything else in the room is moving enough to reach the ball. A
    // ball asleep on the floor still has to be hit by whatever is falling on it.
    bool ballNeighboursMoving() const;
    // Answers a ball's contact. A ball against the room shares nothing; a ball
    // against a body shares the answer with the corner it struck, so the ball
    // gives up only what the body takes.
    // `resting` says the ball was already lying on this surface when the tick began.
    // That is a contact being kept, not something arrived at, and the difference is
    // the whole of what a bounce is: a surface that is merely holding the ball up
    // has nothing to give back, however fast its own motion makes the two of them
    // look as if they are closing.
    // `reached` is where in the tick the two met, as a fraction of it. A bounce is
    // the answer to the speed the ball had *there*, and a tick of gravity is half a
    // metre a second in this room: measured at the start of the tick the blow is too
    // small and measured at the end of it too large, so the moment itself is passed.
    void ballContact(Vec3 normal, int bodyIndex, bool ballHeld = false, Vec3 contactPoint = {},
                     double depth = 0.0, bool exchange = true, bool resting = false, double reached = 1.0);
    // A face of the ball, said once: what it is against, and which way out of the
    // ball. The ball's grip is solved for all of its faces at once in
    // settleBallFriction(), so every road by which the ball comes to be against
    // something has to report the face it is against -- and a shape resting on the
    // ball is answered as a *patch*, not through ballContact(), which is why a weight
    // sitting on the crown had no grip at all. See noteBallTouch().
    void noteBallTouch(std::size_t body, Vec3 normal, Vec3 at);
    // The ball meets the point, and the point meets the ball. Both exchange the
    // same momentum along the line between them.
    void hitBallWithPoint(Vec3 normal);
    void pointAgainstBall(Vec3 normal);
    // How much say the ball has in a blow that pushes it along `push`, as an inverse
    // mass. It is not simply 1/m: a ball resting on something cannot be driven into
    // it, because the surface underneath answers that part of the blow -- the floor
    // does not give. What is left of the blow is the part along that surface, and
    // that part does move the ball.
    //
    // It used to be all or nothing: a ball resting on anything took *no* share of
    // any blow, in any direction. A shape landing flat on the top of a ball is
    // caught by the floor that way, and that is right; a shape or a dot striking
    // the side of that same ball was given nothing as well, which is why the ball
    // sat there like a painted circle while things bounced off it. This is the
    // projection: zero along the way it cannot go, everything along the way it can.
    // The fraction of a unit push the ball can actually take: everything, except
    // along the way the surface under it will not let it go. One is a ball in the
    // air or a push along the floor; zero is a push straight down onto it.
    double ballFreedom(Vec3 push) const {
        // Only a surface that is actually under it holds it. A ball in mid-air has
        // no support at all, and its last one is not a fact about this tick.
        if (!ball_.touching || !ball_.standing) return 1.0;
        const double into = std::max(0.0, -dot(push, ball_.supportNormal));
        return 1.0 - into * into;
    }
    double ballCompliance(Vec3 push, bool held) const {
        if (held) return 0.0;
        return ballFreedom(push) / ball_.mass;
    }
    // Whether a body has arrived at the resting point this tick. A point that is
    // asleep is not stepped, and a step that is not taken runs no hitscan -- so a
    // shape falling on a dot that is resting on the floor would pass straight
    // through it. This is the hitscan that wakes it: the step every body took this
    // tick, swept against the dot's own radius.
    bool pointReachedByBodies() const;
    // Whether the surface the dot went to sleep on has gone away underneath it.
    bool pointSupportGone() const;
    void freezeCheck();
    void updateDrag();
    // One force, written down as it is applied. Called from the tick itself and
    // from the pair and ball contacts, so the ledger is filled by the physics and
    // nothing else writes to it.
    // Which solid feels it has no default: an arrow that names no owner lands on
    // body 0, and a ball's weight silently drawn on a cube is exactly the sort of
    // lie a required argument stops being possible.
    void addForce(Vec3 at, Vec3 force, ForceKind kind, Vec3* accumulator, std::size_t owner) {
        if (forceCount_ < kMaxForceSamples) forces_[forceCount_++] = ForceSample{at, force, kind, owner};
        if (accumulator != nullptr) *accumulator = *accumulator + force;
    }
    // A couple, written the same way and in the same ledger, with the force it is
    // drawn as. See ForceSample: the moment is the physics, the force is the pen.
    void addCouple(Vec3 at, double moment, Vec3 drawn, ForceKind kind, std::size_t owner) {
        if (forceCount_ < kMaxForceSamples) forces_[forceCount_++] = ForceSample{at, drawn, kind, owner, moment, true};
    }
    void clearForces() { forceCount_ = 0; }
    // What is still acting on the things that are no longer stepped. A body at
    // rest is not ticked, and a body that is not ticked writes nothing -- but it
    // is still being pulled down by its own weight and still held up by whatever
    // it stands on. This is that pair, written from what holds it still: the
    // weight at every corner, and the same weight back at the corners carrying it.
    // `stepped` says, per body, whether this tick stepped it -- and the two flags
    // do the same for the dot and the ball. Anything that was stepped has already
    // written its own forces and is skipped here.
    void recordStaticForces(const std::array<bool, kBodyCount>& stepped, bool pointStepped, bool ballStepped);
    // The arrows, and the numbers written beside them.
    void placeForceArrows();
    void appendForceLabels(const Camera& camera, NativeFrame& frame);
    void buildScene(const Camera& camera);
    void placeMarkers();
    void interpolate();
    void interpolateBody(Body& body, double alpha);

    Vec3 point_{0, 2, 0}, previous_{0, 2, 0}, shown_{0, 2, 0};
    // The point's speed once the tick's forces had been applied to it, before any
    // contact answered it: the difference at the end of the tick is what the
    // contacts delivered.
    Vec3 velocity_{};
    // And the same accumulator for the dot: its weight goes in, what the surface
    // answers goes in, and the integrator adds the total over its mass.
    Vec3 pointForce_{};
    Vec3 traceFrom_{0, 2, 0}, traceTo_{0, 2, 0};
    std::array<Vec3, 40> ghosts_{};
    std::size_t ghostCount_ = 0, ghostNext_ = 0;
    std::array<Body, kBodyCount> bodies_{};
    Ball ball_{};
    // Every blow the ball has been given this tick, summed, in newton seconds.
    // The ball's account is closed against this: what actually happened to its
    // momentum, minus the blows written where they landed, is what its support
    // supplied -- and that is the one force in the room with no solid to hang it on,
    // so it is the one that has to be worked out rather than assumed.
    Vec3 ballBlows_{};
    std::array<float, 4> dragOrigin_{}, dragDirection_{};
    GpuScene scene_;
    std::size_t pointBatch_ = 0, traceBatch_ = 0;
    // One batch per arrow, built once and hidden when there is nothing to draw. The
    // shaft is a unit bar along +x and the head a unit disc, so the tail goes where
    // the force acts and the tip where the force reaches -- the same matrix call the
    // bodies' own edges are placed with.
    std::array<std::array<std::size_t, kArrowsPerKind>, std::size_t(ForceKind::KindCount)> arrowShaft_{};
    std::array<std::array<std::size_t, kArrowsPerKind>, std::size_t(ForceKind::KindCount)> arrowHead_{};
    std::array<ForceSample, kMaxForceSamples> forces_{};
    std::size_t forceCount_ = 0;
    // Every impulse handed to a body this tick, added up at the place it acted, so
    // that a contact solved in eight passes is one force and not eight arrows.
    // The force a body feels, not the pair of them. A contact between two shapes is
    // one impulse of +J on one and one of -J on the other, at the same place: added
    // together at the place alone they are nothing at all, which is how a shape
    // resting on another shape came to be drawn with no contact force on it while it
    // was very obviously being held up. Each is kept against the body it acts on.
    struct RecordedImpulse {
        Vec3 at{}, impulse{};
        ForceKind kind = ForceKind::Contact;
        std::size_t on = 0;
    };
    // A whole tick's worth, merged by (body, kind, place). The merge is what keeps
    // this small -- a corner, one kind of force, one arrow -- and the array is sized
    // to what the room can actually hold: every body's corners against every kind of
    // force, with room to spare. It is a bound, never a cut: a tick that silently
    // dropped impulses would draw a body with arrows that do not add up to what it
    // actually felt.
    // One entry per contact per kind. Two contacts are never added together, so
    // a point where the floor and a face both act is two entries, not one: the
    // bound is twice the old one, which is what that costs.
    static constexpr std::size_t kMaxRecordedImpulses =
        8 * kBodyCount * std::size_t(ForceKind::KindCount) + 16;
    std::array<RecordedImpulse, kMaxRecordedImpulses> impulses_{};
    std::size_t impulseCount_ = 0;
    void clearImpulses() { impulseCount_ = 0; }
    void writeImpulses();
    bool showForces_ = false, showNewtons_ = false;
    // The numbers are quads like everything else: they are laid out here and handed
    // to the same batcher the rest of the interface goes through.
    UiLayer forceLabels_;
    std::array<std::size_t, 40> ghostBatches_{};
    double accumulator_ = 0;
    DragTarget dragTarget_ = DragTarget::None;
    std::size_t dragBody_ = kBodyCount;
    Vec3 planeGrab_{}, grabOffset_{};
    // How much of the tick the point spent sliding on something. Friction acts
    // over that time and no more, so a contact made late in a tick does not
    // brake the point for the whole tick.
    double slideSpan_ = 0.0;
    // The outward normal of whatever is holding the point up. The surface
    // answers only the force into itself; the rest still acts on the point,
    // which is why a point on a slope slides down it.
    Vec3 supportNormal_{0, 1, 0};
    // True when nothing is left to move the point: it is held, not merely slow.
    bool heldStill_ = false;
    bool sceneReady_ = false, resting_ = false, haveTrace_ = false;
    bool pointFrozen_ = false;
    // Which body is holding the point up, or -1 when none is.
    int restingBody_ = -1;
    bool restingOnBall_ = false;
    // How often the room has had to *repair* an overlap instead of preventing it:
    // a ball found already through a shape at the start of its own tick, a body
    // carried back to where it met the ball, and a ball lifted out sideways from
    // under something. Prevention is the rule and these are the last resort, so
    // they are counted -- a number that only ever goes up in a room where something
    // is still being missed is how the thing being missed gets found.
    std::size_t ballFoundThrough_ = 0;
    std::size_t bodyStepClamped_ = 0;
    std::size_t ballLiftedOut_ = 0;
};
} // namespace gtc::native
