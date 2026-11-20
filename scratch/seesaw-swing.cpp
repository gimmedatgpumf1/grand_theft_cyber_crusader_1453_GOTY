// The seesaw swung under a ball, drawn frame by frame, so the eye can say whether
// it is slow -- and a table of the same tick, so the numbers can say it too.
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
int shotWidth = 760, shotHeight = 520;
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
    s.velocity = {};
    s.spin = 0.0;
    room.ball_.frozen = false;
    room.ball_.position = {-3.1600, 1.2721 + 0.2400, 0};
    room.ball_.velocity = {};
    room.ball_.spin = 0.0;
    room.ball_.previous = room.ball_.position;
    std::printf("the plank under the ball, tick by tick (rad, rad/s):\n");
    RoomFreecam cam;
    cam.reset({-3.1, 1.55, 3.10}, 0.02, -0.16);
    char name[64];
    for (int k = 0; k < 130; ++k) {
        room.advance(kDt);
        if (k % 5 == 0)
            std::printf("  t%.2fs  plank %+.4f spin %+.3f f%d  ball (%+.3f,%+.3f) v(%+.2f,%+.2f) f%d\n", (k + 1) * 0.05,
                        s.angle, s.spin, int(s.frozen), room.ball_.position.x, room.ball_.position.y,
                        room.ball_.velocity.x, room.ball_.velocity.y, int(room.ball_.frozen));
        if (k == 4 || k == 9 || k == 14 || k == 19 || k == 29 || k == 49) {
            std::snprintf(name, sizeof name, "scratch/seesaw-swing-%d.png", k);
            shoot(room, cam, name);
        }
    }
    double kinetic = 0.0, potential = 0.0;
    room.energyBudget(kinetic, potential);
    std::printf("final: plank %+.4f spin %+.3f | KE %.4f PE %.3f\n", s.angle, s.spin, kinetic, potential);
    return 0;
}
