// The seesaw, drawn: the room with the plank in it, and the plank carrying a cube
// down onto the floor. Two pictures, and the state every shape is in under them.
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

static void report(const PlaygroundRoom& room, int tick) {
    const auto& s = room.bodies_[2];
    const Vec3 cube = room.bodyMiddle(0), tri = room.bodyMiddle(1);
    double lowest = 1e9;
    for (std::size_t i = 0; i < s.count; ++i) lowest = std::min(lowest, s.position[i].y);
    std::printf("tick %4d: plank (%+.6f,%+.6f) a%+.6f spin%+.6f lowest%+.6f | cube (%+.6f,%+.6f) | tri (%+.6f,%+.6f)\n",
                tick, s.centre.x, s.centre.y, s.angle, s.spin, lowest, cube.x, cube.y, tri.x, tri.y);
}

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    room.toggleForceArrows();
    room.toggleForceNewtons();
    for (int i = 0; i < 40; ++i) room.advance(1.0 / 20.0);
    RoomFreecam cam;
    // From the triangle's side of the room, looking along the plank: the axle in the
    // middle, both ends, and everything standing round it in the frame.
    cam.reset({-2.10, 1.95, 3.30}, -0.72, -0.30);
    report(room, 0);
    shoot(room, cam, "scratch/seesaw-room.png");
    // And the same axle with the cube riding the raised end down.
    auto& s = room.bodies_[2];
    auto& c = room.bodies_[0];
    c.frozen = false;
    c.centre = {s.centre.x + 0.82, 1.62, 0};
    c.angle = 0.0;
    c.velocity = {};
    c.spin = 0.0;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    for (int i = 0; i < 9; ++i) room.advance(1.0 / 20.0);
    report(room, 9);
    shoot(room, cam, "scratch/seesaw-loaded.png");
    for (int i = 0; i < 600; ++i) room.advance(1.0 / 20.0);
    report(room, 609);
    shoot(room, cam, "scratch/seesaw-rested.png");
    return 0;
}
