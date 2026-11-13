// One arrangement from the scan, tick by tick: the ball, its speed in both
// directions, and the shapes it is against.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;
constexpr double kDt = 1.0 / 20.0;
}

int main(int argc, char** argv) {
    const double plank = argc > 1 ? std::atof(argv[1]) : 0.575;
    const double along = argc > 2 ? std::atof(argv[2]) : 0.85;
    const double drop = argc > 3 ? std::atof(argv[3]) : 0.00;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(kDt);
    auto& s = r.bodies_[S];
    auto& c = r.bodies_[0];
    auto& t = r.bodies_[1];
    s.angle = plank;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    c.frozen = false;
    c.centre = {s.centre.x + along * std::cos(plank), s.centre.y + along * std::sin(plank) + 0.36, 0};
    c.angle = plank;
    c.velocity = {};
    c.spin = 0.0;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    r.ball_.position = {c.centre.x + 0.50, c.centre.y - 0.10 + drop * 0.18, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    const int showFrom = argc > 4 ? std::atoi(argv[4]) : 560;
    for (int k = 0; k < 800; ++k) {
        r.advance(kDt);
        if (k < showFrom) continue;
        const auto& b = r.ball_;
        // Where it is relative to the two things that can be holding it.
        const double dCube = gtc::native::ball_shape::outlineGap(b.position, c.position.data(), c.position.data(),
                                                                c.count, 1.0).gap - b.radius;
        const double dTri = gtc::native::ball_shape::outlineGap(b.position, t.position.data(), t.position.data(),
                                                               t.count, 1.0).gap - b.radius;
        const double dPlank = gtc::native::ball_shape::outlineGap(b.position, s.position.data(), s.position.data(),
                                                                 s.count, 1.0).gap - b.radius;
        std::printf("t%-4d (%+.5f,%+.5f) v(%+.4f,%+.4f) w%+.3f | gaps cube %+.5f tri %+.5f plank %+.5f | touching%d standing%d frz%d\n",
                    k, b.position.x, b.position.y, b.velocity.x, b.velocity.y, b.spin, dCube, dTri, dPlank,
                    int(b.touching), int(b.standing), int(b.frozen));
    }
    return 0;
}
