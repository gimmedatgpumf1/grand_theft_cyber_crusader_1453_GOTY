// Does the ledger add up?
//
// Every arrow on screen is an impulse over a tick, so over one tick the arrows on a
// body must be exactly the momentum that body gained: the sum of the forces it was
// drawn with, times the tick, is its mass times the change in its speed. Nothing
// more and nothing less. That is the whole of what "no conjured forces" means, and
// it is a number that can be checked instead of argued about.
//
// A force written with the wrong sign, at the wrong place, or with the wrong
// magnitude shows up here at once -- the ledger is short or long by exactly twice
// the mistake. A body that is pinned is left out: the pin does not move its centre,
// so its linear account is not the whole story, and its own reaction is written at
// the pin.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cmath>
#include <cstdio>

namespace gtc::native { extern int gtcAudit; }
namespace {
constexpr double kDt = 1.0 / 20.0;
struct Worst {
    double miss = 0.0;
    int trial = -1, tick = -1;
    std::size_t body = 0;
    Vec3 expected{}, drawn{};
};

void check(PlaygroundRoom& r, Worst& worst, int trial, int tick) {
    for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
        const auto& body = r.bodies_[index];
        if (body.count == 0 || body.pivoted || body.frozen) continue;
        const Vec3 start = body.arrivalVelocity;  // the speed the tick found it with
        Vec3 drawn{};
        int terms = 0;
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& sample = r.forces_[i];
            if (sample.owner != index || sample.couple) continue;
            drawn = drawn + sample.force;
            ++terms;
        }
        if (terms == 0) continue;
        // A ledger that has filled up has stopped writing, and a tick that fills it is
        // a tick this check cannot read: the room caps its drawn samples at
        // kMaxForceSamples, and past that it draws nothing rather than draw wrong.
        if (r.forceCount_ >= r.forces_.size()) return;
        const Vec3 expected = (body.velocity - start) * body.mass;
        const Vec3 got = drawn * kDt;
        const double miss = length(got - expected);
        if (miss > 1e-4) {
            std::fprintf(stderr, "AUDIT trial %d tick %d body %zu expected (%+.5f,%+.5f) got (%+.5f,%+.5f) samples %zu\n",
                         trial, tick, index, expected.x, expected.y, got.x, got.y, r.forceCount_);
            for (std::size_t i = 0; i < r.forceCount_; ++i) {
                const auto& smp = r.forces_[i];
                if (smp.owner != index) continue;
                std::fprintf(stderr, "   kind %d couple %d F(%+.5f,%+.5f) at (%+.4f,%+.4f) J(%+.5f,%+.5f)\n",
                             int(smp.kind), int(smp.couple), smp.force.x, smp.force.y, smp.at.x, smp.at.y,
                             smp.force.x * kDt, smp.force.y * kDt);
            }
            std::fprintf(stderr, "   applied count %zu met %d contacts %d onBall %d wasOnBall %d\n",
                         body.appliedCount, body.met, body.contacts, int(body.onBall), int(body.wasOnBall));
            for (std::size_t i = 0; i < body.appliedCount; ++i)
                std::fprintf(stderr, "   app kind %d J(%+.5f,%+.5f) at (%+.4f,%+.4f)\n", int(body.applied[i].kind),
                             body.applied[i].impulse.x, body.applied[i].impulse.y, body.applied[i].at.x,
                             body.applied[i].at.y);
        }
        if (miss > worst.miss) worst = Worst{miss, trial, tick, index, expected, got};
    }
}
}  // namespace

