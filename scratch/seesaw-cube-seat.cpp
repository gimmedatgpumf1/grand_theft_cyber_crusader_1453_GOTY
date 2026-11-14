// A square left on the seesaw's plank while the plank is free to turn. The
// question: does the square stay flat on the face and ride it, the way a real
// square on a real plank does, or does it perch, hover, or slide wrongly?
#include "common.h"
#include "client/playground_shapes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace gtc::native::playground_detail;

static void state(PlaygroundRoom& r, int k) {
    auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
    auto& c = r.bodies_[0];
    const Vec3 u{-std::sin(s.angle), std::cos(s.angle), 0};
    const Vec3 d{std::cos(s.angle), std::sin(s.angle), 0};
    const Vec3 rel = c.centre - s.centre;
    const double along = dot(rel, d);
    const double perp = dot(rel, u);
    // deepest corner of the square into the plank face, along the face's normal
    double worstInto = -1e9;
    for (std::size_t i = 0; i < c.count; ++i) {
        const double h = dot(c.position[i] - s.centre, u) - kSeesawHalfThick;
        worstInto = std::max(worstInto, -h);
    }
    std::printf("%3d plank %+7.4f w %+7.3f | cube a %+7.4f relA %+7.4f | along %+7.4f perp %+7.4f into %+9.6f | "
                "v(%+6.3f,%+6.3f) w %+7.3f | frz %d onBody %d\n",
                k, s.angle, s.spin, c.angle, c.angle - s.angle, along, perp, worstInto, c.velocity.x, c.velocity.y,
                c.spin, int(c.frozen), int(c.onBody));
}

int main(int argc, char** argv) {
    const double dt = 1.0 / 20.0;
    const double startAngle = argc > 1 ? std::atof(argv[1]) : 0.20;
    const double along0 = argc > 2 ? std::atof(argv[2]) : 0.55;
    const int ticks = argc > 3 ? std::atoi(argv[3]) : 80;
    const bool holdPlank = argc > 4 ? std::atoi(argv[4]) != 0 : false;
    PlaygroundRoom r;
    r.reset();
    for (int i = 0; i < 40; ++i) r.advance(dt);
    auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
    auto& c = r.bodies_[0];

    s.frozen = false;
    s.angle = startAngle;
    s.spin = 0.0;
    s.velocity = {};
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;

    c.frozen = false;
    c.angle = s.angle;  // flat on the face
    c.velocity = {};
    c.spin = 0.0;
    const Vec3 up{-std::sin(s.angle), std::cos(s.angle), 0};
    const Vec3 dir{std::cos(s.angle), std::sin(s.angle), 0};
    c.centre = s.centre + dir * along0 + up * (kSeesawHalfThick + kHalfSize);
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    if (holdPlank) {
        r.dragTarget_ = PlaygroundRoom::DragTarget::Body;
        r.dragBody_ = PlaygroundRoom::kSeesaw;
        s.frozen = false;
    }
    r.ball_.frozen = true;
    r.ball_.position = {0.0, -5.0, 0.0};  // out of the way

    std::printf("start angle %+.3f, square flat on the face at %+.3f m along it\n", startAngle, along0);
    state(r, 0);
    for (int k = 1; k <= ticks; ++k) {
        r.advance(dt);
        state(r, k);
    }
    return 0;
}
