// The King's picture, reached the way it can legally be reached: the plank is let go
// and swings down onto whatever is under it, instead of being snapped to an angle.
//
// If the room can arrive at a tipped triangle with the plank lying across it, this
// gets there and says what it costs in energy doing it. If it cannot, that is the
// answer too: the state in the picture would then only be reachable by something that
// put a shape somewhere by hand, and that is the bug to find.
#include "common.h"
#include "client/playground_shapes.h"
#include "client/room_freecam.h"
#include "client/renderer.h"
#include "client/ui_layer.h"
#include "client/playground_pair.h"
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

double energyOf(const PlaygroundRoom& r) {
    double kinetic = 0.0, potential = 0.0;
    r.energyBudget(kinetic, potential);
    return kinetic + potential;
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
}  // namespace

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    room.toggleForceArrows();
    room.toggleForceNewtons();
    for (int i = 0; i < 30; ++i) room.advance(kDt);
    // The triangle lifted clear, tilted a long way over, and let go.
    auto& t = room.bodies_[PlaygroundRoom::kTriangle];
    t.centre = {-1.900, 3.2, 0};
    t.angle = -0.90;
    PlaygroundRoom::placeCorners(t);
    double lowest = 1e9;
    for (std::size_t i = 0; i < t.count; ++i) lowest = std::min(lowest, t.position[i].y);
    if (lowest < 0.0) t.centre.y -= lowest;
    PlaygroundRoom::placeCorners(t);
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
    t.velocity = {};
    t.spin = 0.0;
    t.frozen = false;
    const double start = energyOf(room);
    double peak = start;
    int peakTick = -1;
    for (int k = 0; k < 400; ++k) {
        room.advance(kDt);
        const double now = energyOf(room);
        if (now > peak) {
            peak = now;
            peakTick = k;
        }
    }
    std::printf("triangle knocked over: energy start %.2f, peak %.2f (+%.2f J at t%d) now %.2f\n", start, peak,
                peak - start, peakTick, energyOf(room));
    std::printf("  tri a%+.4f c(%+.3f,%+.3f) frz%d\n", room.bodies_[1].angle, room.bodies_[1].centre.x,
                room.bodies_[1].centre.y, int(room.bodies_[1].frozen));
    // Now the plank, let go and left to swing down onto it by its own weight.
    auto& s = room.bodies_[PlaygroundRoom::kSeesaw];
    s.angle = 0.9;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.spin = 0.0;
    s.frozen = false;
    const double before = energyOf(room);
    double swingPeak = before;
    int swingTick = -1;
    for (int k = 0; k < 300; ++k) {
        room.advance(kDt);
        const double now = energyOf(room);
        if (now > swingPeak) {
            swingPeak = now;
            swingTick = k;
        }
    }
    std::printf("plank let go from +0.90 and swung down: energy before %.2f, peak %.2f (+%.2f J at t%d) now %.2f\n",
                before, swingPeak, swingPeak - before, swingTick, energyOf(room));
    std::printf("  plank a%+.4f w%+.4f lowest ", room.bodies_[2].angle, room.bodies_[2].spin);
    double lowestNow = 1e9;
    for (std::size_t i = 0; i < room.bodies_[2].count; ++i) lowestNow = std::min(lowestNow, room.bodies_[2].position[i].y);
    std::printf("%+.4f | tri a%+.4f\n", lowestNow, room.bodies_[1].angle);
    double worstDrag = 0.0;
    for (std::size_t i = 0; i < room.forceCount_; ++i)
        if (room.forces_[i].kind == PlaygroundRoom::ForceKind::Drag) worstDrag = std::max(worstDrag, length(room.forces_[i].force));
    std::printf("  largest arrow now on screen: %.2f N of it is air\n", worstDrag);
    RoomFreecam cam;
    cam.reset({-1.60, 1.75, 3.30}, -0.72, -0.26);
    shoot(room, cam, "scratch/king-scene.png");
    return 0;
}
