// The King's arrangement, drawn: the cube on the triangle's slope, every arrow on.
#include "common.h"
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
int shotWidth = 720, shotHeight = 560;
const RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* labelsFrom = nullptr;
Vec3 applyTo(const std::array<double, 16>& m, const Vec3& p, bool point) {
    const double w = point ? 1.0 : 0.0;
    return {m[0]*p.x + m[4]*p.y + m[8]*p.z + m[12]*w, m[1]*p.x + m[5]*p.y + m[9]*p.z + m[13]*w,
            m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14]*w};
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
                const GpuVertex& v = batch.vertices[batch.indices[i + std::size_t(k)]];
                const Vec3 world = applyTo(batch.modelMatrix, {v.x, v.y, v.z}, true);
                const Vec3 normal = applyTo(batch.modelMatrix, {v.nx, v.ny, v.nz}, false);
                corners[std::size_t(k)] = cameraVertex(camera, world, normalized(normal), world, {v.u, v.v});
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
    view.width = shotWidth; view.height = shotHeight; view.gpu = true;
    cameraFrom = &cam; labelsFrom = &room;
    NativeFrame frame;
    if (!room.render(frame, view, cam) || !frame.save(path)) std::fprintf(stderr, "shot failed %s\n", path.c_str());
}
}
int main(int argc, char** argv) {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 20; ++k) r.advance(kDt);
    // The ball where the King holds it: straight above the triangle's apex.
    const double dx = argc > 1 ? std::atof(argv[1]) : 0.20;
    r.ball_.position = {-2.60 + dx, 2.10 + 1.60, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.toggleForceArrows();
    r.toggleForceNewtons();
    RoomFreecam cam;
    cam.reset({-1.90, 1.45, 3.35}, 0.0, -0.05);
    char name[96];
    for (int k = 0; k < 30; ++k) {
        r.advance(kDt);
        std::snprintf(name, sizeof name, "scratch/film4/frame%03d.png", k);
        shoot(r, cam, name);
        std::printf("frame %02d  ball (%+.2f,%+.2f) v (%+.2f,%+.2f)\n", k, r.ball_.position.x, r.ball_.position.y,
                    r.ball_.velocity.x, r.ball_.velocity.y);
    }
    return 0;
}
