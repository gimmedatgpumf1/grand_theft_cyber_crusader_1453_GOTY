// The cube dropped onto a ball lying on the floor, drawn frame by frame: the cube
// is the thing in the air here, and what happens next is gravity and contact.
#include "client/playground_room.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "vulkan/vk_renderer.h"
#include <cstdio>
#include <string>
using namespace gtc::native;

static void run(PlaygroundRoom& r, int ticks) { for (int i = 0; i < ticks; ++i) r.advance(1.0 / 20.0); }
static void placeBall(PlaygroundRoom& r, double x, double y) {
    const Vec3 at = r.ballPosition();
    r.setDragRay({at.x, at.y, 5}, {0, 0, -1}); r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1}); r.advance(1.0 / 20.0); r.endDrag();
}
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

int main() {
    setGpuRenderer(&renderVulkanScene);
    const std::string out = "scratch";
    PlaygroundRoom room;
    room.reset();
    room.resetPoint({-3.90, 0.0, 0});
    run(room, 160);
    park(room, 1, -3.40, 0.90);
    placeBall(room, 0.40, 0.70);
    run(room, 20);
    park(room, 0, 0.40, 1.80);
    RoomFreecam cam;
    cam.reset({0.40, 1.00, 2.40}, 0.0, -0.12);
    shot(room, cam, out + "/drop-1.png");
    int landed = -1;
    for (int tick = 0; tick < 70; ++tick) {
        run(room, 1);
        const Vec3 b = room.ballPosition();
        const double low = room.bodySimulatedCorner(0, 0).y;
        double lowest = 1e9;
        for (std::size_t i = 0; i < room.bodyCorners(0); ++i) lowest = std::min(lowest, room.bodySimulatedCorner(0, i).y);
        (void)low;
        if (landed < 0 && lowest > 0.001 && lowest < 0.30) landed = tick;
        if (tick == 2) shot(room, cam, out + "/drop-2.png");
        if (tick == 6) shot(room, cam, out + "/drop-3.png");
        if (tick == 12) shot(room, cam, out + "/drop-4.png");
        if (tick == 30) shot(room, cam, out + "/drop-5.png");
        if (tick == 69) shot(room, cam, out + "/drop-6.png");
        if (tick % 6 == 0)
            std::printf("tick %2d ball (%6.3f,%6.3f) cube bottom %6.3f cube mid x %6.3f\n", tick, b.x, b.y,
                        lowest, room.bodyMiddle(0).x);
    }
    std::printf("cube first came to rest on the ball at tick %d\n", landed);
}
