#include "client/skeleton_room.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    Material skirting;
    skirting.color = colorHex("#1b241f");
    drawBox(rasterizer, camera, {0, kRoomHeight * 0.5, -kRoomHalf}, {kRoomHalf * 2, kRoomHeight, 0.1}, wall);
    drawBox(rasterizer, camera, {0, kRoomHeight * 0.5, kRoomHalf}, {kRoomHalf * 2, kRoomHeight, 0.1}, wall);
    drawBox(rasterizer, camera, {-kRoomHalf, kRoomHeight * 0.5, 0}, {0.1, kRoomHeight, kRoomHalf * 2}, wall);
    drawBox(rasterizer, camera, {kRoomHalf, kRoomHeight * 0.5, 0}, {0.1, kRoomHeight, kRoomHalf * 2}, wall);
    drawBox(rasterizer, camera, {0, kRoomHeight, 0}, {kRoomHalf * 2, 0.1, kRoomHalf * 2}, skirting);
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
    Rasterizer rasterizer(camera.width, camera.height, colorHex("#0d1210"), camera.focal());
    drawFloor(rasterizer, camera);
    drawWalls(rasterizer, camera);
    male_.poseIdle(seconds, 0.0);
    female_.poseIdle(seconds, 1.7);
    male_.draw(rasterizer, camera, {-0.52, 0, 0}, 3.14159265);
    female_.draw(rasterizer, camera, {0.52, 0, 0}, 3.14159265);
    frame = rasterizer.finish();
    frame.logicalUi();
    // The heights are read back out of the loaded meshes, not typed in: if a
    // later change alters the assets, this caption says so.
    const auto centimetres = [](double metres) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f CM", metres * 100.0);
        return std::string(buffer);
    };
    drawText(frame, 24, 24, "TEST ROOM // SKELETON BENCH", colorHex("#d7ff54"));
    drawText(frame, 24, 56, "MALE " + centimetres(male_.height()), colorHex("#b4c5ba"), 2);
    drawText(frame, 24, 80, "FEMALE " + centimetres(female_.height()), colorHex("#b4c5ba"), 2);
    drawText(frame, 24, 108, "C RETURNS TO THE STREET", colorHex("#b4c5ba"), 2);
}

} // namespace gtc::native
