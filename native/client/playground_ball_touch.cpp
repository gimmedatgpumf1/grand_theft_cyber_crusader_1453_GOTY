// The grip of the ball's contacts, worked out together.
//
// The ball's friction does not belong to one face at a time -- an impulse at one
// changes what is left to slip at every other -- so it is its own step, run once
// all of a tick's contacts are known. This file is that step and nothing else.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
namespace {

// The ball's two coefficients are the room's: they are in playground_ball_shape.h
// beside the mass and the radius, and they were also written out here as well,
// which is two places for one number to be wrong in. This file uses the one.
using ball_shape::kBallKineticFriction;
using ball_shape::kBallStaticFriction;
// How many passes the tick's contacts get to agree with each other in. Two faces
// are settled in a handful; a corner of three is a little slower, and past this
// what is left is smaller than the arithmetic can tell from nothing.
constexpr int kBallFrictionPasses = 40;

}  // namespace

// The grip of every surface the ball is against this tick, worked out together.
//
// Friction is not a property of one contact. An impulse that stops the slip at one
// face changes the slip at every other face the ball is touching, and a ball
// wedged between two of them -- a slope above, a cube's side behind -- had its spin
// taken away by the first and handed straight back by the second, every tick, for
// as long as it stayed in the corner: the seam whirled while the ball itself never
// moved. Solved together, the same two faces agree instead: each impulse is the one
// its own grip allows once the others have had theirs, and a ball held in a corner
// has no spin left to whirl with because neither face is slipping.
//
// Each impulse is a Coulomb one: mu_s can take the slip away outright and hold, and
// anything it cannot take is mu_k * N against the slide, over the tick, never more.
// How much weight stands on a body, in newtons: its own, plus whatever the solver
// found resting on it this tick. A face of the ball with a body over it is carrying
// this, and friction is mu times it. A shape is one thing in a stack as well, so a
// cube with a triangle on it presses the ball under them both with both weights.
double PlaygroundRoom::loadOver(std::size_t body) const {
    if (body >= kBodyCount) return 0.0;
    double mass = bodies_[body].mass;
    for (std::size_t i = 0; i < kBodyCount; ++i) {
        if (i == body || bodies_[i].supportBody != body) continue;
        mass += bodies_[i].mass;
    }
    return mass * ball_shape::kGravity;
}

