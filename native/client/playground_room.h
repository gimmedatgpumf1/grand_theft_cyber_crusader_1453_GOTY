#pragma once

#include "client/raster.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gtc::native {
class RoomFreecam;
struct ViewState;

// Step one of the physics rebuild: a single point with a mass, gravity, and one
// plane. There is exactly one update per 20 Hz tick. The travel of that tick is
// tested with a hitscan segment before the point is allowed to move, and the
// plane stops it by pushing back with the weight it carries.
class PlaygroundRoom {
public:
    // Puts the point back at the top of the room, with the velocity it starts
    // with. Zero velocity is the plain drop; anything else is a launch.
    bool reset(Vec3 start = {0, 2, 0}, Vec3 startVelocity = {});
    // Hands the frame's elapsed time to the fixed 20 Hz tick accumulator. No
    // tick is ever split and no tick is ever run twice.
    void advance(double seconds);
    void setDragRay(Vec3 origin, Vec3 direction);
    void beginDrag();
    void endDrag() { dragging_ = false; }
    bool render(NativeFrame& frame, const ViewState& view, const RoomFreecam& camera);
    // Where the point is and how fast it is moving, for the visual harness.
    Vec3 point() const { return point_; }
    Vec3 velocity() const { return velocity_; }
    bool resting() const { return resting_; }

private:
    void tick();
    void updateDrag();
    void buildScene(const Camera& camera);
    void placeMarkers();

    Vec3 point_{0, 2, 0};
    Vec3 velocity_{};
    Vec3 traceFrom_{0, 2, 0}, traceTo_{0, 2, 0};
    std::array<Vec3, 40> ghosts_{};
    std::size_t ghostCount_ = 0, ghostNext_ = 0;
    std::array<float, 4> dragOrigin_{}, dragDirection_{};
    GpuScene scene_;
    std::size_t pointBatch_ = 0, traceBatch_ = 0;
    std::array<std::size_t, 40> ghostBatches_{};
    double accumulator_ = 0;
    bool sceneReady_ = false, dragging_ = false, resting_ = false, haveTrace_ = false;
};
} // namespace gtc::native
