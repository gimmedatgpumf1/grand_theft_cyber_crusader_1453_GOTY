// Does the solved sweep agree with the truth?
//
// The turn's meeting with the floor, with another shape and with the ball is solved
// outright in pivotTurnAllowed(). Here the same question is answered the slow, dumb
// way -- by stepping along the turn in slices far finer than anything in the room and
// closing in on the first slice that is inside something -- and the two answers are
// compared. A sweep that is right agrees with the dumb answer everywhere. A sweep
// that is wrong either lets the body through something (it says yes where the truth
// says a fraction) or stops it for nothing (it says a fraction where the truth says
// the whole turn is clear).
#include "common.h"
#include "client/playground_shapes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace {
constexpr double S = PlaygroundRoom::kSeesaw;

// The same questions the sweep asks, asked of a bare angle and nothing else: how
// deep the plank is inside the floor, inside a shape, and inside the ball. "Deep" is
// per face -- how far the deepest corner of one is inside the other -- because that is
// the measure a meeting is judged by. It is written out again here rather than called
// from the room, so that the check is independent of what it checks.
struct Frame {
    Vec3 corner[PlaygroundRoom::kMaxCorners];
    std::size_t count;
};

Frame frameAt(const PlaygroundRoom& r, double angle) {
    const auto& body = r.bodies_[S];
    Frame f{};
    f.count = body.count;
    const double c = std::cos(angle), s = std::sin(angle);
    for (std::size_t i = 0; i < body.count; ++i)
        f.corner[i] = Vec3{body.pivotPoint.x + body.local[i].x * c - body.local[i].y * s,
                           body.pivotPoint.y + body.local[i].x * s + body.local[i].y * c, 0.0};
    return f;
}

// How far a point is inside a convex polygon, per face.
double pointInside(const Vec3* outline, std::size_t count, Vec3 point) {
    Vec3 middle{};
    for (std::size_t i = 0; i < count; ++i) middle = middle + outline[i];
    middle = middle * (1.0 / double(count));
    double deepest = 1e30;
    for (std::size_t i = 0; i < count; ++i) {
        const Vec3 a = outline[i];
        const Vec3 side = outline[(i + 1) % count] - a;
        const double span = length(side);
        if (!(span > 1e-12)) continue;
        Vec3 outward{side.y / span, -side.x / span, 0.0};
        if (dot(middle - a, outward) > 0.0) outward = outward * -1.0;
        deepest = std::min(deepest, -dot(point - a, outward));
    }
    return std::max(0.0, deepest);
}

double floorDepthAt(const PlaygroundRoom& r, double angle) {
    const Frame f = frameAt(r, angle);
    double deepest = 0.0;
    for (std::size_t i = 0; i < f.count; ++i) deepest = std::max(deepest, -f.corner[i].y);
    return deepest;
}

double shapeDepthAt(const PlaygroundRoom& r, double angle, std::size_t other) {
    const Frame f = frameAt(r, angle);
    const auto& solid = r.bodies_[other];
    if (solid.count == 0) return 0.0;
    double deepest = 0.0;
    for (std::size_t i = 0; i < f.count; ++i)
        deepest = std::max(deepest, pointInside(solid.position.data(), solid.count, f.corner[i]));
    for (std::size_t i = 0; i < solid.count; ++i)
        deepest = std::max(deepest, pointInside(f.corner, f.count, solid.position[i]));
    return deepest;
}

double ballDepthAt(const PlaygroundRoom& r, double angle) {
    const Frame f = frameAt(r, angle);
    double deepest = 0.0;
    for (std::size_t i = 0; i < f.count; ++i) {
        const Vec3 a = f.corner[i], b = f.corner[(i + 1) % f.count];
        const Vec3 side = b - a;
        const double span = length(side);
        Vec3 closest = a;
        if (span > 1e-12) {
            const double t = std::clamp(dot(r.ball_.position - a, side) / (span * span), 0.0, 1.0);
            closest = a + side * t;
        }
        deepest = std::max(deepest, r.ball_.radius - length(r.ball_.position - closest));
    }
    return deepest;
}

