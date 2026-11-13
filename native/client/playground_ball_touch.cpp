// The grip of the ball's contacts, worked out together.
//
// The ball's friction does not belong to one face at a time -- an impulse at one
// changes what is left to slip at every other -- so it is its own step, run once
// all of a tick's contacts are known. This file is that step and nothing else.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_shapes.h"

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
constexpr int kBallFrictionPasses = 12;

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
    // The load the ground is carrying, in newtons, which is what both friction and
    // rolling resistance are measured against.
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
    // Until the faces agree. Each pass is one pass over them, and the slip a face
    // sees is measured against what the passes before it have already done -- that
    // is what makes them agree instead of undoing each other.
    for (int pass = 0; pass < kBallFrictionPasses; ++pass) {
        bool clashing = false;
        for (std::size_t i = 0; i < ballTouchCount_; ++i) {
            const double slip = slipAt(i, ball.velocity, ball.spin);
            if (slip == 0.0) continue;
            const double lever = arms[i].x * tangents[i].y - arms[i].y * tangents[i].x;
            const double effective = 1.0 / ball.mass + lever * lever / inertia;
            const double wanted = -slip / effective;
            const double pressed = ballTouches_[i].pressed;
            // Static friction takes the slip away and the ball rolls from there; if
            // it cannot, the face slides and kinetic friction acts against the slide.
            const bool holds = std::abs(wanted) <= kBallStaticFriction * pressed;
            if (!holds) clashing = true;
            const double limit = (holds ? kBallStaticFriction : kBallKineticFriction) * pressed;
            const double impulse = holds ? wanted : (wanted > 0.0 ? limit : -limit);
            ball.velocity = ball.velocity + tangents[i] * (impulse / ball.mass);
            ball.spin += lever * impulse / inertia;
            taken[i] += impulse;
        }
        if (!clashing) break;
    }
    // A surface that is not moving gives nothing: a ball can never come out of a
    // friction solve against still faces with more energy than it took in. A moving
    // surface is another matter -- it is doing work, which is what being hit by a
    // moving body is -- so the check is only made against what is still.
    bool still = true;
    for (std::size_t i = 0; i < ballTouchCount_; ++i) still = still && length(ballTouches_[i].surfaceVelocity) <= 0.0;
    const double energyAfter = 0.5 * ball.mass * dot(ball.velocity, ball.velocity) + 0.5 * inertia * ball.spin * ball.spin;
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
