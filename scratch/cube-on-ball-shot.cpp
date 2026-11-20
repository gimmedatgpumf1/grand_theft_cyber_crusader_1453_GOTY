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
#include "client/playground_room_tick.h"
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

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    room.toggleForceArrows();
    room.toggleForceNewtons();
    for (int i = 0; i < 200; ++i) room.advance(1.0 / 20.0);
    // The King's own case: a ball put down on clear floor and left to settle there,
    // and the cube dropped onto its shoulder.
    room.ball_.position = {0.30, room.ball_.radius, 0};
    room.ball_.velocity = {};
    room.ball_.spin = 0.0;
    room.ball_.frozen = false;
    room.ball_.previous = room.ball_.position;
    for (int i = 0; i < 40; ++i) room.advance(1.0 / 20.0);
    PlaygroundRoom::Body& cube = room.bodies_[0];
    cube.centre = {0.30 + 0.16, room.ball_.radius + 0.12 + 0.35 + 0.50, 0};
    cube.angle = 0.0;
    cube.velocity = {};
    cube.spin = 0.0;
    cube.frozen = false;
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position;
    cube.previousCentre = cube.centre;
    cube.previousAngle = cube.angle;
    RoomFreecam cam;
    cam.reset({0.20, 1.15, 4.60}, -0.05, -0.14);
    const int ticks[] = {0, 6, 7, 8, 10, 14, 22, 40};
    char path[64];
    int at = 0;
    for (int target : ticks) {
        while (at < target) { room.advance(1.0 / 20.0); ++at; }
        std::snprintf(path, sizeof path, "scratch/cube-on-ball-%d", at);
        std::printf("%-26s cube (%+.4f,%+.4f) v(%+.3f,%+.3f) | ball (%+.4f,%+.4f) v(%+.3f,%+.3f) spin %+.2f\n",
                    path, cube.centre.x, cube.centre.y, cube.velocity.x, cube.velocity.y, room.ball_.position.x,
                    room.ball_.position.y, room.ball_.velocity.x, room.ball_.velocity.y, room.ball_.spin);
        NativeFrame frame;
        ViewState view;
        view.width = shotWidth;
        view.height = shotHeight;
        view.gpu = true;
        cameraFrom = &cam;
        labelsFrom = &room;
        if (!room.render(frame, view, cam) || !frame.save(std::string(path) + ".png"))
            std::fprintf(stderr, "shot failed %s\n", path);
    }
    return 0;
}
