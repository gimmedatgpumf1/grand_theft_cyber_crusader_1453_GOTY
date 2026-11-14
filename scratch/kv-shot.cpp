// Is a resting pair really still?
//
// Two pictures of the same room six hundred ticks apart, and the numbers under them:
// where every shape is at the first, and where it is at the second. If anything in
// the room walks, these two pictures are different and the difference is the walk.
// The room is put in the state the King photographed: the cube left leaning on the
// triangle's slope, which slides down and comes to rest on the floor beside it.
#include <array>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#define private public
#include "client/playground_pair.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#undef private
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"
#include <cstdio>
using namespace gtc::native;

namespace {
const RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* labelsFrom = nullptr;
int shotWidth = 900, shotHeight = 600;

Vec3 applyTo(const std::array<double, 16>& m, const Vec3& p, bool point) {
    const double w = point ? 1.0 : 0.0;
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * w, m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * w,
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * w};
}

bool renderOnHost(const GpuScene& scene, NativeFrame& frame) {
    if (cameraFrom == nullptr) return false;
    const Camera camera = cameraFrom->camera(shotWidth, shotHeight, false);
    Rasterizer raster(shotWidth, shotHeight, scene.background, camera.focal(), false);
    for (const GpuBatch& batch : scene.batches) {
        if (batch.isUi) continue;
        for (std::size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
            std::array<RenderVertex, 3> corners;
            for (int k = 0; k < 3; ++k) {
                const GpuVertex& v = batch.vertices[batch.indices[i + static_cast<std::size_t>(k)]];
                const Vec3 world = applyTo(batch.modelMatrix, {v.x, v.y, v.z}, true);
                const Vec3 normal = applyTo(batch.modelMatrix, {v.nx, v.ny, v.nz}, false);
                corners[static_cast<std::size_t>(k)] = cameraVertex(camera, world, normalized(normal), world, {v.u, v.v});
            }
            raster.triangle(corners, batch.material);
        }
    }
    frame = raster.finish();
    frame.wantsBgra = false;
    if (labelsFrom != nullptr) compositeUi(frame, labelsFrom->forceLabels_);
    return true;
}
}  // namespace

static void shoot(PlaygroundRoom& room, RoomFreecam& cam, const std::string& path) {
    ViewState view;
    view.width = shotWidth;
    view.height = shotHeight;
    view.gpu = true;
    cameraFrom = &cam;
    labelsFrom = &room;
    NativeFrame frame;
    if (!room.render(frame, view, cam) || !frame.save(path)) std::fprintf(stderr, "shot failed %s\n", path.c_str());
    else std::printf("wrote %s\n", path.c_str());
}

static void report(const PlaygroundRoom& room, int tick, const char* what) {
    std::printf("tick %4d %s: ball (%+.6f,%+.6f) v(%+.6f,%+.6f) | cube c(%+.6f,%+.6f) v(%+.6f,%+.6f) | tri c(%+.6f,%+.6f) v(%+.6f,%+.6f)\n",
                tick, what, room.ball_.position.x, room.ball_.position.y, room.ball_.velocity.x, room.ball_.velocity.y,
                room.bodies_[0].centre.x, room.bodies_[0].centre.y, room.bodies_[0].velocity.x, room.bodies_[0].velocity.y,
                room.bodies_[1].centre.x, room.bodies_[1].centre.y, room.bodies_[1].velocity.x, room.bodies_[1].velocity.y);
}

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    room.toggleForceArrows();
    room.toggleForceNewtons();
    for (int i = 0; i < 20; ++i) room.advance(1.0 / 20.0);
    auto& cube = room.bodies_[0];
    cube.centre = {-0.60, 0.35, 0};
    cube.angle = 0.0;
    cube.velocity = {}; cube.spin = 0.0; cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position; cube.previousCentre = cube.centre; cube.previousAngle = cube.angle;
    auto& tri = room.bodies_[1];
    tri.angle = 0.5 * 3.14159265358979323846;
    tri.centre = {-0.25 + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(tri);
    tri.previous = tri.position; tri.previousCentre = tri.centre; tri.previousAngle = tri.angle;
    room.ball_.position = {-0.18, 0.60, 0};
    room.ball_.previous = room.ball_.position;
    room.ball_.velocity = {}; room.ball_.spin = 0.0; room.ball_.frozen = false;
    room.clearRepairCounts();
    RoomFreecam cam;
    cam.reset({-0.10, 1.35, 5.20}, -0.05, -0.12);
    const int ticks[] = {0, 40, 120, 200, 400};
    char path[64];
    int at = 0;
    for (int target : ticks) {
        while (at < target) { room.advance(1.0 / 20.0); ++at; }
        std::snprintf(path, sizeof path, "scratch/kv-%d", at);
        report(room, at, path);
        shoot(room, cam, std::string(path) + ".png");
    }
    std::printf("repairs: through %zu clamped %zu lifted %zu\n", room.ballFoundThroughCount(),
                room.bodyStepClampedCount(), room.ballLiftedOutCount());
    return 0;
}
