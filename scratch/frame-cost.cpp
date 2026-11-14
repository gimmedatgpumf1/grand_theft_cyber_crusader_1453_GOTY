// What a displayed frame costs, and whether the room keeps real time at that cost.
//
// The King says the seesaw is sluggish. A tick is a millisecond, so if it still
// looks slow the time is going somewhere else: either into drawing the frame, or
// into the frame loop refusing to catch up when a frame is slow.
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
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;
int shotWidth = 1280, shotHeight = 720;
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
}  // namespace

int main() {
    PlaygroundRoom r;
    setGpuRenderer(renderOnHost);
    r.reset();
    RoomFreecam cam;
    cameraFrom = &cam;
    labelsFrom = &r;
    ViewState view;
    view.width = shotWidth;
    view.height = shotHeight;
    NativeFrame frame;
    frame.width = shotWidth;
    frame.height = shotHeight;
    frame.wantsBgra = true;
    UiLayer layer;
    frame.ui = &layer;
    // Settle, then swing the plank so there is something to watch.
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    std::printf("what a frame costs at %dx%d (software rasteriser, as the fallback draws):\n", shotWidth, shotHeight);
    double worst = 0.0, total = 0.0;
    const int frames = 60;
    const auto start = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        const auto before = std::chrono::steady_clock::now();
        r.render(frame, view, cam);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        total += ms;
        worst = std::max(worst, ms);
    }
    const double span = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  %d frames in %.1f ms: %.1f ms each, worst %.1f ms  -> %.0f fps\n", frames, span, total / frames,
                worst, 1000.0 * frames / span);
    std::printf("  the fallback draws at most %.0f frames a second here.\n", 1000.0 * frames / span);
    // And what that means for time: the loop hands the room min(0.05, elapsed), so
    // at N frames a second the room is handed N*0.05 s per second at most.
    const double fps = 1000.0 * frames / span;
    std::printf("  at %.0f fps, elapsed is %.4f s a frame but the loop passes min(0.05, elapsed) = %.4f\n", fps, 1.0 / fps,
                std::min(0.05, 1.0 / fps));
    std::printf("  room time per second of real time: min(0.05, 1/fps)*fps = %.3f s\n", std::min(0.05, 1.0 / fps) * fps);
    return 0;
}
