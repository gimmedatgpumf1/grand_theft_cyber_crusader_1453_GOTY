// The room as the King sees it: does the PICTURE still move after the scene has been
// left alone? Rendered through the real renderer, with the arrows and the newtons on,
// and compared frame to frame -- a shape a tenth of a millimetre away is one thing,
// a picture that changes twenty times a second is what "it is still vibrating" means.
#include "common.h"
#include "client/room_freecam.h"
#include "client/renderer.h"
#include "client/ui_layer.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
using namespace gtc::native;
namespace {
constexpr double kDt = 1.0 / 20.0;
const RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* labelsFrom = nullptr;
int shotWidth = 520, shotHeight = 340;
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
void putBody(PlaygroundRoom& r, std::size_t i, Vec3 centre, double angle) {
    auto& b = r.bodies_[i];
    b.centre = centre; b.angle = angle; b.velocity = {}; b.spin = 0; b.frozen = false;
    PlaygroundRoom::placeCorners(b);
    b.previous = b.position; b.previousCentre = b.centre; b.previousAngle = b.angle;
}
void putBall(PlaygroundRoom& r, Vec3 at, Vec3 v = {}) {
    r.ball_.position = at; r.ball_.previous = at;
    r.ball_.velocity = v; r.ball_.spin = 0; r.ball_.frozen = false;
}
void film(const char* name, int settleTicks, const std::function<void(PlaygroundRoom&)>& place, Vec3 eye) {
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 40; ++k) r.advance(kDt);
    place(r);
    for (int k = 0; k < settleTicks; ++k) r.advance(kDt);
    if (std::getenv("NOARROWS") == nullptr) r.toggleForceArrows();
    if (std::getenv("NOLABELS") == nullptr) r.toggleForceNewtons();
    RoomFreecam cam;
    cam.reset(eye, 0.0, -0.10);
    cameraFrom = &cam;
    labelsFrom = &r;
    std::vector<unsigned char> was;
    int differing = 0, worst = 0;
    for (int f = 0; f < 400; ++f) {
        r.advance(kDt);
        ViewState view;
        view.width = shotWidth; view.height = shotHeight; view.gpu = true;
        NativeFrame frame;
        r.render(frame, view, cam);
        if (f > 340 && !was.empty()) {
            // The HUD's own readout is not the room: its last digits are the energy
            // total, which is allowed to change while anything is still creeping.
            const std::size_t stride = static_cast<std::size_t>(shotWidth) * 3;
            const std::size_t skip = stride * 26;
            int diff = 0;
            std::size_t first = frame.rgb.size(), last = 0;
            for (std::size_t i = skip; i < frame.rgb.size() && i < was.size(); ++i)
                if (frame.rgb[i] != was[i]) { ++diff; if (i < first) first = i; if (i > last) last = i; }
            if (diff) {
                ++differing; worst = std::max(worst, diff);
                if (diff > 50) {
                    std::printf("   frame %d: %d px, rows %zu..%zu, cols %zu..%zu\n", f, diff, first / stride,
                                last / stride, (first % stride) / 3, (last % stride) / 3);
                }
            }
        }
        was = frame.rgb;
        if (f == 399) {
            char path[96];
            std::snprintf(path, sizeof path, "scratch/filmaudit-%s.png", name);
            frame.save(path);
        }
    }
    std::printf("%-34s last 60 frames: %2d differ, worst %4d px\n", name, differing, worst);
    cameraFrom = nullptr;
    labelsFrom = nullptr;
}
}  // namespace
int main() {
    setGpuRenderer(&renderOnHost);
    film("notch", 1400, [](PlaygroundRoom& r) {
        putBody(r, 0, {-0.90, 0.35, 0}, 0.0);
        putBody(r, 1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
        putBall(r, {-0.15, 0.40, 0});
    }, {-0.15, 0.85, 2.30});
    film("foot", 1400, [](PlaygroundRoom& r) {
        putBody(r, 0, {-0.90, 0.35, 0}, 0.0);
        putBody(r, 1, {1.15, 0.7, 0}, 0.5 * 3.14159265358979323846);
        putBall(r, {0.33, 0.14, 0});
    }, {0.22, 0.45, 1.30});
    film("cubeonball", 1200, [](PlaygroundRoom& r) {
        putBall(r, {3.10, 0.12, 0}); putBody(r, 0, {3.10, 1.40, 0}, 0.0);
    }, {3.10, 0.85, 2.20});
    film("seesaw", 1200, [](PlaygroundRoom& r) { putBall(r, {-3.10, 1.40, 0}); }, {-3.10, 1.20, 2.40});
    return 0;
}
