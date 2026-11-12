// Frames from the same scene the GPU draws, rasterised on the host: this machine
// has no Vulkan loader, and a picture of where things are is worth having anyway.
// The force numbers are composited from the room's own label layer, so the words
// in the picture are the words the interface would have written.
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
        if (batch.isUi) continue;  // the labels are composited from the layer instead
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
    double floorCarry = 0.0, pressed = 0.0;
    for (std::size_t i = 0; i < room.forceCount_; ++i) {
        const auto& f = room.forces_[i];
        if (f.owner != 1) continue;
        const bool pair = f.kind == PlaygroundRoom::ForceKind::Pair || f.kind == PlaygroundRoom::ForceKind::Friction;
        if (pair && f.at.y > 0.5) pressed -= f.force.y;
        if (f.kind == PlaygroundRoom::ForceKind::Contact && f.at.y < 0.05) floorCarry += f.force.y;
    }
    const Vec3 cube = room.bodyMiddle(0), tri = room.bodyMiddle(1);
    std::printf("tick %2d: cube (%+.3f,%+.3f) | triangle under %7.3f N of its own plus %6.3f N of cube | tri middle (%+.3f,%+.3f)\n",
                tick, cube.x, cube.y, floorCarry - pressed, pressed, tri.x, tri.y);
}

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    room.toggleForceArrows();
    room.toggleForceNewtons();
    for (int i = 0; i < 160; ++i) room.advance(1.0 / 20.0);
    RoomFreecam cam;
    cam.reset({-0.60, 1.60, 6.20}, -0.06, -0.08);
    report(room, -1);
    shoot(room, cam, "scratch/host-rest.png");

    // The construction from the screenshot: the cube lying along the hypotenuse.
    {
        const Vec3 a{-2.6, 2.1, 0}, b{-0.5, 0.0, 0};
        const Vec3 on = a + (b - a) * 0.33;
        const double slope = std::atan2(b.y - a.y, b.x - a.x);
        const double half = 0.35, c = std::cos(slope), s = std::sin(slope);
        const Vec3 local{-half, -half, 0};
        auto& cube = room.bodies_[0];
        cube.centre = Vec3{on.x - (local.x * c - local.y * s), on.y - (local.x * s + local.y * c), 0};
        cube.angle = slope;
        cube.velocity = {};
        cube.spin = 0.0;
        cube.frozen = false;
        room.placeCorners(cube);
        cube.previous = cube.position;
    }
    RoomFreecam near;
    near.reset({-1.20, 1.35, 5.40}, -0.06, -0.10);
    shoot(room, near, "scratch/host-lean-0.png");
    for (int tick = 1; tick <= 40; ++tick) {
        room.advance(1.0 / 20.0);
        report(room, tick);
        if (tick == 1) shoot(room, near, "scratch/host-lean-1.png");
        if (tick == 8) shoot(room, near, "scratch/host-lean-8.png");
        if (tick == 12) shoot(room, near, "scratch/host-lean-12.png");
        if (tick == 40) shoot(room, near, "scratch/host-lean-40.png");
    }
    return 0;
}
