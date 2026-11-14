// What a tick costs in the three states that matter: everything settled, the
// seesaw swinging under a ball, and a shape being dragged.
#include "common.h"
#include "client/playground_shapes.h"
#include <chrono>
#include <cstdio>
int main() {
    const double dt = 1.0 / 20.0;
    const auto cost = [&](const char* what, auto&& setup, auto&& perTick, int ticks) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 100; ++i) r.advance(dt);
        setup(r);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < ticks; ++i) {
            perTick(r, i);
            r.advance(dt);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  %-34s %7.3f ms/tick  (%.0f ticks a second)\n", what, ms / ticks, 1000.0 * ticks / ms);
    };
    std::printf("tick cost, 400 ticks each:\n");
    cost("everything settled", [](PlaygroundRoom&) {}, [](PlaygroundRoom&, int) {}, 400);
    cost("seesaw swinging under a ball", [](PlaygroundRoom& r) {
        auto& s = r.bodies_[2];
        s.frozen = false;
        s.angle = 0.40;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position; s.previousCentre = s.centre; s.previousAngle = s.angle;
        s.spin = 0.0; s.velocity = {};
        r.ball_.frozen = false;
        r.ball_.position = {-3.1600, 1.5121, 0};
        r.ball_.velocity = {}; r.ball_.spin = 0.0;
        r.ball_.previous = r.ball_.position;
    }, [](PlaygroundRoom&, int) {}, 400);
    cost("a shape carried by the hand", [](PlaygroundRoom& r) {
        r.setDragRay({1.3914, 0.35, 3.0}, {0.0, 0.0, -1.0});
        r.beginDrag();
    }, [](PlaygroundRoom& r, int i) {
        r.setDragRay({1.3914 + 1.2 * std::sin(i * 0.03), 0.35 + 1.0 * std::cos(i * 0.03), 3.0}, {0.0, 0.0, -1.0});
    }, 400);
    return 0;
}
