#pragma once

#include "client/raster.h"
#include "client/ui_layer.h"

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
// A tick is walked, not sampled: the next instant at which anything would touch
// anything is solved for, the bodies are carried exactly that far, the contact is
// answered with an impulse, and the walk continues with what is left of the tick
// -- so nothing can cross anything however fast it is going, and nothing is ever
// pushed apart after the fact. There is no distance solve, no position
// correction, and no velocity read back out of a position.
class PlaygroundRoom {
public:
    // The bodies in the room, in the order they are simulated.
    static constexpr std::size_t kCube = 0, kTriangle = 1, kBodyCount = 2;
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
    bool bodyFrozen(std::size_t body) const { return body < kBodyCount ? bodies_[body].frozen : true; }
    // Forces, as they are applied, and the room's energy.
    //
    // Every tick the room writes down every force it applies: where it acts, which
    // way, and how hard in newtons. Nothing in the simulation is allowed to move a
    // corner except a force written here, so this is the room's own account of what
    // it is doing -- not a separate diagnostic that could drift from the physics.
    // The arrows drawn on F are these numbers, and the figures on N are the same
    // numbers again, so what is on screen cannot disagree with what was solved.
    enum class ForceKind { Gravity = 0, Contact = 1, Pair = 2, Ball = 3, Drag = 4, KindCount = 5 };
    // Room for a whole tick's worth of every kind at once: two bodies of four
    // corners, the dot, the ball, and the contacts and pairs between them. The
    // number is a bound on the array, not a cut: nothing is ever dropped quietly,
    // because a ledger that loses entries is worse than no ledger at all.
    static constexpr std::size_t kMaxForceSamples = 128;
    static constexpr std::size_t kArrowsPerKind = 20;
    struct ForceSample {
        Vec3 at{};     // where it acts, in metres
        Vec3 force{};  // newtons
        ForceKind kind = ForceKind::Gravity;
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
    static constexpr std::size_t kMaxCorners = 4;
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
        int contacts = 0;
        // What held it up this tick, worked out from the contacts it ended the tick
        // with: which way the contact patch runs, and whether what it stands on is
        // itself asleep. A body whose weight does not run between its supports is
        // not carried by them -- it tips instead, which is what a shape hanging
        // over an edge does.
        bool touchingPlane = false, onBody = false, supportFrozen = false;
        double supportLow = 1e30, supportHigh = -1e30;
        // The box its pose has stayed inside, and for how many ticks: a shape that
        // has not been anywhere for a second is standing still, whether the
        // arithmetic of its contacts leaves it a hair of a rock or not.
        Vec3 restLow{1e30, 1e30, 0}, restHigh{-1e30, -1e30, 0};
        double restAngleLow = 1e30, restAngleHigh = -1e30;
        int restTicks = 0;
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
    // How much a body gives when it is pushed at `at` along `direction`: the
    // reciprocal of its mass plus the lever's share about its centre. This is the
    // one number a contact is solved with.
    static double giveAt(const Body& body, Vec3 at, Vec3 direction) {
        const Vec3 arm = at - body.centre;
        const double lever = arm.x * direction.y - arm.y * direction.x;
        return 1.0 / body.mass + lever * lever / body.moment;
    }

    void tick();
    // One tick of every body at once. The forces are applied first, then the tick
    // is walked: the earliest meeting of anything with anything is solved for, the
    // bodies are carried exactly that far, the meeting is answered with an impulse
    // at the place it happens, and the walk goes on with what is left of the tick.
    void tickBodies();
    void applyBodyForces();
    // Carries every body that is being simulated by `seconds` of its own speed and
    // turn. This is the only place a body's place or turn is ever changed by the
    // simulation, and it integrates -- it never corrects.
    double advanceBodies(double seconds);
    // The first fraction of `remaining` at which anything would touch anything, or
    // `remaining` itself when nothing will. The walk stops there and answers it.
    double timeToContact(double remaining) const;
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
        double normalImpulse[kMaxPatchEnds]{}, tangentImpulse[kMaxPatchEnds]{};
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
                double carried[kMaxPatchEnds]{}, grip[kMaxPatchEnds]{};
                for (std::size_t k = 0; k < patch.ends; ++k) {
                    for (std::size_t j = 0; j < known.ends; ++j) {
                        if (!patch.ball && known.corner[j] != patch.corner[k]) continue;
                        if (patch.ball && j != k) continue;
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
    // Every patch at the configuration the walk has stopped at, and the answer to
    // all of them at once: each patch is solved as one load over its two ends, and
    // the patches are swept until they agree with each other.
    void gatherContacts(ManifoldSet& patches) const;
    bool solveContacts(ManifoldSet& patches);
    // A body's state of rest, decided from its own motion and from what is under
    // it rather than from how little it happened to move.
    void settleBodies();
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
    void recordImpulse(Vec3 at, Vec3 impulse, ForceKind kind);
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
    // What the ball was against this tick, so the grip at each of them can be
    // worked out against the others instead of one after another.
    struct BallTouch {
        Vec3 normal{}, surfaceVelocity{};
        double pressed = 0.0;
    };
    static constexpr std::size_t kMaxBallTouches = 8;
    std::array<BallTouch, kMaxBallTouches> ballTouches_{};
    std::size_t ballTouchCount_ = 0;
    void settleBallFriction();
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
    void ballContact(Vec3 normal, int bodyIndex, bool ballHeld = false, Vec3 contactPoint = {},
                     double depth = 0.0, bool exchange = true);
    // The ball meets the point, and the point meets the ball. Both exchange the
    // same momentum along the line between them.
    void hitBallWithPoint(Vec3 normal);
    void pointAgainstBall(Vec3 normal);
    // Whether a body has arrived at the resting point this tick. A point that is
    // asleep is not stepped, and a step that is not taken runs no hitscan -- so a
    // shape falling on a dot that is resting on the floor would pass straight
    // through it. This is the hitscan that wakes it: the step every body took this
    // tick, swept against the dot's own radius.
    bool pointReachedByBodies() const;
    void freezeCheck();
    void updateDrag();
    // One force, written down as it is applied. Called from the tick itself and
    // from the pair and ball contacts, so the ledger is filled by the physics and
    // nothing else writes to it.
    void addForce(Vec3 at, Vec3 force, ForceKind kind, Vec3* accumulator = nullptr) {
        if (forceCount_ < kMaxForceSamples) forces_[forceCount_++] = ForceSample{at, force, kind};
        if (accumulator != nullptr) *accumulator = *accumulator + force;
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
    struct RecordedImpulse {
        Vec3 at{}, impulse{};
        ForceKind kind = ForceKind::Contact;
    };
    static constexpr std::size_t kMaxRecordedImpulses = 24;
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
};
} // namespace gtc::native
