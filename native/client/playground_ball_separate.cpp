// Putting the ball back out of what it is inside, and the one place the ball is moved
// by the room's hand rather than by its own travel.
//
// This is the ball's half of the separation the shapes get in playground_body_pair.cpp:
// a sweep cannot see a shape that has already turned into the ball, and a ball pinched
// between several surfaces at once needs an answer that names all of them, not the
// nearest one. Both are here rather than there only because a file of that size has to
// be readable; the rules are the room's, and they are written out at each one.

#include "client/playground_ball_shape.h"
#include "client/playground_pair.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {
using namespace playground_tick;
using namespace ball_shape;

Vec3 PlaygroundRoom::ballClearStep(double& left) const {
    // How deep the ball is inside anything at `at`, and which way is out of the worst
    // of them. The floor counts: the ground is a solid like any other, and the room's
    // only answer for a ball under it is that it is not under it.
    const auto deepest = [&](Vec3 at, Vec3& way) {
        double pen = ball_.radius - at.y;
        way = Vec3{0, 1, 0};
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            if (body.count == 0) continue;
            const ball_shape::OutlineGap gap =
                ball_shape::outlineGap(at, body.position.data(), body.position.data(), body.count, 1.0);
            const double through = ball_.radius - gap.gap;
            if (through > pen) {
                pen = through;
                way = gap.normal;
            }
        }
        return pen;
    };
    // The passes are a projection, not a search: each one takes the ball out of the
    // worst surface it is in at the place it has reached, and a corner of two or three
    // is clear in a pass or two. A pinch -- two surfaces with nothing clear between
    // them -- has no answer, and the passes then oscillate instead of converging; what
    // is returned is the best place they reached, and `left` says that there is still
    // an overlap there, which is the caller's cue to make a surface give way instead.
    Vec3 step{};
    Vec3 bestStep{};
    double best = 1e30;
    for (int pass = 0; pass < 8; ++pass) {
        Vec3 way{};
        const double pen = deepest(ball_.position + step, way);
        if (pen < best) {
            best = pen;
            bestStep = step;
        }
        if (!(pen > playground_tick::kQuantum)) break;
        step = step + way * pen;
    }
    left = std::max(0.0, best);
    return bestStep;
}

