#include "common.h"
#include <cstdio>
using namespace gtc::native;
int main() {
    PlaygroundRoom r; r.reset();
    for (int k = 0; k < 40; ++k) r.advance(1.0 / 20.0);
    r.ball_.position = {3.10, 0.12, 0};
    r.ball_.previous = r.ball_.position;
    r.ball_.velocity = {}; r.ball_.spin = 0; r.ball_.frozen = false;
    r.resetPoint({3.10, 1.40, 0});
    double was = 0.0;
    for (int k = 0; k < 240; ++k) {
        r.advance(1.0 / 20.0);
        const double e = 0.5 * 1.0 * dot(r.velocity(), r.velocity()) + 1.0 * 9.8 * r.point().y;
        std::printf("t%4d y %.9f v %+.6f E %.6f dE %+.6f\n", k + 1, r.point().y, r.velocity().y, e, e - was);
        was = e;
    }
    return 0;
}
