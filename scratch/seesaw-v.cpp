// The ball in the V.
//
// A ball resting on the floor against the sloping face of a tipped plank is held by
// two surfaces that are not parallel, and it presses on both. It is the position the
// King keeps finding it in: the plank on its side, a shape on the raised end, and the
// ball down in the corner the two of them make. It must come to rest there, and it
// must stay outside both of them.
#include "common.h"
#include "client/playground_ball_shape.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;

double penPlank(const PlaygroundRoom& r) {
    const auto& s = r.bodies_[S];
    return r.ball_.radius - ball_shape::outlineGap(r.ball_.position, s.position.data(), s.position.data(), s.count, 1.0).gap;
}

double penBody(const PlaygroundRoom& r, std::size_t i) {
    const auto& b = r.bodies_[i];
    if (b.count == 0) return -1.0;
    return r.ball_.radius - ball_shape::outlineGap(r.ball_.position, b.position.data(), b.position.data(), b.count, 1.0).gap;
}

struct Report {
    double worstPen = 0.0;
    int flips = 0;
    double lastSpeed = 0.0;
};

Report watch(PlaygroundRoom& r, int ticks, int from = 0) {
    Report out;
    double lastVx = 0.0, lastVy = 0.0;
    for (int k = 0; k < ticks; ++k) {
        r.advance(1.0 / 20.0);
        out.worstPen = std::max(out.worstPen, penPlank(r));
        out.worstPen = std::max(out.worstPen, penBody(r, 0));
        out.worstPen = std::max(out.worstPen, penBody(r, 1));
        if (k >= from) {
            if (r.ball_.velocity.x * lastVx < 0.0) ++out.flips;
            if (r.ball_.velocity.y * lastVy < 0.0) ++out.flips;
            if (r.bodies_[S].spin * 0.0 == 0.0) { /* keep */ }
        }
        lastVx = r.ball_.velocity.x;
        lastVy = r.ball_.velocity.y;
        out.lastSpeed = length(r.ball_.velocity);
    }
    return out;
}

void scene(const char* name, double plankAngle, double ballX, double ballY, Vec3 ballV, bool cubeOnPlank) {
    PlaygroundRoom r;
    r.reset();
    for (int k = 0; k < 10; ++k) r.advance(1.0 / 20.0);
    auto& s = r.bodies_[S];
    s.frozen = false;
    s.angle = plankAngle;
    s.spin = 0.0;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    if (cubeOnPlank) {
        auto& c = r.bodies_[0];
        c.frozen = false;
        // Set it down on the raised end, just clear of the face.
        const double raised = plankAngle < 0.0 ? -1.0 : 1.0;
        const double a = plankAngle;
        const Vec3 local{raised * 0.70, raised * 0.12, 0.0};
        const Vec3 onFace{s.centre.x + local.x * std::cos(a) - local.y * std::sin(a),
                          s.centre.y + local.x * std::sin(a) + local.y * std::cos(a), 0.0};
        Vec3 up{std::sin(a) * (raised > 0 ? -1.0 : 1.0), std::cos(a), 0.0};
        if (up.y < 0.0) up = up * -1.0;
        c.centre = onFace + up * 0.355;
        c.angle = a;
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
    }
    r.ball_.position = {ballX, ballY, 0};
    r.ball_.velocity = ballV;
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    const Report early = watch(r, 100);
    const Report late = watch(r, 300, 100);
    std::printf("%-34s | ball (%+.4f,%+.4f) v(%+.4f,%+.4f) w%+.3f | plank %+.4f w%+.4f | worst pen %.6f | speed %.4f | flips %d\n",
                name, r.ball_.position.x, r.ball_.position.y, r.ball_.velocity.x, r.ball_.velocity.y,
                r.ball_.spin, r.bodies_[S].angle, r.bodies_[S].spin,
                std::max(early.worstPen, late.worstPen), late.lastSpeed, late.flips);
}

}  // namespace

int main() {
    // The plank tipped to the right (its +x end down), a ball rolled into the corner.
    scene("tip -0.885, ball set in the V", -0.885, -2.98, 0.12, {}, false);
    scene("tip -0.885, ball rolled in at -1 m/s", -0.885, -2.20, 0.12, {-1.0, 0, 0}, false);
    scene("tip -0.885, ball dropped into the V", -0.885, -2.92, 0.45, {}, false);
    scene("tip -0.60, ball set in the V", -0.60, -2.75, 0.12, {}, false);
    scene("tip +0.885, ball set in the V", +0.885, -4.72, 0.12, {}, false);
    scene("tip -0.885, cube on top, ball in V", -0.885, -2.98, 0.35, {}, true);
    scene("tip -0.60, cube on top, ball in V", -0.60, -2.80, 0.50, {}, true);
    return 0;
}
