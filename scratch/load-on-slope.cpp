// The construction that would not fall over: a cube left on the triangle's slope.
// Drawn frame by frame, so what happens next is what is on the pictures.
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
#include "vulkan/vk_renderer.h"
#include <cstdio>
#include <string>
using namespace gtc::native;

static void run(PlaygroundRoom& r, int ticks) { for (int i = 0; i < ticks; ++i) r.advance(1.0 / 20.0); }
static void park(PlaygroundRoom& r, std::size_t body, double x, double y) {
    const Vec3 m = r.bodyMiddle(body);
    r.setDragRay({m.x, m.y, 5}, {0, 0, -1}); r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1}); r.advance(1.0 / 20.0); r.endDrag();
}
static void shot(PlaygroundRoom& r, const RoomFreecam& cam, const std::string& path) {
    ViewState view; view.width = 900; view.height = 600; view.gpu = true;
    NativeFrame frame;
    if (!r.render(frame, view, cam) || !frame.save(path)) std::fprintf(stderr, "shot failed %s\n", path.c_str());
    else std::printf("wrote %s\n", path.c_str());
}
// The triangle's floor arrows must carry the triangle and whatever is on it.
static void report(PlaygroundRoom& r, int tick) {
    double floorCarry = 0.0, pressed = 0.0;
    for (std::size_t i = 0; i < r.forceCount_; ++i) {
        const auto& f = r.forces_[i];
        if (f.owner != 1) continue;
        const bool pair = f.kind == PlaygroundRoom::ForceKind::Pair || f.kind == PlaygroundRoom::ForceKind::Friction;
        if (pair && f.at.y > 0.5) pressed -= f.force.y;
        if (f.kind == PlaygroundRoom::ForceKind::Contact && f.at.y < 0.05) floorCarry += f.force.y;
    }
    const Vec3 cube = r.bodyMiddle(0), tri = r.bodyMiddle(1);
    std::printf("tick %2d cube (%+.3f,%+.3f) | triangle middle (%+.3f,%+.3f) | triangle floor %7.3f N, cube pressing %6.3f N\n",
                tick, cube.x, cube.y, tri.x, tri.y, floorCarry, pressed);
}

int main() {
    const std::string out = "scratch";
    PlaygroundRoom room;
    room.reset();
    run(room, 160);
    // The cube lies along the slope, set down on it the way a hand would have to put
    // it: the exact place, so the picture is of the construction and not of a drop.
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
    RoomFreecam cam;
    cam.reset({-1.70, 1.30, 3.20}, -0.10, -0.18);
    report(room, 0);
    shot(room, cam, out + "/load-0.png");
    for (int tick = 1; tick <= 60; ++tick) {
        run(room, 1);
        if (tick <= 6 || tick % 10 == 0) report(room, tick);
        if (tick == 2) shot(room, cam, out + "/load-2.png");
        if (tick == 6) shot(room, cam, out + "/load-6.png");
        if (tick == 16) shot(room, cam, out + "/load-16.png");
        if (tick == 40) shot(room, cam, out + "/load-40.png");
        if (tick == 60) shot(room, cam, out + "/load-60.png");
    }
    std::printf("cube ends at (%+.3f,%+.3f)\n", room.bodyMiddle(0).x, room.bodyMiddle(0).y);
    return 0;
}