void PlaygroundRoom::separateBall() {
    // The floor first, before anything is measured against anything.
    //
    // A ball that is under the ground is not a state this room ever resolves, and the
    // line at the end of this function already says so -- but saying it there is
    // saying it after the separation has run, and the separation is what needs it.
    // Measured on a cube tumbling into a ball lying on the floor: the ball was pushed
    // 21 mm under the floor during the tick, the shape was then moved 17.9 mm out of
    // it, and the floor put the ball back up afterwards -- on top of the corner that
    // had just been moved *around* it, leaving the pair 6.8 mm inside each other with
    // nothing left in the tick to notice. Put where it is now, the separation is
    // measured against the ball's real place and the overlap never survives the tick.
    if (ball_.position.y < ball_.radius) ball_.position.y = ball_.radius;
    // The shapes that could not give way, to be answered together below.
    std::vector<std::size_t> stuck;
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        if (body.count == 0 || body.frozen || bodyHeld(index)) continue;
        Vec3 way{};
        Vec3 at{};
        const double gap = bodyBallGap(index, way, at);
        // A gap a fraction of a picometre negative is touching, not inside: the room's
        // resolution is a micrometre, and the arithmetic that rests a ball on a face
        // lands it that far either side of exactly one radius, every tick. Read as an
        // overlap it entered this separation on a perfectly ordinary contact, and
        // because a pinned body cannot be pushed out of anything the ball was moved by
        // hand instead -- counted as a repair, and worth exactly nothing but the count.
        if (!(gap < -playground_tick::kQuantum)) continue;
        // The shape gives way, and it gives way *first*, because it is the shape that
        // is where it should not be. Splitting the overlap by mass share -- which is
        // what this used to do -- handed three quarters of it to the ball, because
        // the ball is the lighter of the two, and 75 % of a two-centimetre overlap
        // is a ball moved fifteen millimetres in a tick by nothing at all. That is
        // the teleport; this room does not do teleports.
        //
        // So `way`, which runs out of the ball and into the body, is the direction
        // the body leaves along, and pushBodyOutOfBall does the moving -- including
        // refusing when the body has nowhere to go, because it is on the floor.
        if (pushBodyOutOfBall(index, way * -1.0, -gap)) continue;
        stuck.push_back(index);
    }
    // Nowhere to go, any of them: the ball gives the rest of it, and only what is left.
    //
    // Not one shape at a time, though. The way out of one is not the way out of the
    // room, and moving the ball along a pinned plank's own normal is exactly how a ball
    // was carried into the face of a triangle standing beside it -- out of one shape and
    // straight into another, which is not a repair. So every surface the ball is inside
    // is answered at once, by one step, and the step is the smallest one that is clear
    // of all of them.
    //
    // And when no such step exists -- a ball pinched between a pinned plank and a
    // triangle, or under a shape standing on the floor -- then something *has* to give,
    // and the something is a shape that is free to move, at the place the step reached.
    // The step is re-read after each of those, because pushing a shape out changes which
    // surfaces the ball is inside.
    if (!stuck.empty() && !ball_.frozen && dragTarget_ != DragTarget::Ball) {
        // Counted once, however many rounds it takes: the count is "the room moved the
        // ball by its place", and a repair that takes two steps is one repair.
        const Vec3 beganAt = ball_.position;
        for (int round = 0; round < 5; ++round) {
            double left = 0.0;
            const Vec3 clear = ballClearStep(left);
            if (!(left > playground_tick::kQuantum)) {
                ball_.position = ball_.position + clear;
                break;
            }
            // A pinch. The ball goes to the best place the projection reached -- which
            // is the place that leaves the least overlap there is to leave -- and then a
            // shape that is free to move gives way from where the ball now is, so that
            // the next read of the step has something new to work with. Pushing the
            // shape by the depth measured at a place the ball was never at is a push of
            // the wrong size: measured, the ball stayed the same six tenths of a
            // millimetre inside a pinned plank while a triangle beside it crept away a
            // little at a time and nothing was ever put right.
            Vec3 here = ball_.position + clear;
            here.y = std::max(here.y, ball_.radius);
            ball_.position = here;
            // The deepest surface the ball is inside here that can give way at all: a
            // pinned body, a sleeping one and one in the mouse's hand cannot, and
            // neither can the floor.
            std::size_t worst = kBodyCount;
            double deepest = playground_tick::kQuantum;
            Vec3 worstWay{};
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                const Body& body = bodies_[index];
                if (body.count == 0 || body.frozen || bodyHeld(index) || body.pivoted) continue;
                Vec3 way{}, at{};
                const double depth = -bodyBallGap(index, way, at);
                if (depth > deepest) {
                    deepest = depth;
                    worst = index;
                    worstWay = way;
                }
            }
            if (worst < kBodyCount) {
                pushBodyOutOfBall(worst, worstWay * -1.0, deepest);
                continue;
            }
            // Nothing in the room can give way: the ball is under something pinned to
            // its axle or bolted down, and the only way out left is along whatever is
            // under it -- the ball rolls out from under the shape, which is what a real
            // ball does. It is only taken to a place that is clear of *everything*.
            const auto clearAt = [&](Vec3 at) {
                if (at.y < ball_.radius - kSkin) return false;
                for (std::size_t index = 0; index < kBodyCount; ++index) {
                    const Body& body = bodies_[index];
                    if (body.count == 0) continue;
                    if (ball_shape::outlineGap(at, body.position.data(), body.position.data(), body.count, 1.0).gap <
                        ball_.radius - kSkin)
                        return false;
                }
                return true;
            };
            const double side = here.x >= ball_.position.x ? -1.0 : 1.0;
            double low = 0.0, high = 0.0;
            for (double reach = 0.0005; reach <= 0.5; reach *= 1.5) {
                if (clearAt(Vec3{here.x + side * reach, here.y, 0})) {
                    high = reach;
                    break;
                }
                low = reach;
            }
            if (high == 0.0) break;
            for (int step = 0; step < 24; ++step) {
                const double middle = 0.5 * (low + high);
                if (clearAt(Vec3{here.x + side * middle, here.y, 0}))
                    high = middle;
                else
                    low = middle;
            }
            ball_.position = Vec3{here.x + side * high, here.y, 0};
            break;
        }
        if (length(ball_.position - beganAt) > playground_tick::kQuantum) ++ballFoundThrough_;
    }
    // And the floor, for the ball as well, after everything else -- the same rule in
    // the same place as the shapes above. The ball's own contact is what rests it on
    // the ground, and normally nothing gets it under: but a shape that has nowhere to
    // go hands the ball the whole of a separation, and a ball pushed through the
    // ground by a shape squeezed beside it is still a ball under the ground. Nothing
    // is below the floor at the end of a tick, which is this line.
    if (ball_.position.y < ball_.radius) ball_.position.y = ball_.radius;
}


}  // namespace gtc::native
