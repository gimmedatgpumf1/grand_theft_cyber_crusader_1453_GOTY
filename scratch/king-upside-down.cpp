// The King's arrangement drawn by the room's own renderer, twice: once as the room
// is left after a knock-over, and once with the plank swept across it by a hand.
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
#include <functional>
#include <string>

namespace {
constexpr double kDt = 1.0 / 20.0;
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

void shoot(PlaygroundRoom& room, RoomFreecam& cam, const std::string& path) {
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

void report(const PlaygroundRoom& room, const char* when) {
    double kinetic = 0.0, potential = 0.0;
    room.energyBudget(kinetic, potential);
    double worstAir = 0.0;
    for (std::size_t i = 0; i < room.forceCount_; ++i)
        if (room.forces_[i].kind == PlaygroundRoom::ForceKind::Drag) worstAir = std::max(worstAir, length(room.forces_[i].force));
    std::printf("%-28s KE %.4f  PE %.3f | worst air arrow %.2f N |", when, kinetic, potential, worstAir);
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        const auto& b = room.bodies_[i];
        if (!b.count) continue;
        std::printf("  b%zu a%+.3f v(%+.2f,%+.2f) w%+.2f", i, b.angle, b.velocity.x, b.velocity.y, b.spin);
    }
    std::printf("\n");
}
}  // namespace

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    room.toggleForceArrows();
    room.toggleForceNewtons();
    for (int i = 0; i < 30; ++i) room.advance(kDt);
    // The triangle turned over: horizontal edge on top, one vertex down.
    auto& t = room.bodies_[PlaygroundRoom::kTriangle];
    t.angle = 2.62;
    t.centre = {-2.30, 1.75, 0};
    PlaygroundRoom::placeCorners(t);
    {
        double low = 1e9;
        for (std::size_t i = 0; i < t.count; ++i) low = std::min(low, t.position[i].y);
        if (low < 0.0) t.centre.y -= low;
        PlaygroundRoom::placeCorners(t);
    }
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
    t.velocity = {};
    t.spin = 0.0;
    t.frozen = false;
    RoomFreecam cam;
    cam.reset({-1.95, 1.85, 3.10}, -0.30, -0.22);
    for (int i = 0; i < 200; ++i) room.advance(kDt);
    report(room, "triangle turned over, 10 s");
    shoot(room, cam, "scratch/king-upside-down.png");
    return 0;
}
