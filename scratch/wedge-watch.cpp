// The corner in the King's picture, on the host: a ball dropped into the wedge where
// the triangle's slope, a cube's upright face and the floor all meet, watched tick by
// tick and then rendered from the room's own scene.
//
// It prints where the ball is and how fast at a few ticks, the deepest it ever got
// inside any shape, and the gap to each face at the end -- then writes two pictures. The
// depths are the room's own arithmetic (bodyBallGap over outlineGap), so this cannot
// pass by agreeing with a copy of itself.
//
// Linked by hand, as the other probes here are, against the client's objects:
//
//     g++ -pthread -std=c++20 -O2 -Iscratch -Isrc -Inative -Inative/client \
//         scratch/wedge-watch.cpp $CLIENT_COMMON ./build/obj/native-client/window_x11.o \
//         ./build/obj/native-client/opengl_shared.o -ldl -o build/bin/wedge-watch
//
// It writes scratch/host-corner.png and scratch/host-corner-close.png.

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
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
#include <cmath>
using namespace gtc::native;

namespace {
const RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* labelsFrom = nullptr;
int shotWidth = 700, shotHeight = 900;

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

int main() {
    setGpuRenderer(&renderOnHost);
    PlaygroundRoom room;
    room.reset();
    for (int i = 0; i < 300; ++i) room.advance(1.0 / 20.0);
    // The cube stands just past the foot of the slope, so the gap between its face and
    // the slope is one ball wide.
    auto& cube = room.bodies_[0];
    cube.centre = Vec3{-0.35, 0.3501, 0};
    cube.angle = 0.0;
    cube.velocity = Vec3{};
    cube.spin = 0.0;
    cube.frozen = true;
    room.placeCorners(cube);
    cube.previous = cube.position;
    room.bodies_[1].frozen = true;
    // The ball put on the slope above the corner and let go.
    room.ball_.position = Vec3{-0.62, 1.20, 0};
    room.ball_.velocity = Vec3{0, -3.0, 0};
    room.ball_.spin = 0.0;
    room.ball_.frozen = false;
    RoomFreecam cam;
    cam.reset({-0.55, 0.45, 0.62}, 0.0, 0.0);
    double worst = 0.0;
    int worstTick = -1;
    std::size_t worstBody = 99;
    for (int tick = 0; tick < 120; ++tick) {
        room.advance(1.0 / 20.0);
        for (std::size_t b = 0; b < PlaygroundRoom::kBodyCount; ++b) {
            Vec3 way{}, at{};
            const double d = -room.bodyBallGap(b, way, at);
            if (d > worst) { worst = d; worstTick = tick; worstBody = b; }
        }
        if (tick == 0 || tick == 4 || tick == 12 || tick == 30 || tick == 119)
            std::printf("tick %3d ball (%+.17g,%+.17g) v (%+.3e,%+.3e) frozen %d\n", tick, room.ball_.position.x,
                        room.ball_.position.y, room.ball_.velocity.x, room.ball_.velocity.y,
                        (int)room.ball_.frozen);
    }
    std::printf("worst inside %.3e m = 2^%.1f at tick %d in body %zu\n", worst,
                worst > 0 ? std::log2(worst) : -1000.0, worstTick, worstBody);
    Vec3 way{}, at{};
    std::printf("final gaps: cube %+.3e m, triangle %+.3e m, floor %+.17g m\n",
                room.bodyBallGap(0, way, at), room.bodyBallGap(1, way, at), room.ball_.position.y - room.ball_.radius);
    shoot(room, cam, "scratch/host-corner.png");
    cam.reset({-0.55, 0.42, 0.35}, 0.0, 0.0);
    shoot(room, cam, "scratch/host-corner-close.png");
    return 0;
}