void PlaygroundRoom::settleBallFriction() {
    if (ballTouchCount_ == 0) return;
    Ball& ball = ball_;
    const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;  // a solid sphere
    std::array<Vec3, kMaxBallTouches> tangents{}, arms{};
    // What each face's grip actually gave the ball this tick, in N.s. The passes
    // below are one solve and the totals are what the ball felt, so this is what
    // the ledger gets -- the same rule as every other contact in the room: the
    // arrows are the impulses that were applied, and nothing else.
    std::array<double, kMaxBallTouches> taken{};
    for (std::size_t i = 0; i < ballTouchCount_; ++i) {
        tangents[i] = Vec3{ballTouches_[i].normal.y, -ballTouches_[i].normal.x, 0};
        arms[i] = ballTouches_[i].normal * -ball.radius;
    }
    // The slip each face sees, for a given speed and turn of the ball.
    const auto slipAt = [&](std::size_t i, Vec3 velocity, double spin) {
        const Vec3 turned{-spin * arms[i].y, spin * arms[i].x, 0};
        return dot(velocity + turned - ballTouches_[i].surfaceVelocity, tangents[i]);
    };
    // What each face is carrying.
    //
    // Friction is mu times the normal force, and the normal force is whatever that
    // face is holding up -- not "the ball's own weight, if the face happens to be
    // underneath". That reading is right for a ball on the floor and wrong for
    // every other arrangement, and it was worst in the pinch: a ball with a weight
    // on top of it and the floor under it, where turning is impossible because the
    // two faces grip it between them. The face above was given no load, because the
    // ball's weight does not press into it, so it was given no grip either -- and
    // the ball went on spinning under a weight it could not possibly spin under.
    //
    // So the ball is treated as what it is: one object in a stack. What bears down
    // on it is its own weight plus every face that is over it, each carrying what
    // stands on it. What holds that up is the faces underneath, and they share it in
    // proportion to how directly they are underneath. Nothing here is a rule about
    // balls: it is the statics of one plate on some supports, which is also exactly
    // what the shapes' own solver does for a stack of bodies.
    {
        const double weight = ball.mass * ball_shape::kGravity * ball_shape::kTick;  // N.s
        double carried = weight;
        double under = 0.0;
        for (std::size_t i = 0; i < ballTouchCount_; ++i) {
            const Vec3 normal = ballTouches_[i].normal;
            const double down = std::max(0.0, -normal.y);
            const double up = std::max(0.0, normal.y);
            if (down > 0.0) {
                // Over the ball: it carries what stands on it. The blow of this tick,
                // if there was one, is in that same number and is the larger of the
                // two whenever anything has just landed.
                const double above = loadOver(ballTouches_[i].body) * down * ball_shape::kTick;
                ballTouches_[i].pressed = std::max(ballTouches_[i].pressed, above);
                carried += ballTouches_[i].pressed;
            }
            under += up * up;
        }
        if (under > 0.0) {
            for (std::size_t i = 0; i < ballTouchCount_; ++i) {
                const double up = std::max(0.0, ballTouches_[i].normal.y);
                if (up <= 0.0) continue;
                ballTouches_[i].pressed = std::max(ballTouches_[i].pressed, carried * up / under);
            }
        }
    }
    // The load the ball's faces are carrying altogether, in newtons, which is what
    // both friction and rolling resistance are measured against.
    double press = 0.0;
    for (std::size_t i = 0; i < ballTouchCount_; ++i) press += ballTouches_[i].pressed;
    const double normalForce = press / ball_shape::kTick;
    const Vec3 velocityBefore = ball.velocity;
    const double spinBefore = ball.spin;
    // The rolling resistance couple, before the grip has its say.
    //
    // M = Crr * N * r, against the roll. That is the whole of it, and it comes from
    // the geometry of the thing rather than from a rule: the ball and the floor both
    // flatten a little where they meet, so the pressure under the ball is not
    // symmetric about the bottom -- it is heavier in front of it than behind -- and
    // the resultant of the ground's push therefore acts a little ahead of the
    // bottom of the ball by delta = Crr * r. A push N at an offset delta is a moment
    // N * delta = Crr * N * r about the middle of the ball, and it is a moment and
    // nothing else: the same weight, in the same place, doing the same job of
    // holding the ball up. It opposes the roll, so it is the one thing in the room
    // that takes the turn out of a ball with no sliding anywhere to rub.
    //
    // It is applied to the turn, not to the speed. What slows the *ball* down is the
    // grip: the ball is now turning slower than it is travelling, the bottom of it
    // slips forward by that much, and friction answers the slip -- which is exactly
    // what happens to a real ball. Taking the speed off by hand -- clamping the
    // travel to Crr * N * dt as this room used to -- was not rolling resistance at
    // all: it was a brake, it never touched the spin, and it was invisible in the
    // ledger because it happened to a velocity instead of to the ball.
    if (normalForce > 0.0 && ball.spin != 0.0) {
        const double couple = ball_shape::kBallRollingResistance * normalForce * ball.radius;  // N.m
        const double take = couple * ball_shape::kTick / inertia;                              // rad/s, this tick
        const double turn = std::copysign(std::min(std::abs(ball.spin), take), ball.spin);
        ball.spin -= turn;
        // Drawn as the force that couple is worth at the rolling radius, against the
        // way the ball is going, at the place it meets the ground. A couple has no
        // place of its own to be drawn at; this is the honest way to put one on a
        // picture, and the ledger knows it is a couple.
        const Vec3 at = ball.position + Vec3{0.0, -ball.radius, 0.0};
        // The way it is going: its own travel, or -- for a ball spinning on the
        // spot, where the travel is nothing -- the way that turn would carry it.
        Vec3 way = ball.velocity;
        if (!(length(way) > 1e-9)) way = Vec3{-ball.spin, 0.0, 0.0};
        if (length(way) > 1e-9) {
            const Vec3 against = way * (-1.0 / length(way));
            addCouple(at, couple, against * (couple / ball.radius), ForceKind::Rolling, kBallOwner);
        }
    }
    const double energyBefore = 0.5 * ball.mass * dot(ball.velocity, ball.velocity) + 0.5 * inertia * ball.spin * ball.spin;
    // Until the faces agree, and no further than the tick's own budget.
    //
    // Each pass measures the slip a face sees against what the passes before it
    // have already done, which is what makes them agree instead of undoing each
    // other. But a pass is not a tick: Coulomb's limit is what a face can give over
    // the whole tick, so what has already been given is carried in `lambda` and the
    // next pass may only add the difference. Adding a fresh limit every pass is not
    // a stronger grip, it is twelve grips in one tick -- and stopping after the
    // first pass that did not exceed its limit, as this used to, left the second
    // face of a pinch answered against a ball the first face had already moved and
    // never asked again: the ball came out of the solve still spinning under a load
    // that could not let it.
    // How much the *other* side of each face gives when it is pushed along the
    // tangent -- the reciprocal of a mass plus the lever's share about its own
    // middle, the same number every other contact in the room is solved with. It
    // belongs in the answer on the ball's side: the impulse is a pair, so the ball's
    // slip is closed by both of them moving, and a solve that counts only the ball
    // overshoots every time the thing it is against can move. It does not look like
    // an overshoot -- the passes converge to a pair of impulses that alternate in
    // sign, the ball ends where it started, and the shape it is against is left with
    // the last one. That alternation is momentum out of nothing, and it is what
    // squeezed the cube off the ball at a metre a second.
    std::array<double, kMaxBallTouches> partner{};
    for (std::size_t i = 0; i < ballTouchCount_; ++i) {
        const std::size_t other = ballTouches_[i].body;
        if (other >= kBodyCount) continue;
        const Body& mate = bodies_[other];
        if (mate.frozen || bodyHeld(other)) continue;
        const Vec3 arm = ball.position + arms[i] - mate.centre;
        const double lever = arm.x * tangents[i].y - arm.y * tangents[i].x;
        partner[i] = dot(tangents[i], tangents[i]) / mate.mass + lever * lever / mate.moment;
    }
    std::array<double, kMaxBallTouches> lambda{};
    for (int pass = 0; pass < kBallFrictionPasses; ++pass) {
        double worst = 0.0;
        for (std::size_t i = 0; i < ballTouchCount_; ++i) {
            const double slip = slipAt(i, ball.velocity, ball.spin);
            worst = std::max(worst, std::abs(slip));
            if (slip == 0.0) continue;
            const double lever = arms[i].x * tangents[i].y - arms[i].y * tangents[i].x;
            // And the ball's own share of it, with the same projection its translation
            // gets everywhere else in the tick: a grip along a face the ball is
            // squeezed against can only move it the way the surfaces under it allow.
            // Solved free, the grip believes a ball pinched between the floor and a
            // weight can be rubbed along a diagonal that the floor will not let it
            // take, and the part it cannot take is answered by taking more of it --
            // a tangential impulse larger than the slip it was solving for.
            const double along = dot(tangents[i], ballFree(tangents[i]));
            const double effective = along / ball.mass + lever * lever / inertia + partner[i];
            const double load = ballTouches_[i].pressed;
            // Static friction takes the slip away and the ball rolls from there; if
            // the whole of what it would take is more than the face can hold, it
            // slides, and what it holds with is the sliding coefficient and no more.
            double next = lambda[i] - slip / effective;
            if (std::abs(next) > kBallStaticFriction * load) {
                const double limit = kBallKineticFriction * load;
                next = std::clamp(next, -limit, limit);
            }
            const double impulse = next - lambda[i];
            if (impulse == 0.0) continue;
            lambda[i] = next;
            ball.velocity = ball.velocity + ballFree(tangents[i]) * (impulse / ball.mass);
            ball.spin += lever * impulse / inertia;
            taken[i] += impulse;
            // And the other side of it. Friction is a pair like every other force
            // in the room: the ball is not gripped by a surface, it is gripped by
            // the thing it is against, and that thing is given the opposite impulse
            // in the same place. A face over the ball is carrying a weight, and the
            // ball's spin pulling on that face has to pull the weight with it --
            // otherwise the spin is taken off the ball by nothing, and the room has
            // lost the momentum it was holding.
            //
            // What it is against is asked what it can take before it is given
            // anything. A shape that is asleep, or that the mouse is holding, is a
            // surface and not a body: it does not move, it does not take a share,
            // and the impulse it answers goes into what is holding *it* -- the same
            // rule the rest of the room follows for a frozen body. And a shape that
            // does take it is moving differently from that moment on, so the slip the
            // next pass measures has to be against the face as it now is, or the two
            // would be solved against a state the room has already left.
            const std::size_t other = ballTouches_[i].body;
            if (other < kBodyCount) {
                Body& mate = bodies_[other];
                const Vec3 at = ball.position + arms[i];
                if (!mate.frozen && !bodyHeld(other)) {
                    pushBody(mate, at, tangents[i] * -impulse, ForceKind::Pair);
                    ballTouches_[i].surfaceVelocity = pointSpeed(mate, at);
                } else {
                    ballTouches_[i].surfaceVelocity = Vec3{};
                }
            }
        }
        // A slip smaller than the room can resolve is not a slide: a micrometre a
        // tick is the last thing this world's arithmetic can tell from standing
        // still, and asking friction to answer less than that is asking it to
        // answer a number that is not there.
        if (worst <= playground_tick::kQuantumSpeed) break;
    }
    // A surface that is not moving gives nothing: a ball can never come out of a
    // friction solve against still faces with more energy than it took in. A moving
    // surface is another matter -- it is doing work, which is what being hit by a
    // moving body is -- so the check is only made against what is still.
    bool still = true;
    for (std::size_t i = 0; i < ballTouchCount_; ++i) still = still && length(ballTouches_[i].surfaceVelocity) <= 0.0;
    const double energyAfter = 0.5 * ball.mass * dot(ball.velocity, ball.velocity) + 0.5 * inertia * ball.spin * ball.spin;
    static int guardCount = 0;
    if (still && energyAfter > energyBefore) ++guardCount;
    if (still && energyAfter > energyBefore) {
        ball.velocity = velocityBefore;
        ball.spin = spinBefore;
        taken.fill(0.0);
    }
    // Grip, written down: a force along the face at the place the ball meets it.
    // Without this a ball rolling along the floor was drawn with a normal and
    // nothing else, and the thing that was actually slowing it down -- the friction
    // holding the bottom of it still against the ground -- was nowhere on screen.
    for (std::size_t i = 0; i < ballTouchCount_; ++i) {
        if (taken[i] == 0.0) continue;
        addForce(ball.position + arms[i], tangents[i] * (taken[i] / ball_shape::kTick), ForceKind::Friction,
                 nullptr, kBallOwner);
        // Into the ball's account for the tick as well, or the ground's share --
        // worked out at the end of the tick as what is left over -- would have this
        // grip counted twice: once where it acts and once in the remainder.
        ballBlows_ = ballBlows_ + tangents[i] * taken[i];
    }
    ballTouchCount_ = 0;
}

}  // namespace gtc::native
