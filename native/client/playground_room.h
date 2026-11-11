#pragma once

#include "client/raster.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gtc::native {
class RoomFreecam;
struct ViewState;

// The physics rebuild, one step at a time. A point with a mass, gravity and one
// plane; then solid bodies made of such points, one at each corner, held at a
// fixed distance from each other. The square is four corners and the six
// distances between them; the triangle is three corners and the three distances
// between them. Both go through the same code, because a body is its corners
// and its fixed distances and nothing else. There is exactly one update per
// 20 Hz tick. The travel of that tick is tested with a hitscan segment before
// anything is allowed to move, and the plane stops what reaches it. A body with
// no velocity and no net force is frozen.
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
        bool frozen = false, touching = false;
        // Whether static friction could hold the ball at every surface it touched
        // this tick. A slope cannot, so a ball on one is never at rest.
        bool standing = true;
        std::size_t faceBatch = 0, markBatch = 0;
    };
    // A body is its corner points and the distances between them that are not
    // allowed to change. Nothing in the room asks which body it is looking at.
    static constexpr std::size_t kMaxCorners = 4, kMaxPairs = 6;
    struct Body {
        std::size_t count = 0, pairCount = 0;
        std::array<Vec3, kMaxCorners> position{}, previous{}, shown{}, velocity{};
        std::array<std::array<int, 2>, kMaxPairs> pairs{};
        std::array<double, kMaxPairs> rest{};
        // The rest shape about the body's own middle, which is what the drawn
        // body is rebuilt from between ticks.
        std::array<Vec3, kMaxCorners> local{};
        // This body's own friction, from its shape.
        double staticFriction = 0, kineticFriction = 0;
        bool frozen = false;
        int contacts = 0;
        // Whether another body is holding this one up -- worked out every tick from
        // the contacts it ended that tick with, exactly as the ball remembers
        // whether it could stand on what it touched -- and where along its bottom
        // those contacts are. A body whose weight does not pass between them is not
        // carried by them: it turns about the last of them instead, which is what a
        // shape hanging over an edge does.
        bool onBody = false;
        double supportLow = 1e30, supportHigh = -1e30;
        // And whether what it is standing on is itself at rest. A body asleep on a
        // body that is still moving has nothing solid under it.
        bool supportFrozen = false;
        // Which other body this one's step was stopped by this tick, or kBodyCount
        // for none. The sweep stops a body exactly where it touches the other one,
        // and the solve that follows -- the fixed distances and the plane -- leaves
        // it a fraction of a hair outside; a contact that is only recognised by
        // measuring the gap afterwards is therefore a contact that comes and goes,
        // and a pair whose contact comes and goes never has its closing speed
        // answered and hangs in the air gathering speed. The ball remembers what it
        // is touching for exactly this reason.
        std::size_t metBody = kBodyCount;
        // Whether a contact answered one of this body's points this tick. A body
        // that was not touched is left exactly as it is.
        bool touched = false;
        std::size_t faceBatch = 0;
        std::array<std::size_t, kMaxCorners> edgeBatches{}, cornerBatches{};
    };
    void tick();
    void tickBody(std::size_t index);
    bool supportedBy(std::size_t body) const;
    bool bodyTopAt(std::size_t body, double x, double& height) const;
    bool bodyHeld(std::size_t body) const { return dragTarget_ == DragTarget::Body && dragBody_ == body; }
    // The point meets a body: energy into the corner it struck, a push back
    // along the normal.
    void hitBody(std::size_t body, Vec3 normal, double into);
    // Which of a body's corners a blow lands on, and what a body does with a blow
    // it is given there: the struck corner takes half and the rest share it. This
    // is how a point's blow reaches a body -- the dot's collision, and the reaction
    // to the friction of a body the dot is sliding on.
    std::size_t nearestCorner(const Body& body, Vec3 point) const;
    void giveBodyImpulse(std::size_t body, std::size_t struck, Vec3 momentum);
    // What a body is when a blow lands on it: where its corners balance, and the
    // moment of inertia of them about that middle. `shareAt` is how much of the
    // body's mass lies the way the impulse points from there, which is the
    // reciprocal the impulse is solved against, and `giveTurn` is what the body is
    // left doing when it takes that impulse at that point. The ball's strike on a
    // body and a body's strike on a body are both answered by these three.
    struct BodyInertia {
        Vec3 middle{};
        double moment = 1.0;
    };
    BodyInertia bodyInertia(const Body& body) const;
    double bodyShareAt(const Body& body, const BodyInertia& inertia, Vec3 contact, Vec3 direction) const;
    void giveBodyTurn(std::size_t body, const BodyInertia& inertia, Vec3 contact, Vec3 momentum);
    // A point of mass kMass meets a body: the impulse the point takes along the
    // normal is returned so the dot's own velocity can be given it back. Nothing
    // bounces, and a body on the plane only takes the part of the blow that runs
    // along it.
    double answerPointOnBody(std::size_t body, Vec3 pointPosition, Vec3 pointVelocity, Vec3 normal);
    static Vec3 outwardNormalAt(const Body& body, Vec3 point);
    // A body and the ball, from the body's side. How far the body's step can go
    // before the whole of its outline reaches the ball, and how far the ball has
    // the outline inside it and which way out. Both are the ball's geometry
    // against a body's outline, so both live with the ball.
    double bodyBallReach(std::size_t index, const Vec3* target) const;
    // A body and another body. How far this body's step can be walked before one of
    // its corners crosses another body's outline; the corners each puts back out of
    // the other; and what the two of them are still closing at, answered for both
    // sides at once once every body has been stepped. The same geometry and the
    // same answer for every pair -- nothing in there knows which shapes it is
    // looking at, and a corner is a point with no size of its own.
    double bodiesReach(std::size_t index, const Vec3* target, Vec3* wayOut, std::size_t* metWith) const;
    double pushBodyOutOfBody(std::size_t index, std::array<Vec3, kMaxCorners>* lift);
    // Moves a body as one rigid piece so that the piece of it at `at` travels by
    // `displacement`: that is what a correction at a contact is. It is here because
    // the floor, the ball and another body all have to correct a body the same way,
    // and correcting a body any other way -- pinning the one point that is out and
    // letting the fixed distances sort out the rest -- does not keep its shape.
    void pushBodyAtPoint(std::size_t index, Vec3 at, Vec3 displacement, std::array<Vec3, kMaxCorners>* lift);
    void bodiesAnswer();
    double pushBodyOutOfBall(std::size_t index, std::array<Vec3, kMaxCorners>* lift);
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
    void freezeCheck();
    void updateDrag();
    void buildScene(const Camera& camera);
    void placeMarkers();
    void interpolate();
    void interpolateBody(Body& body, double alpha);

    Vec3 point_{0, 2, 0}, previous_{0, 2, 0}, shown_{0, 2, 0};
    Vec3 velocity_{};
    Vec3 traceFrom_{0, 2, 0}, traceTo_{0, 2, 0};
    std::array<Vec3, 40> ghosts_{};
    std::size_t ghostCount_ = 0, ghostNext_ = 0;
    std::array<Body, kBodyCount> bodies_{};
    Ball ball_{};
    std::array<float, 4> dragOrigin_{}, dragDirection_{};
    GpuScene scene_;
    std::size_t pointBatch_ = 0, traceBatch_ = 0;
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
