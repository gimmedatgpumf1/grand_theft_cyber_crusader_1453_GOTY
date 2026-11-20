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
#include <vector>
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


}  // namespace

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    for (int k = 0; k < 40; ++k) room.advance(1.0 / 20.0);
    room.ball_.position = {3.10, 0.12, 0};
    room.ball_.previous = room.ball_.position;
    room.ball_.velocity = {}; room.ball_.spin = 0; room.ball_.frozen = false;
    room.resetPoint({3.10, 1.40, 0});
    room.toggleForceArrows();
    room.toggleForceNewtons();
    RoomFreecam cam;
    cam.reset({3.10, 1.15, 2.10}, 0.0, -0.10);
    shotWidth = 640; shotHeight = 420;
    std::vector<unsigned char> was;
    int differing = 0, worst = 0;
    for (int f = 0; f < 700; ++f) {
        room.advance(1.0 / 60.0);
        ViewState view;
        view.width = shotWidth; view.height = shotHeight; view.gpu = true;
        cameraFrom = &cam; labelsFrom = &room;
        NativeFrame frame;
        room.render(frame, view, cam);
        if (f > 560 && !was.empty()) {
            int diff = 0;
            for (std::size_t i = 0; i < frame.rgb.size() && i < was.size(); ++i)
                if (frame.rgb[i] != was[i]) ++diff;
            if (diff) { ++differing; worst = std::max(worst, diff); }
        }
        was = frame.rgb;
        if (f == 30 || f == 90 || f == 699) {
            char path[64];
            std::snprintf(path, sizeof path, "scratch/dotfilm-%d.png", f);
            frame.save(path);
        }
    }
    std::printf("dot (%.9f,%.9f) v(%+.3e,%+.3e) frozen %d | ball (%.9f,%.9f)\n", room.point().x, room.point().y,
                room.velocity().x, room.velocity().y, room.pointFrozen() ? 1 : 0, room.ball_.position.x,
                room.ball_.position.y);
    std::printf("last 140 frames differing from the one before: %d, worst %d pixels\n", differing, worst);
    return 0;
}
