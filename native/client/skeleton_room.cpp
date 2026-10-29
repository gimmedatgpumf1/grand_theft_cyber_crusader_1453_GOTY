#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include "client/threadpool.h"

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
    ready_ = false;
    lineSceneReady_ = false;
    restReady_ = false;
    sceneReady_ = false;
    const std::string directory = root + "/tools/skeleton-source/";
    if (!male_.load(directory + "skeleton-male.obj", error)) return false;
    if (!female_.load(directory + "skeleton-female.obj", error)) return false;
    if (!maleLines_.load(directory + "skeleton-male.lines", error)) return false;
    if (!femaleLines_.load(directory + "skeleton-female.lines", error)) return false;
    error_.clear();
    ready_ = true;
    return true;
}

void SkeletonRoom::render(NativeFrame& frame, const ViewState& view, double seconds, const RoomFreecam* freecam) {
    Camera camera = freecam ? freecam->camera(view.width, view.height, view.gpu) : Camera{};
    if (!freecam) {
        // Existing benchmark/preview orbit, independent of the interactive freecam.
        camera.width = std::max(1, view.width);
        camera.height = std::max(1, view.height);
        camera.fovDegrees = 50;
        camera.target = {0, 0.95, 0};
        camera.position = {std::sin(view.yaw) * 3.5, 1.15, std::cos(view.yaw) * 3.5};
        camera.worldSpaceGpu = view.gpu;
    }
    if (lineView_) {
        // Line view is a static inspection of the source pose. Its
        // triangles are built once and stay on the device; meshes are not drawn.
        if (view.gpu) {
            if (!lineSceneReady_) {
                Rasterizer raster(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), true);
                raster.setCamera(camera);
                drawFloor(raster, camera); drawWalls(raster, camera);
                maleLines_.draw(raster, {-0.52, 0, 0}, 0.0, 9001);
                femaleLines_.draw(raster, {0.52, 0, 0}, 3.14159265, 8001);
                lineScene_ = raster.takeGpuScene();
                lineScene_.revision = ++revision_;
                lineSceneReady_ = true;
            }
            lineScene_.width = camera.width; lineScene_.height = camera.height;
            lineScene_.focal = camera.focal(); lineScene_.viewMatrix = camera.viewMatrix();
            lineScene_.covered = freecam == nullptr;
            frame.width = camera.width; frame.height = camera.height;
            frame.wantsBgra = true; frame.rgb.clear();
            if (!renderGpuScene(lineScene_, frame)) frame.rgb.clear();
            frame.logicalUi();
            return;
        }
        Rasterizer raster(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), false);
        raster.setCamera(camera);
        drawFloor(raster, camera); drawWalls(raster, camera);
        maleLines_.draw(raster, {-0.52, 0, 0}, 0.0, 9001);
        femaleLines_.draw(raster, {0.52, 0, 0}, 3.14159265, 8001);
        frame = raster.finish();
        frame.logicalUi();
        return;
    }
    // Room geometry is in world space: resizing or orbiting changes only the
    // projection and view matrix, not the floor, walls or skeleton meshes.
    const auto updateCamera = [&] {
        scene_.width = camera.width;
        scene_.height = camera.height;
        scene_.focal = camera.focal();
        scene_.viewMatrix = camera.viewMatrix();
        // A free camera can leave the room, so its background must be cleared.
        scene_.covered = freecam == nullptr;
    };
    (void)seconds;
    const double pose = 0.0;
    const bool reuse = view.gpu && sceneReady_ && pose == cachedPose_;
    if (reuse) {
        updateCamera();
        frame.width = camera.width;
        frame.height = camera.height;
        frame.wantsBgra = true;
        frame.rgb.clear();
        if (!renderGpuScene(scene_, frame)) frame.rgb.clear();
        frame.logicalUi();
        return;
    }
    // Both bodies retain the original source pose while the new rig is developed.
    const bool poseChanged = pose != cachedPose_;

    if (view.gpu) {
        // A bone is a rigid body. So the triangles are emitted once, in rest
        // position, one batch per bone -- and a pose is then 56 matrices rather
        // than 195,000 transformed vertices per body plus the same geometry
        // travelling to the GPU again. Both bodies retain their original source pose.
        if (!restReady_) {
            const int maleBase = 9001;
            const int femaleBase = 8001;
            Rasterizer room(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), true,
                            std::move(scene_));
            room.setCamera(camera);
            drawFloor(room, camera);
            drawWalls(room, camera);
            room.setObject(0, {0.0, 0.0}, 0.0F);
            GpuScene parts = room.takeGpuScene();
            male_.buildRestGeometry(room, camera, maleBase);
            female_.buildRestGeometry(room, camera, femaleBase);
            std::vector<GpuScene> halves;
            halves.push_back(std::move(parts));
            halves.push_back(room.takeGpuScene());
            scene_ = mergeScenes(std::move(halves));
            scene_.revision = ++revision_;
            // Which batch belongs to which bone. Batches keep their identity
            // until the scene is rebuilt, so this is looked up once.
            boneBatches_.clear();
            for (std::size_t index = 0; index < scene_.batches.size(); ++index) {
                const std::uint32_t id = scene_.batches[index].objectId;
                if (id == 0) continue;
                boneBatches_.emplace_back(id, index);
            }
            sceneTriangles_ = 0;
            for (const GpuBatch& batch : scene_.batches) sceneTriangles_ += batch.vertices.size() / 3;
            restReady_ = true;
            sceneReady_ = true;
        }
        // The pose is the only thing that changes: write the bone matrices into
        // the batches they belong to. No vertex data moves. The static pose is solved only once.
        const bool poseMoved = poseChanged;
        if (poseMoved) {
            // The OpenSkeleton source faces the opposite way to the former atlas.
            cachedMaleMatrices_ = male_.boneMatrices({-0.52, 0, 0}, 0.0);
            cachedFemaleMatrices_ = female_.boneMatrices({0.52, 0, 0}, 3.14159265);
        }
        const std::vector<std::array<double, 16>>& maleMatrices = cachedMaleMatrices_;
        const std::vector<std::array<double, 16>>& femaleMatrices = cachedFemaleMatrices_;
        for (const std::pair<std::uint32_t, std::size_t>& entry : boneBatches_) {
            if (!poseMoved) break;
            const int id = static_cast<int>(entry.first);
            const std::vector<std::array<double, 16>>* matrices = nullptr;
            std::size_t boneIndex = 0;
            if (id >= 9001 && static_cast<std::size_t>(id - 9001) < maleMatrices.size()) {
                matrices = &maleMatrices;
                boneIndex = static_cast<std::size_t>(id - 9001);
            } else if (id >= 8001 && static_cast<std::size_t>(id - 8001) < femaleMatrices.size()) {
                matrices = &femaleMatrices;
                boneIndex = static_cast<std::size_t>(id - 8001);
            }
            if (matrices == nullptr) continue;
            scene_.batches[entry.second].modelMatrix = (*matrices)[boneIndex];
        }
        cachedPose_ = pose;
        updateCamera();
        frame.wantsBgra = true;
        frame.width = camera.width;
        frame.height = camera.height;
        frame.rgb.clear();
        if (!renderGpuScene(scene_, frame)) frame.rgb.clear();
        frame.logicalUi();
        return;
    }
    // Software path: the second half was built on its own rasterizer, so it is
    // drawn straight into the same buffers. Two threads, one body each -- the
    // software rasterizer needs posed vertices, so the rest-geometry trick does
    // not apply to it.
    Rasterizer room(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), false);
    room.setCamera(camera);
    drawFloor(room, camera);
    drawWalls(room, camera);
    Rasterizer second(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), false);
    second.setCamera(camera);
    std::thread worker([&] { female_.draw(second, camera, {0.52, 0, 0}, 3.14159265); });
    male_.draw(room, camera, {-0.52, 0, 0}, 0.0);
    worker.join();
    second.drain(room);
    frame = room.finish();
    frame.logicalUi();
}

} // namespace gtc::native
