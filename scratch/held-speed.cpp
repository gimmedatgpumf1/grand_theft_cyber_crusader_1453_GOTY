// Speed that accumulates while a body is held by geometry.
//
// A body pressed by its surroundings and not moving should feel a contact that
// answers its weight every tick. If it instead gathers speed it never spends, the
// contact is not answering -- and the room will draw that speed as air. This looks
// for monotonic growth of any body's speed across ticks in which its own place did
// not change, which is the pump as a number.
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kDt = 1.0 / 20.0;
}

int main() {
    double worstGrowth = 0.0;
    int worstTrial = -1, worstBody = -1;
    double worstSpeed = 0.0;
    for (int trial = 0; trial < 40; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 30; ++i) r.advance(kDt);
        auto& t = r.bodies_[PlaygroundRoom::kTriangle];
        auto& s = r.bodies_[PlaygroundRoom::kSeesaw];
        auto& c = r.bodies_[PlaygroundRoom::kCube];
        // A pile: the triangle turned over, the cube wedged against it, the plank
        // let down onto both of them. Exactly the kind of place a body ends up with
        // nowhere to go.
        t.angle = 2.50 + 0.02 * double(trial % 11);
        t.centre = {-2.30, 1.9, 0};
        PlaygroundRoom::placeCorners(t);
        {
            double low = 1e9;
            for (std::size_t i = 0; i < t.count; ++i) low = std::min(low, t.position[i].y);
            if (low < 0.0) t.centre.y -= low;
            PlaygroundRoom::placeCorners(t);
        }
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        t.velocity = {};
        t.spin = 0.0;
        t.frozen = false;
        c.frozen = false;
        c.angle = 0.2 * double(trial % 4);
        c.velocity = {};
        c.spin = 0.0;
        c.centre = {t.centre.x + 0.55, t.centre.y + 0.30, 0};
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        s.angle = -0.95 + 0.03 * double(trial % 7);
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        s.spin = 0.0;
        s.frozen = false;
        std::array<double, PlaygroundRoom::kBodyCount> lastSpeed{};
        std::array<int, PlaygroundRoom::kBodyCount> stillTicks{};
        for (int k = 0; k < 400; ++k) {
            std::array<Vec3, PlaygroundRoom::kBodyCount> was{};
            std::array<double, PlaygroundRoom::kBodyCount> wasAngle{};
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                was[i] = r.bodies_[i].centre;
                wasAngle[i] = r.bodies_[i].angle;
            }
            r.advance(kDt);
            for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
                const auto& b = r.bodies_[i];
                if (!b.count) continue;
                const double speed = length(b.velocity) + std::abs(b.spin);
                const bool still = length(b.centre - was[i]) < 1e-6 && std::abs(b.angle - wasAngle[i]) < 1e-6;
                if (!still) {
                    stillTicks[i] = 0;
                    lastSpeed[i] = speed;
                    continue;
                }
                ++stillTicks[i];
                const double growth = speed - lastSpeed[i];
                // Growth while standing still, sustained: that is gravity getting in
                // without a contact answering it.
                if (stillTicks[i] > 4 && growth > 1e-4 && speed > worstSpeed) {
                    worstSpeed = speed;
                    worstTrial = trial;
                    worstBody = int(i);
                }
                if (stillTicks[i] > 4) worstGrowth = std::max(worstGrowth, speed);
                lastSpeed[i] = speed;
            }
        }
    }
    std::printf("worst speed held by a body that stood still for more than four ticks: %.4f m/s\n", worstGrowth);
    std::printf("  fastest such case: %.3f m/s (trial %d body %d)\n", worstSpeed, worstTrial, worstBody);
    return 0;
}
