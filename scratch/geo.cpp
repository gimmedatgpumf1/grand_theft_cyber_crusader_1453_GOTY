#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
using namespace gtc::native;
int main() {
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(1.0/20);
    std::printf("AFTER RESET+20:\n");
    for (std::size_t b = 0; b < r.bodies_.size(); ++b) {
        const auto& body = r.bodies_[b];
        std::printf(" body %zu centre (%+.3f,%+.3f) a%+.4f:", b, body.centre.x, body.centre.y, body.angle);
        for (std::size_t i = 0; i < body.count; ++i) std::printf(" (%+.3f,%+.3f)", body.position[i].x, body.position[i].y);
        std::printf("\n");
    }
    // kv-sweep setup
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; r.placeCorners(c);
    auto& t = r.bodies_[1];
    t.angle = 0.5*3.14159265358979323846; t.centre = {-0.25+1.4, 0.7, 0}; r.placeCorners(t);
    std::printf("SWEEP SETUP:\n");
    for (std::size_t b = 0; b < 2; ++b) {
        const auto& body = r.bodies_[b];
        std::printf(" body %zu:", b);
        for (std::size_t i = 0; i < body.count; ++i) std::printf(" (%+.3f,%+.3f)", body.position[i].x, body.position[i].y);
        std::printf("\n");
    }
    return 0;
}
