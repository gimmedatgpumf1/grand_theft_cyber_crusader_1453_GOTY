#include "client/skeleton_room.h"

#include <thread>

#include <algorithm>
#include <cmath>

namespace gtc::native {
namespace {

// A room measured in metres, like the skeletons in it: eight metres by eight,
// three metres of headroom. The old room was built in engine units and the
// conversion is exactly the sort of thing that turns a rig test into an
// argument about scale.
constexpr double kRoomHalf = 6.0;
constexpr double kRoomHeight = 3.0;
constexpr double kTile = 1.0;

void drawFloor(Rasterizer& rasterizer, const Camera& camera) {
    Material dark;
    dark.color = colorHex("#26332c");
    dark.constantLighting = true;
    Material pale;
    pale.color = colorHex("#54614f");
    pale.constantLighting = true;
    // A slab first. Without it the grout gap between tiles shows the empty void
    // below the room, which reads as a crack running through the floor.
    Material slab;
    slab.color = colorHex("#1d2823");
    slab.constantLighting = true;
    drawBox(rasterizer, camera, {0, -0.06, 0}, {kRoomHalf * 2, 0.08, kRoomHalf * 2}, slab);
    // Tiles are drawn individually rather than as one textured plane: the room
    // is a test bench, so a tile is a unit of measurement you can count.
    const int tiles = static_cast<int>(kRoomHalf * 2 / kTile);
    for (int x = 0; x < tiles; ++x) {
        for (int z = 0; z < tiles; ++z) {
            const double x0 = -kRoomHalf + x * kTile, z0 = -kRoomHalf + z * kTile;
            drawBox(rasterizer, camera, {x0 + kTile * 0.5, -0.02, z0 + kTile * 0.5}, {kTile * 0.98, 0.04, kTile * 0.98},
                    ((x + z) & 1) ? pale : dark);
        }
    }
}

void drawWalls(Rasterizer& rasterizer, const Camera& camera) {
    Material wall;
    wall.color = colorHex("#2f3a33");
    wall.constantLighting = true;
    Material skirting;
    skirting.color = colorHex("#1b241f");
    skirting.constantLighting = true;
    // Thickness 0.1 sits the inner face exactly on the room bound; the boxes
    // that meet in a corner therefore share a plane, and two coplanar faces at
    // identical depth z-fight into a seam. Each wall is pushed back by a
    // millimetre so the meeting faces are not coincident.
    const double inset = 0.001;
    drawBox(rasterizer, camera, {0, kRoomHeight * 0.5, -kRoomHalf - inset}, {kRoomHalf * 2, kRoomHeight, 0.1}, wall);
    drawBox(rasterizer, camera, {0, kRoomHeight * 0.5, kRoomHalf + inset}, {kRoomHalf * 2, kRoomHeight, 0.1}, wall);
    drawBox(rasterizer, camera, {-kRoomHalf - inset, kRoomHeight * 0.5, 0}, {0.1, kRoomHeight, kRoomHalf * 2}, wall);
    drawBox(rasterizer, camera, {kRoomHalf + inset, kRoomHeight * 0.5, 0}, {0.1, kRoomHeight, kRoomHalf * 2}, wall);
    drawBox(rasterizer, camera, {0, kRoomHeight, 0}, {kRoomHalf * 2 + 0.2, 0.1, kRoomHalf * 2 + 0.2}, skirting);
    // One-metre ticks on the back wall, so the skeletons' heights can be read
    // off the picture instead of trusted.
    Material tick;
    tick.color = colorHex("#7d8f6a");
    tick.constantLighting = true;
    for (int metre = 1; metre <= 3; ++metre) {
        drawBox(rasterizer, camera, {-kRoomHalf + 0.4, metre * 1.0, -kRoomHalf + 0.06},
                {0.5, 0.02, 0.02}, tick);
    }
}

} // namespace

bool SkeletonRoom::load(const std::string& root, std::string& error) {
    const std::string directory = root + "/tools/skeleton-source/";
    if (!male_.load(directory + "skeleton-male.obj", error)) return false;
    if (!female_.load(directory + "skeleton-female.obj", error)) return false;
    error_.clear();
    return true;
}

void SkeletonRoom::render(NativeFrame& frame, const ViewState& view, double seconds) {
    Camera camera;
    camera.width = std::max(1, view.width);
    camera.height = std::max(1, view.height);
    camera.fovDegrees = 50;
    // The camera orbits the pair at eye height; both bodies stand on the floor
    // line so their heights can be compared directly.
    const double distance = 3.5;
    const double height = 1.15;
    camera.target = {0, 0.95, 0};
    camera.position = {std::sin(view.yaw) * distance, height, std::cos(view.yaw) * distance};
    camera.worldSpaceGpu = view.gpu;
    // The idle pose is worth rebuilding the batches for four times a second,
    // not sixty: the pose runs at the simulation's 15 Hz, the camera does not,
    // and the camera is the only thing that changes between those rebuilds.
    const double pose = std::floor(std::max(0.0, seconds) * 15.0) / 15.0;
    const bool reuse = view.gpu && sceneReady_ && pose == cachedPose_ &&
        camera.width == cachedWidth_ && camera.height == cachedHeight_;
    if (reuse) {
        frame.width = camera.width;
        frame.height = camera.height;
        frame.rgb.clear();
        if (!renderGpuScene(scene_, frame)) frame.rgb.clear();
        frame.logicalUi();
        return;
    }
    // Each body is built on its own rasterizer, on its own thread. A scanned
    // skeleton is 195k triangles, so posing and emitting two of them in a row
    // was the most expensive thing in the room -- and the two halves share
    // nothing, which is the easiest kind of work to split.
    male_.poseIdle(seconds, 0.0);
    female_.poseIdle(seconds, 1.7);
    Rasterizer room(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), view.gpu,
                    view.gpu ? std::move(scene_) : GpuScene{});
    room.setCamera(camera);
    drawFloor(room, camera);
    drawWalls(room, camera);
    room.setObject(1, {0.0, 0.0}, 3.14159265F);
    room.setObject(0, {0.0, 0.0}, 0.0F);

    Rasterizer second(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), view.gpu);
    second.setCamera(camera);
    second.setObject(1, {0.0, 0.0}, 3.14159265F);

    std::thread worker([&] {
        second.setObject(2, {0.0, 0.0}, 3.14159265F);
        female_.draw(second, camera, {0.52, 0, 0}, 3.14159265);
    });
    male_.draw(room, camera, {-0.52, 0, 0}, 3.14159265);
    worker.join();

    if (view.gpu) {
        // The GPU path merges the two halves and hands them to the backend;
        // nothing is rasterized here, so finish() has no work to do.
        std::vector<GpuScene> halves;
        halves.push_back(room.takeGpuScene());
        halves.push_back(second.takeGpuScene());
        second.finish();
        scene_ = mergeScenes(std::move(halves));
        scene_.revision = ++revision_;
        sceneReady_ = true;
        cachedPose_ = pose;
        cachedWidth_ = camera.width;
        cachedHeight_ = camera.height;
        frame.width = camera.width;
        frame.height = camera.height;
        frame.rgb.clear();
        if (!renderGpuScene(scene_, frame)) frame.rgb.clear();
        frame.logicalUi();
        return;
    }
    // Software path: the second half was built on its own rasterizer, so it is
    // drawn straight into the same buffers.
    second.drain(room);
    frame = room.finish();
    frame.logicalUi();
}

} // namespace gtc::native
