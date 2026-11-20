// The ball let go above the triangle's hypotenuse, from a range of heights: how much
// of the impact does it get back along the face's own normal?
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>
namespace { constexpr double kDt = 1.0 / 20.0; }
int main() {
    // The hypotenuse runs from (-2.60, 0.00) to (-0.50, 2.10): its outward normal is
    // (0.7071, 0.7071), and a point on it is anywhere along that line.
    for (double up = 0.20; up <= 2.01; up += 0.45) {
        for (double along = 0.25; along <= 0.76; along += 0.25) {
            PlaygroundRoom r;
            r.reset();
            for (int k = 0; k < 20; ++k) r.advance(kDt);
            const Vec3 onFace{-2.60 + 2.10 * along, 0.00 + 2.10 * along, 0};
            const Vec3 normal{0.7071067811865476, 0.7071067811865476, 0.0};
            r.ball_.position = onFace + normal * (r.ball_.radius + up);
            r.ball_.previous = r.ball_.position;
            r.ball_.velocity = {};
            r.ball_.spin = 0.0;
            r.ball_.frozen = false;
            double closing = 0.0, leaving = 0.0;
            bool landed = false;
            for (int k = 0; k < 200; ++k) {
                const double before = -(r.ball_.velocity.x * normal.x + r.ball_.velocity.y * normal.y);
                r.advance(kDt);
                const double after = r.ball_.velocity.x * normal.x + r.ball_.velocity.y * normal.y;
                if (!landed && after > 0.0) {
                    landed = true;
                    closing = before;
                    leaving = after;
                }
            }
            std::printf("  dropped %+.2f m off the face at along %.2f: arrived at %.2f m/s along the normal, "
                        "left at %+.2f m/s (%.0f%%)%s\n",
                        up, along, closing, leaving, closing > 0 ? 100.0 * leaving / closing : 0.0,
                        landed ? "" : "   NEVER CAME OFF IT");
        }
    }
    return 0;
}
