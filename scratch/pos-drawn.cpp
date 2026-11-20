// What the SCREEN shows, frame by frame, at the rate a screen really runs. The
// simulation's tick positions can be perfectly still while the drawing wobbles,
// because every frame draws each body interpolated between the last two ticks.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace gtc::native;
int main(int argc, char** argv) {
    const double footX = argc > 1 ? std::atof(argv[1]) : -0.25;
    const double ballX = argc > 2 ? std::atof(argv[2]) : -0.18;
    const double ballY = argc > 3 ? std::atof(argv[3]) : 0.60;
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 20; ++k) r.advance(1.0 / 20.0);
    auto& c = r.bodies_[0];
    c.centre = {-0.60, 0.35, 0}; c.angle = 0; c.velocity = {}; c.spin = 0; c.frozen = false;
    PlaygroundRoom::placeCorners(c); c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
    auto& t = r.bodies_[1];
    t.angle = 0.5 * 3.14159265358979323846; t.centre = {footX + 1.4, 0.7, 0};
    PlaygroundRoom::placeCorners(t); t.previous = t.position; t.previousCentre = t.centre; t.previousAngle = t.angle;
    r.ball_.position = {ballX, ballY, 0}; r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    // settle, then draw 300 frames at 60 Hz and watch only what is drawn
    for (int k = 0; k < 600; ++k) r.advance(1.0 / 20.0);
    Vec3 blo{1e9, 1e9, 1e9}, bhi{-1e9, -1e9, -1e9}, clo = blo, chi = bhi;
    double alo = 1e9, ahi = -1e9, wlo = 1e9, whi = -1e9;
    for (int f = 0; f < 300; ++f) {
        r.advance(1.0 / 60.0);
        r.interpolate();
        const Vec3 bp = r.ball_.shown;
        const Vec3 cp = r.bodies_[0].shown[0];
        blo.x = std::min(blo.x, bp.x); blo.y = std::min(blo.y, bp.y);
        bhi.x = std::max(bhi.x, bp.x); bhi.y = std::max(bhi.y, bp.y);
        clo.x = std::min(clo.x, cp.x); clo.y = std::min(clo.y, cp.y);
        chi.x = std::max(chi.x, cp.x); chi.y = std::max(chi.y, cp.y);
        alo = std::min(alo, r.ball_.shownAngle); ahi = std::max(ahi, r.ball_.shownAngle);
        wlo = std::min(wlo, r.bodies_[0].shown[0].x);
        whi = std::max(whi, r.bodies_[0].shown[0].x);
        if (f >= 290)
            std::printf("f%d shown ball (%.12f,%.12f) angle %+.12f | cube corner (%.12f,%.12f)\n", f, bp.x, bp.y,
                        r.ball_.shownAngle, cp.x, cp.y);
    }
    (void)wlo; (void)whi;
    std::printf("drawn range over 300 frames: ball x %.3e y %.3e angle %.3e | cube corner x %.3e y %.3e\n",
                bhi.x - blo.x, bhi.y - blo.y, ahi - alo, chi.x - clo.x, chi.y - clo.y);
    return 0;
}