// The truth: the first fraction of `turn` at which any meeting is deeper than it was
// when the turn began, found by slicing the turn far finer than anything in the room
// and closing in on the first slice that is deeper.
double truth(const PlaygroundRoom& r, double turn) {
    const int slices = 40000;
    const double a0 = r.bodies_[S].angle;
    const double startFloor = floorDepthAt(r, a0);
    double startShape[PlaygroundRoom::kBodyCount] = {};
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i)
        if (i != std::size_t(S)) startShape[i] = shapeDepthAt(r, a0, i);
    const double startBall = ballDepthAt(r, a0);
    const auto free = [&](double angle) {
        if (floorDepthAt(r, angle) > startFloor + 1e-12) return false;
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i)
            if (i != std::size_t(S) && shapeDepthAt(r, angle, i) > startShape[i] + 1e-12) return false;
        return !(ballDepthAt(r, angle) > startBall + 1e-12);
    };
    double clear = 0.0;
    for (int i = 1; i <= slices; ++i) {
        const double fraction = double(i) / double(slices);
        if (free(a0 + turn * fraction)) {
            clear = fraction;
            continue;
        }
        double low = clear, high = fraction;
        for (int b = 0; b < 60; ++b) {
            const double mid = 0.5 * (low + high);
            if (free(a0 + turn * mid))
                low = mid;
            else
                high = mid;
        }
        return low;
    }
    return 1.0;
}
}  // namespace

int main() {
    int bad = 0, total = 0, illegal = 0;
    double worstMiss = 0.0;
    for (int trial = 0; trial < 120; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int i = 0; i < 20; ++i) r.advance(1.0 / 20.0);
        auto& s = r.bodies_[S];
        auto& c = r.bodies_[0];
        auto& t = r.bodies_[1];
        // Something in the way, in a legal place.
        s.angle = -0.8 + 1.6 * double(trial % 11) / 10.0;
        PlaygroundRoom::placeCorners(s);
        s.previous = s.position;
        s.previousCentre = s.centre;
        s.previousAngle = s.angle;
        c.frozen = false;
        c.centre = {s.centre.x - 0.9 + 1.8 * double((trial * 7) % 11) / 10.0, 1.0 + 0.2 * double(trial % 4), 0};
        c.angle = 0.3 * double(trial % 5);
        c.velocity = {};
        c.spin = 0.0;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position;
        c.previousCentre = c.centre;
        c.previousAngle = c.angle;
        t.frozen = false;
        t.centre = {-2.0 - 0.3 * double(trial % 3), 0.7, 0};
        PlaygroundRoom::placeCorners(t);
        t.previous = t.position;
        t.previousCentre = t.centre;
        for (int k = 0; k < 60; ++k) r.advance(1.0 / 20.0);
        // The sweep is a question about a legal pose: a plank already inside a shape
        // is a pose the room does not put itself in, and asking what it may turn to
        // from there is asking about a state that has no answer. The probe forces the
        // plank's angle while the cube sits where it was, so it can make one; those
        // trials are counted and set aside rather than answered wrongly.
        double deepestStart = floorDepthAt(r, s.angle);
        for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i)
            if (i != std::size_t(S)) deepestStart = std::max(deepestStart, shapeDepthAt(r, s.angle, i));
        deepestStart = std::max(deepestStart, ballDepthAt(r, s.angle));
        if (deepestStart > 1e-6) {
            ++illegal;
            continue;
        }
        // Now ask, from wherever it has got to, about a handful of turns.
        for (int which = 0; which < 6; ++which) {
            const double turn = (which % 2 == 0 ? 1.0 : -1.0) * (0.02 + 0.12 * double(which));
            const double solved = r.pivotTurnAllowed(s, turn);
            const double slow = truth(r, turn);
            ++total;
            const double miss = std::abs(solved - slow) * std::abs(turn);
            // In radians of plank: the sweep may not be out by more than the room's
            // resolution at the tip.
            if (miss > 1e-6) {
                ++bad;
                if (miss > worstMiss) worstMiss = miss;
                if (bad <= 12)
                    std::printf("MISS trial%d turn %+.3f: solved %.9f truth %.9f (out by %.3g rad)\n", trial, turn,
                                solved, slow, miss);
            }
        }
    }
    std::printf("%d of %d turns disagreed, worst by %.3g rad | %d trials began already inside something\n", bad,
                total, worstMiss, illegal);
    return 0;
}
