// Does the drawn ball ever touch what it hit? A ball dropped fast is drawn at a
// series of moments inside the tick the meeting happened in; the lowest of those is
// the closest the eye ever sees it to the face. If that is above the face it bounced
// on air, whatever the physics did.
#include "common.h"
static void drop(double speed) {
    PlaygroundRoom r; r.reset();
    for (int i = 0; i < 200; ++i) r.advance(1.0 / 20.0);
    const double h = speed * speed / (2.0 * 9.8);
    r.ball_.position = {1.395, 0.35 + 0.12 + h + 0.02, 0};
    r.ball_.velocity = {};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
    r.sceneReady_ = true;
    double lowest = 1e9;
    int bentTicks = 0;
    for (int t = 1; t <= 120; ++t) {
        r.advance(1.0 / 20.0);
        if (r.ball_.bent) ++bentTicks;
        // Every frame that falls inside this tick, whatever the frame rate is.
        for (int f = 0; f <= 60; ++f) {
            r.accumulator_ = playground_tick::kTick * (double(f) / 60.0);
            r.interpolate();
            if (r.ball_.shown.y < 0.7) {
                static int shown = 0;
                if (shown++ < 12)
                    std::printf("      t%-3d f%-3d shown %.4f | prev %.4f pos %.4f | bent %d at %.4f point %.4f"
                                " in %+.3f out %+.3f\n",
                                t, f, r.ball_.shown.y, r.ball_.previous.y, r.ball_.position.y, (int)r.ball_.bent,
                                r.ball_.bendAt, r.ball_.bendPoint.y, r.ball_.bendIn.y, r.ball_.bendOut.y);
            }
            lowest = std::min(lowest, r.ball_.shown.y);
        }
    }
    const double face = 0.7;  // the cube's top
    std::printf("%4.1f m/s: the lowest the ball is ever DRAWN is y %.4f -- the face is at %.3f, so the ball's"
                " underside is %.4f m %s the cube (radius %.3f, %d bent ticks)\n",
                speed, lowest, face, std::abs(lowest - (face + 0.12)),
                lowest <= face + 0.12 + 1e-6 ? "ON OR BELOW" : "ABOVE", 0.12, bentTicks);
}
int main() {
    for (double v : {2.0, 4.0, 8.0, 12.0, 20.0}) drop(v);
    return 0;
}
