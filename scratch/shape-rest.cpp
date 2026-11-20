// A shape, alone, standing on the floor: does its place stay put?
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace gtc::native;
namespace { constexpr double kDt = 1.0 / 20.0; }
int main() {
    for (int which = 0; which < 2; ++which) {
        for (double angle : {0.0, 0.30, 0.7853981634, 1.5707963268, 2.2}) {
            for (double x : {-1.0, -0.3, 0.5, 1.2}) {
                PlaygroundRoom r; r.reset();
                for (int k = 0; k < 20; ++k) r.advance(kDt);
                // park everything else far away and out of the way
                r.ball_.position = {6.0, 0.12, 0}; r.ball_.velocity = {}; r.ball_.frozen = true;
                auto& other = r.bodies_[1 - which];
                other.centre = {-16.0, 0.7, 0}; other.velocity = {}; other.spin = 0; other.frozen = true;
                PlaygroundRoom::placeCorners(other);
                other.previous = other.position; other.previousCentre = other.centre; other.previousAngle = other.angle;
                auto& b = r.bodies_[which];
                b.centre = {x, 2.0, 0}; b.angle = angle; b.velocity = {}; b.spin = 0; b.frozen = false;
                PlaygroundRoom::placeCorners(b);
                b.previous = b.position; b.previousCentre = b.centre; b.previousAngle = b.angle;
                Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
                double amin = 1e9, amax = -1e9, vmax = 0;
                for (int k = 0; k < 400; ++k) {
                    r.advance(kDt);
                    if (k < 300) continue;
                    lo.x = std::min(lo.x, b.centre.x); lo.y = std::min(lo.y, b.centre.y);
                    hi.x = std::max(hi.x, b.centre.x); hi.y = std::max(hi.y, b.centre.y);
                    amin = std::min(amin, b.angle); amax = std::max(amax, b.angle);
                    vmax = std::max(vmax, length(b.velocity));
                }
                std::printf("body %d angle %+.3f from x %+.2f: settled (%.9f,%.9f) a%+.9f | range x %.3e y %.3e a %.3e | worst v %.3e frz%d\n",
                            which, angle, x, b.centre.x, b.centre.y, b.angle, hi.x - lo.x, hi.y - lo.y, amax - amin, vmax,
                            int(b.frozen));
            }
        }
    }
    return 0;
}
