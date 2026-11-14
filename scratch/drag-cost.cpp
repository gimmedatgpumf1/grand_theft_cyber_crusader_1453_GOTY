// What the hand costs per frame, and whether the plank carries what is on it.
//
// A drag runs once per *frame*, not once per tick, so anything the drag path does
// costs the frame rate -- which is what "laggy" means. This times it.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <chrono>
#include <cstdio>
int main() {
    using clock = std::chrono::steady_clock;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(1.0 / 20.0);
    // Grab the cube and wave it about, the way a hand does.
    r.setDragRay({1.3914, 0.35, 3.0}, {0.0, 0.0, -1.0});
    r.beginDrag();
    std::printf("drag target: %d (0 none, 1 point, 2 body, 3 ball)\n", int(r.dragTarget_));
    const auto start = clock::now();
    for (int i = 0; i < 2000; ++i) {
        r.setDragRay({1.3914 + 1.5 * std::sin(i * 0.02), 0.35 + 1.2 * std::cos(i * 0.02), 3.0}, {0.0, 0.0, -1.0});
        r.updateDrag();
    }
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    std::printf("2000 frames of dragging a shape: %.1f ms (%.4f ms per frame)\n", ms, ms / 2000.0);
    r.endDrag();
    // And the plank with a ball on its end: does it carry it, or shrug it off?
    PlaygroundRoom q;
    q.reset();
    for (int i = 0; i < 40; ++i) q.advance(1.0 / 20.0);
    auto& s = q.bodies_[2];
    s.angle = 0.45;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    s.frozen = false;
    q.ball_.position = {s.centre.x + 0.75, s.centre.y + 0.95, 0};
    q.ball_.velocity = {};
    q.ball_.spin = 0.0;
    q.ball_.frozen = false;
    q.ball_.previous = q.ball_.position;
    std::printf("plank with a ball let go on its raised end:\n");
    for (int k = 0; k < 120; ++k) {
        q.advance(1.0 / 20.0);
        if (k % 10 == 9)
            std::printf("   t%.1fs plank %+.3f spin %+.2f | ball (%+.3f,%+.3f) v(%+.2f,%+.2f) spin %+.2f\n",
                        (k + 1) * 0.05, s.angle, s.spin, q.ball_.position.x, q.ball_.position.y, q.ball_.velocity.x,
                        q.ball_.velocity.y, q.ball_.spin);
    }
    return 0;
}
