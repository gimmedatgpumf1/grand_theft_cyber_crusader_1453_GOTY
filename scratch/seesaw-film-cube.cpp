// The seesaw swung under a square lying on its face, one drawn frame per tick, so the
// motion of the shape on the plank can be watched instead of read off a table.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr double kDt = 1.0 / 20.0;
int shotWidth = 640, shotHeight = 440;
const RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* labelsFrom = nullptr;
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
void shoot(PlaygroundRoom& room, RoomFreecam& cam, const std::string& path) {
    ViewState view;
    view.width = shotWidth;
    view.height = shotHeight;
    view.gpu = true;
    cameraFrom = &cam;
    labelsFrom = &room;
    NativeFrame frame;
    if (!room.render(frame, view, cam) || !frame.save(path)) std::fprintf(stderr, "shot failed %s\n", path.c_str());
}
}  // namespace

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    for (int i = 0; i < 40; ++i) room.advance(kDt);
    auto& s = room.bodies_[PlaygroundRoom::kSeesaw];
    s.frozen = false;
    s.angle = 0.40;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.spin = 0.0;
    s.velocity = {};
    room.ball_.frozen = true;
    room.ball_.position = {0.0, -5.0, 0.0};
    // The square, flat on the plank's face, a little way out from the axle.
    auto& cube = room.bodies_[0];
    cube.frozen = false;
    cube.angle = s.angle;
    cube.velocity = {};
    cube.spin = 0.0;
    {
        const double c = std::cos(s.angle), sn = std::sin(s.angle);
        const double lx = 0.55, ly = 0.12 + 0.35;
        cube.centre = {s.centre.x + c * lx - sn * ly, s.centre.y + sn * lx + c * ly, 0.0};
    }
    PlaygroundRoom::placeCorners(cube);
    cube.previous = cube.position;
    cube.previousCentre = cube.centre;
    cube.previousAngle = cube.angle;
    RoomFreecam cam;
    cam.reset({-3.1, 1.55, 3.10}, 0.02, -0.16);
    char name[96];
    for (int k = 0; k < 60; ++k) {
        room.advance(kDt);
        std::snprintf(name, sizeof name, "scratch/film3/frame%03d.png", k);
        shoot(room, cam, name);
    }
    std::printf("60 frames at 20 Hz: scratch/film3/frame000.png ... frame059.png\n");
    return 0;
}
