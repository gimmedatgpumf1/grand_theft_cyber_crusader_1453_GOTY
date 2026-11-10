#pragma once

#include "client/raster.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gtc::native {
class RoomFreecam;
struct ViewState;

// The physics rebuild, one step at a time. A point with a mass, gravity and one
// plane; then a cube made of four such points, one at each corner, held at a
// fixed distance from each other. There is exactly one update per 20 Hz tick.
// The travel of that tick is tested with a hitscan segment before anything is
// allowed to move, and the plane stops what reaches it by pushing back with the
// weight it carries. A body with no velocity and no net force is frozen.
class PlaygroundRoom {
public:
    // Puts the point back at the top of the room, with the velocity it starts
    // with. Zero velocity is the plain drop; anything else is a launch.
    bool reset(Vec3 start = {0, 1.9, 0}, Vec3 startVelocity = {});
    // Moves the point alone, leaving the square where it is.
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
    // The cube: four corners, as drawn and as simulated.
    Vec3 cubeCorner(std::size_t index) const { return index < 4 ? cubeShown_[index] : Vec3{}; }
    std::array<Vec3, 4> cubeCorners() const { return cubePosition_; }
    bool cubeFrozen() const { return cubeFrozen_; }
    bool cubeSettled() const { return cubeFrozen_; }
    // How many times the point has struck the square, for the visual harness.
    int pointCubeContacts() const { return pointCubeContacts_; }
    Vec3 cubeCornerVelocity(std::size_t index) const { return index < 4 ? cubeVelocity_[index] : Vec3{}; }

private:
    // What the mouse is holding, if anything.
    enum class DragTarget { None, Point, Cube };
    void tick();
    void tickCube();
    bool supportedByCube() const;
    bool cubeTopAt(double x, double& height) const;
    // The point meets the square: energy into the corner it struck, a push back
    // along the normal.
    void hitCube(Vec3 from, Vec3 to, double hit);
    void freezeCheck();
    void updateDrag();
    void buildScene(const Camera& camera);
    void placeMarkers();
    void interpolate();

    Vec3 point_{0, 2, 0}, previous_{0, 2, 0}, shown_{0, 2, 0};
    Vec3 velocity_{};
    Vec3 traceFrom_{0, 2, 0}, traceTo_{0, 2, 0};
    std::array<Vec3, 40> ghosts_{};
    std::size_t ghostCount_ = 0, ghostNext_ = 0;
    // The cube: four corners of equal mass, and the six distances between them
    // that are not allowed to change.
    std::array<Vec3, 4> cubePosition_{}, cubePrevious_{}, cubeShown_{}, cubeVelocity_{};
    std::array<double, 6> cubeRest_{};
    std::array<float, 4> dragOrigin_{}, dragDirection_{};
    GpuScene scene_;
    std::size_t pointBatch_ = 0, traceBatch_ = 0, cubeFaceBatch_ = 0;
    std::array<std::size_t, 4> cubeEdgeBatches_{}, cubeCornerBatches_{};
    std::array<std::size_t, 40> ghostBatches_{};
    double accumulator_ = 0;
    DragTarget dragTarget_ = DragTarget::None;
    Vec3 planeGrab_{}, grabOffset_{};
    bool sceneReady_ = false, resting_ = false, haveTrace_ = false;
    bool pointFrozen_ = false, cubeFrozen_ = false, restingOnCube_ = false;
    int cubeQuietTicks_ = 0, pointCubeContacts_ = 0;
};
} // namespace gtc::native
