// What the screen actually shows, frame by frame, at the frame rates a real
// machine runs at -- which is not the same thing as the tick.
//
// The tick is 20 Hz and the screen is 60, 144 or 200. What a frame draws is the
// tick's motion spread over the frames inside it (interpolate()). If that spread
// is right, every frame's drawn angle differs from the last by about the same
// amount. If it is not, frames alternate between not moving and jumping -- which
// is exactly "stop-move-stop-move", and no table of the tick can ever show it.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include "client/renderer.h"
#include "client/room_freecam.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr double kTick = 1.0 / 20.0;
// The draw path only needs a camera and a renderer that accepts the scene.
bool acceptScene(const GpuScene&, NativeFrame&) { return true; }
}  // namespace

int main() {
    setGpuRenderer(acceptScene);
    for (const double fps : {60.0, 144.0}) {
        PlaygroundRoom room;
        room.reset();
        const double dt = 1.0 / fps;
        for (int i = 0; i < 40; ++i) room.advance(kTick);
        auto& s = room.bodies_[PlaygroundRoom::kSeesaw];
        s.frozen = false;
        s.angle = 0.40;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.spin = 0.0;
        s.velocity = {};
        room.ball_.frozen = false;
        room.ball_.position = {-3.1600, 1.5121, 0};
        room.ball_.velocity = {};
        room.ball_.spin = 0.0;
        room.ball_.previous = room.ball_.position;
        RoomFreecam cam;
        ViewState view;
        view.width = 320;
        view.height = 240;
        NativeFrame frame;
        // 1.4 s: the whole of the downward swing.
        std::vector<double> drawn;
        std::vector<int> froze;
        const int frames = int(1.4 * fps);
        ViewState vs;
        vs.width = 320;
        vs.height = 240;
        NativeFrame nf;
        RoomFreecam camera;
        camera.reset({-3.1, 1.55, 3.10}, 0.02, -0.16);
        for (int f = 0; f < frames; ++f) {
            room.advance(dt);
            // The real path: render() is what calls interpolate() and what sets
            // sceneReady_, so a probe that called interpolate() by hand measured a
            // draw path that never existed.
            room.render(nf, vs, camera);
            // The drawn angle, read off the drawn corners: shown[1]-shown[0] is the
            // local edge turned by exactly the angle the frame was drawn at.
            const Vec3 e0 = s.local[1] - s.local[0];
            const Vec3 e1 = s.shown[1] - s.shown[0];
            drawn.push_back(std::atan2(e1.y, e1.x) - std::atan2(e0.y, e0.x));
            froze.push_back(s.frozen ? 1 : 0);
        }
        std::vector<double> step;
        for (std::size_t i = 1; i < drawn.size(); ++i) step.push_back(drawn[i] - drawn[i - 1]);
        std::vector<double> sorted = step;
        std::sort(sorted.begin(), sorted.end(), [](double a, double b) { return std::abs(a) < std::abs(b); });
        const double median = std::abs(sorted[sorted.size() / 2]);
        int still = 0, jumps = 0;
        for (double d : step) {
            if (std::abs(d) < median * 0.25) ++still;
            if (std::abs(d) > median * 2.5) ++jumps;
        }
        int frozenFrames = 0;
        for (int f : froze) frozenFrames += f;
        std::printf("%.0f fps: %zu frames, median move %.6f rad | frames that barely moved: %d | frames that jumped: %d | frozen frames: %d\n",
                    fps, step.size(), median, still, jumps, frozenFrames);
        std::printf("   first 24 frame steps:");
        for (int i = 0; i < 24 && i < int(step.size()); ++i) std::printf(" %+.4f", step[std::size_t(i)]);
        std::printf("\n");
        // What fraction of a tick's turn each frame should carry, and what it did.
        std::printf("   fraction of a tick per frame: %.3f\n", kTick * fps);
    }
    return 0;
}