int main() {
    Worst worst;
    PlaygroundRoom probe;
    probe.reset();
    double worstShare = 0.0;
    for (int trial = 0; trial < 120; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(kDt);
        constexpr double S = PlaygroundRoom::kSeesaw;
        auto& s = r.bodies_[S];
        s.angle = (double(trial) / 120.0) * 1.7 - 0.85;
        s.spin = (double((trial * 37) % 60) - 30.0) * 0.5;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        double top = -1e30;
        for (std::size_t q = 0; q < s.count; ++q) top = std::max(top, s.position[q].y);
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        c.centre = {s.centre.x - 0.8 + 1.6 * double(trial % 5) / 4.0, top + 0.35, 0};
        c.angle = double(trial % 4) * 0.7;
        c.velocity = {};
        c.spin = 0.0;
        c.frozen = false;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        t.centre = {s.centre.x + 0.9 - 1.8 * double(trial % 3) / 2.0, top + 1.1, 0};
        t.angle = 0.3 * double(trial % 5);
        t.velocity = {};
        t.spin = 0.0;
        t.frozen = false;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        r.ball_.position = {s.centre.x + 1.6, 0.12 + double(trial % 3) * 0.4, 0};
        r.ball_.velocity = {-2.5 - double(trial % 5), 0.0, 0};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        using gtc::native::gtcAudit;
        for (int k = 0; k < 200; ++k) {
            gtcAudit = (trial == 33 && k >= 60 && k <= 67) ? 1 : 0;
            if (gtcAudit) std::fprintf(stderr, "AUD ---- tick %d begin\n", k);
            r.advance(kDt);
            if (gtcAudit) std::fprintf(stderr, "AUD ---- tick %d end\n", k);
            gtcAudit = 0;
            check(r, worst, trial, k);
        }
    }
    (void)worstShare;
    if (worst.trial >= 0) {
        PlaygroundRoom q;
        q.reset();
        for (int k = 0; k < 10; ++k) q.advance(kDt);
        // Re-run the failing arrangement up to its failing tick, then take it apart.
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(kDt);
        constexpr double S = PlaygroundRoom::kSeesaw;
        const int trial = worst.trial;
        auto& s = r.bodies_[S];
        s.angle = (double(trial) / 120.0) * 1.7 - 0.85;
        s.spin = (double((trial * 37) % 60) - 30.0) * 0.5;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        double top = -1e30;
        for (std::size_t qq = 0; qq < s.count; ++qq) top = std::max(top, s.position[qq].y);
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        c.centre = {s.centre.x - 0.8 + 1.6 * double(trial % 5) / 4.0, top + 0.35, 0};
        c.angle = double(trial % 4) * 0.7;
        c.velocity = {};
        c.spin = 0.0;
        c.frozen = false;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        t.centre = {s.centre.x + 0.9 - 1.8 * double(trial % 3) / 2.0, top + 1.1, 0};
        t.angle = 0.3 * double(trial % 5);
        t.velocity = {};
        t.spin = 0.0;
        t.frozen = false;
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        t.previousAngle = t.angle;
        r.ball_.position = {s.centre.x + 1.6, 0.12 + double(trial % 3) * 0.4, 0};
        r.ball_.velocity = {-2.5 - double(trial % 5), 0.0, 0};
        r.ball_.spin = 0.0;
        r.ball_.frozen = false;
        r.ball_.previous = r.ball_.position;
        for (int k = 0; k < worst.tick; ++k) r.advance(kDt);
        {
            auto& bb = r.bodies_[worst.body];
            std::fprintf(stderr, "STAGE before   (%+.4f,%+.4f)\n", bb.velocity.x, bb.velocity.y);
            r.tickBodies();
            std::fprintf(stderr, "STAGE bodies   (%+.4f,%+.4f)\n", bb.velocity.x, bb.velocity.y);
            r.tickBall();
            std::fprintf(stderr, "STAGE ball     (%+.4f,%+.4f)\n", bb.velocity.x, bb.velocity.y);
            r.separateBall();
            std::fprintf(stderr, "STAGE separate (%+.4f,%+.4f)\n", bb.velocity.x, bb.velocity.y);
        }
        PlaygroundRoom z;
        z.reset();
        const auto& b = r.bodies_[worst.body];
        std::printf("\n  taken apart [trial %d tick %d body %zu]:\n    v start (%+.4f,%+.4f) end (%+.4f,%+.4f) mass %.2f\n",
                    worst.trial, worst.tick, worst.body, b.arrivalVelocity.x, b.arrivalVelocity.y, b.velocity.x,
                    b.velocity.y, b.mass);
        std::printf("    met %d contacts %d onBall %d wasOnBall %d hitAt %.3f frozen %d\n", b.met, b.contacts,
                    int(b.onBall), int(b.wasOnBall), b.hitAt, int(b.frozen));
        Vec3 sum{};
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& sm = r.forces_[i];
            if (sm.owner != worst.body) continue;
            if (sm.couple) { std::printf("    couple %.4f N.m at (%+.3f,%+.3f)\n", sm.moment, sm.at.x, sm.at.y); continue; }
            std::printf("    F (%+8.3f,%+8.3f) at (%+.3f,%+.3f) kind %d\n", sm.force.x, sm.force.y, sm.at.x, sm.at.y,
                        int(sm.kind));
            sum = sum + sm.force;
        }
        std::printf("    sum %.3f,%.3f N x dt = %.4f,%.4f\n", sum.x, sum.y, sum.x * kDt, sum.y * kDt);
        for (std::size_t i = 0; i < b.appliedCount; ++i)
            std::printf("    applied: J (%+8.3f,%+8.3f) at (%+.3f,%+.3f) kind %d\n", b.applied[i].impulse.x,
                        b.applied[i].impulse.y, b.applied[i].at.x, b.applied[i].at.y, int(b.applied[i].kind));
    }
    std::printf("forces drawn vs the momentum they explain, over 120 arrangements of the seesaw:\n");
    std::printf("  worst miss: %.3e N.s  [trial %d tick %d body %zu]\n", worst.miss, worst.trial, worst.tick,
                worst.body);
    std::printf("  that tick the body's arrows came to (%+.4f,%+.4f) N.s and its speed changed by "
                "(%+.4f,%+.4f) N.s\n",
                worst.drawn.x, worst.drawn.y, worst.expected.x, worst.expected.y);
    std::printf(worst.miss < 1e-6 ? "  the ledger adds up.\n" : "  the ledger does NOT add up.\n");
    return 0;
}
