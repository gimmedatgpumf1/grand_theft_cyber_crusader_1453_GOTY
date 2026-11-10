// Renders the playground offscreen and writes PNGs, so the drawn picture can be
// looked at rather than only the numbers.
#include "client/playground_room.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "vulkan/vk_renderer.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace gtc::native;

static void run(PlaygroundRoom& r, int ticks) {
    for (int i = 0; i < ticks; ++i) r.advance(1.0 / 20.0);
}

static void placeBall(PlaygroundRoom& r, double x, double y) {
    const Vec3 at = r.ballPosition();
    r.setDragRay({at.x, at.y, 5}, {0, 0, -1});
    r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1});
    r.advance(1.0 / 20.0);
    r.endDrag();
}

static void park(PlaygroundRoom& r, std::size_t body, double x, double y) {
    const Vec3 middle = r.bodyMiddle(body);
    r.setDragRay({middle.x, middle.y, 5}, {0, 0, -1});
    r.beginDrag();
    r.setDragRay({x, y, 5}, {0, 0, -1});
    r.advance(1.0 / 20.0);
    r.endDrag();
}

static bool shot(PlaygroundRoom& r, const RoomFreecam& camera, const char* path) {
    ViewState view;
    view.width = 900;
    view.height = 600;
    view.gpu = true;
    NativeFrame frame;
    if (!r.render(frame, view, camera)) {
        std::fprintf(stderr, "render failed for %s\n", path);
        return false;
    }
    if (!frame.save(path)) {
        std::fprintf(stderr, "save failed for %s\n", path);
        return false;
    }
    std::printf("wrote %s\n", path);
    return true;
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "scratch";
    if (!vulkanAvailable()) {
        std::fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    setGpuRenderer(&renderVulkanScene);
    PlaygroundRoom room;
    room.reset();
    room.resetPoint({-4.60, 0.0, 0});
    run(room, 200);

    RoomFreecam camera;
    // A look at the triangle's foot and the floor beside it -- the place the ball
    // used to be drawn half inside the base.
    RoomFreecam close;
    close.reset({0.20, 0.72, 2.60}, -0.02, -0.10);
    if (!shot(room, close, (out + "/rest-foot.png").c_str())) return 1;

    // The ball run down the hypotenuse and off its foot.
    placeBall(room, -1.55, 1.30);
    int fired = 0;
    for (int tick = 0; tick < 40; ++tick) {
        run(room, 1);
        if (tick == 2) shot(room, close, (out + "/roll-1.png").c_str());
        if (tick == 6) shot(room, close, (out + "/roll-2.png").c_str());
        if (tick == 10) shot(room, close, (out + "/roll-3.png").c_str());
        if (tick == 16) shot(room, close, (out + "/roll-4.png").c_str());
        if (tick == 24) shot(room, close, (out + "/roll-5.png").c_str());
        if (tick == 34) shot(room, close, (out + "/roll-6.png").c_str());
        if (room.ballPosition().y < 0.0) ++fired;
    }
    run(room, 20);
    shot(room, close, (out + "/rest-cube.png").c_str());
    std::printf("ball ends at (%.3f, %.3f), below the floor %d times\n", room.ballPosition().x,
                room.ballPosition().y, fired);
    return 0;
}
