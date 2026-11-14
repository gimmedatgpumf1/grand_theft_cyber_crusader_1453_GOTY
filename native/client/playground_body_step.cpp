// How much of a body's step is clear, and where it first is not.
//
// This is the room's one sweep: a body's pose moving to another pose, asked what it
// would pass through on the way. It is here rather than beside the pair work it grew
// out of because it is long and because it can be read on its own -- it moves nothing,
// changes nothing, and answers one number: the fraction of the step that is free.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;


// How much of a body's step is clear.
//
// A step is a pose moving to another pose: the middle travels a straight line and the
// outline turns through an angle, and every corner of the body follows an arc laid
// over that line. Nothing in the room is allowed to be inside anything, and the
// question here is where along that step something first is.
//
// It is answered by conservative advancement, and the word conservative is the whole
// of it: the clearance now is a distance, the fastest any point of this body can be
// closing that distance is known -- the middle's travel plus the swing of the furthest
// corner, added -- so the clearance cannot be gone before that distance divided by
// that speed has passed. The step is advanced to there and looked at again. No speed
// is too fast to see and no turn is too quick, because nothing is sampled: the walk
// never steps *past* the instant a meeting could happen, it steps exactly to the
// soonest it could.
//
// The room's other sweeps do not cover this one, which is why a shape thrown across
// the room could go through the plank: the ball has a sweep of its own, and a pinned
// body's turn has one, but a free body's step was simply taken -- where its speed said
// it went, and answered afterwards. That answer is the recovery, and the recovery is a
// kick. This is the prevention.
//
// A body whose step *begins* against something is not swept at all: it is in contact,
// and contact is the solver's business -- refusing its step would hold it up for ever
// with the speed it arrived with, which is a shape standing on nothing. The same rule
// the ball's prevention uses, for the same reason.
double PlaygroundRoom::bodyStepAllowed(std::size_t index, Vec3 fromCentre, double fromAngle, Vec3 toCentre,
                                       double toAngle, bool mayBeginInContact, Vec3* blockedBy) const {
    if (blockedBy != nullptr) *blockedBy = Vec3{0, 1, 0};
    const std::size_t self = index < kBodyCount ? index : 0;
    const Body& body = bodies_[self];
    if (body.count == 0) return 1.0;
    const Vec3 travel = toCentre - fromCentre;
    const double turn = toAngle - fromAngle;
    double furthest = 0.0;
    for (std::size_t i = 0; i < body.count; ++i) furthest = std::max(furthest, length(body.local[i]));
    if (!(length(travel) + std::abs(turn) * furthest > playground_tick::kQuantum)) return 1.0;

    std::array<Vec3, kMaxCorners> here{};
    const auto poseAt = [&](double t) {
        const Vec3 middle = fromCentre + travel * t;
        const double angle = fromAngle + turn * t;
        const double c = std::cos(angle), s = std::sin(angle);
        for (std::size_t i = 0; i < body.count; ++i)
            here[i] = Vec3{middle.x + body.local[i].x * c - body.local[i].y * s,
                           middle.y + body.local[i].x * s + body.local[i].y * c, 0.0};
    };

    // One thing standing between the outline and something else: how far there is to
    // go (`gap`, negative when already through), which way out (`normal`), and the two
    // speeds that decide how fast that distance is being eaten.
    struct Approach {
        int owner = -1;
        double gap = 0.0;
        Vec3 normal{0, 1, 0};
        Vec3 closingSpeed{0, 0, 0};
    };
    std::vector<Approach> approaches;
    approaches.reserve(4 * kMaxCorners);
    const auto survey = [&] {
        approaches.clear();
        for (std::size_t i = 0; i < body.count; ++i) {
            Approach row;
            row.owner = -1;
            row.gap = here[i].y - playground_tick::kSurfaceY;
            row.normal = Vec3{0, 1, 0};
            row.closingSpeed = pointSpeed(body, here[i]);
            approaches.push_back(row);
        }
        for (std::size_t other = 0; other < kBodyCount; ++other) {
            if (other == self || bodies_[other].count == 0) continue;
            const Body& solid = bodies_[other];
            for (std::size_t i = 0; i < body.count; ++i) {
                const OutlineGap gap =
                    ball_shape::outlineGap(here[i], solid.position.data(), solid.position.data(), solid.count, 1.0);
                Approach row;
                row.owner = int(other);
                row.gap = gap.gap;
                row.normal = gap.normal;
                row.closingSpeed = pointSpeed(body, here[i]) - pointSpeed(solid, here[i]);
                approaches.push_back(row);
            }
            for (std::size_t i = 0; i < solid.count; ++i) {
                const OutlineGap gap =
                    ball_shape::outlineGap(solid.position[i], here.data(), here.data(), body.count, 1.0);
                Approach row;
                row.owner = int(other);
                row.gap = gap.gap;
                row.normal = gap.normal;
                row.closingSpeed = pointSpeed(body, solid.position[i]) - pointSpeed(solid, solid.position[i]);
                approaches.push_back(row);
            }
        }
        {
            const ball_shape::OutlineGap gap = ball_shape::outlineGap(ball_.position, here.data(), here.data(), body.count, 1.0);
            Approach row;
            row.owner = -2;
            row.gap = gap.gap - ball_.radius;
            row.normal = gap.normal;
            row.closingSpeed = pointSpeed(body, ball_.position) - ball_.velocity;
            approaches.push_back(row);
        }
    };

    // Where the step may not take the body: through anything, deeper than a micrometre.
    // A body that begins its step already through something -- a hair through, from the
    // last tick's arithmetic -- may not get any deeper, and that is the same line: the
    // rule is about the body getting in, not about where it happened to start.
    poseAt(0.0);
    survey();
    double startGap = 1e30;
    for (const Approach& row : approaches) startGap = std::min(startGap, row.gap);
    const double limit = (mayBeginInContact && startGap < -playground_tick::kQuantum)
                             ? startGap
                             : -playground_tick::kQuantum;

    // The soonest any part of the body could come within `limit` of anything, as the
    // piece of *this step* at which that happens: how much distance is left, over how
    // fast it is closing, over how long the step lasts.
    //
    // The last division is the one that matters and it used to be missing. What is left
    // of a gap over the speed closing it is a *time* -- metres over metres per second --
    // and a step is one tick long, so that time over the tick is the fraction of the step
    // at which the two meet. Left as a time and then used as a fraction, the meeting was
    // put far too early: a square riding the plank, with more than a whole tick's
    // clearance in hand at its near corner, was told it would arrive in a fiftieth of
    // its step, was stopped there, and had the rest of its speed and turn taken off it
    // by absorbRefusedStep() -- every tick. A shape could not turn with the face it was
    // standing on, so it tipped up onto a corner and slid off the way a box on a plank
    // never does, and a shape walking along a face walked at a fifth of its speed.
    //
    // A part that is not closing at all is not going to close anything, however long
    // the step is. The face it was stopped by is kept for the caller: a step that was
    // refused is a contact, and a contact has a direction.
    // Which parts would actually be past the limit if the whole step were taken. The
    // sweep is about those and only those.
    //
    // A part that is touching a surface and travelling along it is at the limit now and
    // still at the limit at the end of the step: it is not closing on anything, and
    // asking it how long it has left to close gives a fraction of a step that never
    // arrives -- the walk below then creeps by a ten-thousandth of the step per pass and
    // caps the body at a few per cent of its travel, which is a body stopped for the
    // crime of being in contact. That is the same pump running backwards, and it is what
    // a cube riding a turning plank was doing: half its ticks stopped dead, its speed and
    // its turn taken by absorbRefusedStep(), and the face running out from under it.
    poseAt(1.0);
    survey();
    std::vector<char> crossing(approaches.size(), 0);
    bool anyCrossing = false;
    for (std::size_t i = 0; i < approaches.size(); ++i) {
        if (approaches[i].gap - limit < 0.0) {
            crossing[i] = 1;
            anyCrossing = true;
        }
    }
    // Nothing gets past the limit in this step: the step is the body's, whole.
    if (!anyCrossing) return 1.0;

    Vec3 stopping{0, 1, 0};
    double fastest = 0.0;
    bool printed = false;
    const auto soonestMeeting = [&](double t) {
        poseAt(t);
        survey();
        double soonest = 1e30;
        for (std::size_t i = 0; i < approaches.size(); ++i) {
            if (!crossing[i]) continue;
            const Approach& row = approaches[i];
            const double need = row.gap - limit;
            const double closing = -dot(row.closingSpeed, row.normal);
            if (!(need > 0.0)) {
                // Already at the depth the step may not pass, and still going in: the
                // step may not begin.
                if (!(closing > 0.0)) continue;
                if (self == 1 && std::abs(t) < 0.01 && !printed)
                    std::fprintf(stderr,
                                 "  STEPREF self %zu t %.6f owner %d rows %zu startGap %.3e limit %.3e gap %.3e n (%.2f,%.2f) closing %.4f | other frozen %d pivoted %d count %zu pos0 (%.3f,%.3f)\n",
                                 self, t, row.owner, approaches.size(), startGap, limit, row.gap, row.normal.x,
                                 row.normal.y, closing,
                                 row.owner >= 0 ? int(bodies_[std::size_t(row.owner)].frozen) : -1,
                                 row.owner >= 0 ? int(bodies_[std::size_t(row.owner)].pivoted) : -1,
                                 row.owner >= 0 ? bodies_[std::size_t(row.owner)].count : 0,
                                 row.owner >= 0 ? bodies_[std::size_t(row.owner)].position[0].x : 0.0,
                                 row.owner >= 0 ? bodies_[std::size_t(row.owner)].position[0].y : 0.0);
                printed = true;
                stopping = row.normal;
                return 0.0;
            }
            if (!(closing > 0.0)) continue;
            const double answer = need / (closing * playground_tick::kTick);
            if (answer < soonest) {
                soonest = answer;
                stopping = row.normal;
                fastest = closing;
            }
        }
        return soonest;
    };

    double t = 0.0;
    for (int pass = 0; pass < 96; ++pass) {
        const double soonest = soonestMeeting(t);
        if (!(soonest > 0.0)) {
            if (blockedBy != nullptr) *blockedBy = stopping;
            return t;
        }
        const double next = t + soonest;
        if (!(next < 1.0)) return 1.0;
        t = next;
    }
    (void)fastest;
    if (blockedBy != nullptr) *blockedBy = stopping;
    return t;
}

}  // namespace gtc::native
