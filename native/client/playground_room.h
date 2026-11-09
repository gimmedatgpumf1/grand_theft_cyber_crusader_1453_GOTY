#pragma once

#include "client/raster.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gtc::native {
class RoomFreecam;
struct ViewState;

// Steps one and two of the physics rebuild: a single point, and a square. There
// is exactly one update per 20 Hz tick. The travel of that tick is tested with a
// hitscan segment before anything is allowed to move, the plane stops what
// reaches it by pushing back with the weight it carries, and a body with no
// velocity and no net force is frozen until something wakes it.
//
// The square is not four free points held by distance constraints. It is one
// body with a centre of mass, an angle and a moment of inertia, and its corners
// follow from those three numbers, so the distance between corners is exact by
// construction. Contact is resolved at a corner where the plane meets it, which
// is what makes an edge-balanced square tip over instead of standing on it.
class PlaygroundRoom {
public:
    // Puts the point back at the top of the room, with the velocity it starts
    // with. Zero velocity is the plain drop; anything else is a launch.
    bool reset(Vec3 start = {0, 1.9, 0}, Vec3 startVelocity = {});
    // Hands the frame's elapsed time to the fixed 20 Hz tick accumulator. No
    // tick is ever split and no tick is ever run twice.
    void advance(double seconds);
    void setDragRay(Vec3 origin, Vec3 direction);
    void beginDrag();
    void endDrag() { dragging_ = false; }
    bool render(NativeFrame& frame, const ViewState& view, const RoomFreecam& camera);
    // What is on screen this frame: positions interpolated between the two ticks
    // that bracket the frame, at the fraction of the tick it falls on.
    Vec3 displayed() const { return shown_; }
    // A corner of the square as drawn this frame.
    Vec3 cubeCorner(std::size_t index) const { return index < 4 ? cubeShown_[index] : Vec3{}; }
    // The simulated rather than drawn state, for the visual harness.
    Vec3 point() const { return point_; }
    Vec3 velocity() const { return velocity_; }
    bool resting() const { return resting_; }
    bool pointFrozen() const { return pointFrozen_; }
    bool cubeFrozen() const { return cubeFrozen_; }
    // The four corners of the square, in world space, from the tick that last
    // ran. Used to check that the square really is still a square.
    std::array<Vec3, 4> cubeCorners() const;
    double angle() const { return boxAngle_; }
    double angularVelocity() const { return boxSpin_; }

private:
    void tick();
    void tickBox();
    void solveContacts(std::array<bool, 4> touching);
    std::array<Vec3, 4> cornersAt(Vec3 center, double angle) const;
    std::array<Vec3, 4> cornerVelocity() const;
    void freezeCheck();
    void updateDrag();
    void buildScene(const Camera& camera);
    void placeMarkers();
    void interpolate();

    Vec3 point_{0, 1.9, 0}, previous_{0, 1.9, 0}, shown_{0, 1.9, 0};
    Vec3 velocity_{};
    Vec3 traceFrom_{0, 1.9, 0}, traceTo_{0, 1.9, 0};
    std::array<Vec3, 40> ghosts_{};
    std::size_t ghostCount_ = 0, ghostNext_ = 0;
    // The square as one body: where its centre of mass is, which way it is
    // turned, how fast it is going and how fast it is turning.
    Vec3 boxCenter_{}, boxPreviousCenter_{}, boxShownCenter_{};
    Vec3 boxVelocity_{};
    double boxAngle_ = 0, boxPreviousAngle_ = 0, boxShownAngle_ = 0, boxSpin_ = 0;
    std::array<Vec3, 4> cubeShown_{};
    std::array<float, 4> dragOrigin_{}, dragDirection_{};
    GpuScene scene_;
    std::size_t pointBatch_ = 0, traceBatch_ = 0, cubeFaceBatch_ = 0;
    std::array<std::size_t, 40> ghostBatches_{};
    std::array<std::size_t, 4> cubeEdgeBatches_{}, cubeCornerBatches_{};
    double accumulator_ = 0;
    bool sceneReady_ = false, dragging_ = false, resting_ = false, haveTrace_ = false;
    bool pointFrozen_ = false, cubeFrozen_ = false;
};
} // namespace gtc::native
