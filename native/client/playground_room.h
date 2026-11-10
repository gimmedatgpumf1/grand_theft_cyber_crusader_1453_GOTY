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

private:
    // What the mouse is holding, if anything.
    enum class DragTarget { None, Point, Body };
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
        bool frozen = false;
        int quietTicks = 0, contacts = 0;
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
    void hitBody(std::size_t body, Vec3 from, Vec3 to, double hit);
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
    std::array<float, 4> dragOrigin_{}, dragDirection_{};
    GpuScene scene_;
    std::size_t pointBatch_ = 0, traceBatch_ = 0;
    std::array<std::size_t, 40> ghostBatches_{};
    double accumulator_ = 0;
    DragTarget dragTarget_ = DragTarget::None;
    std::size_t dragBody_ = kBodyCount;
    Vec3 planeGrab_{}, grabOffset_{};
    bool sceneReady_ = false, resting_ = false, haveTrace_ = false;
    bool pointFrozen_ = false;
    // Which body is holding the point up, or -1 when none is.
    int restingBody_ = -1;
};
} // namespace gtc::native
