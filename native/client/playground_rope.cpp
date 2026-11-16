// The rope: one constraint, on one ball.
//
// A rope is the one thing in this room that is a constraint and not a solid. Nothing
// collides with it: a ball thrown across it passes through it. What it does is hold one
// ball inside a sphere of radius L about its anchor, and it does that the way every
// other constraint in this room is done -- with a force. The rope pulls and it never
// pushes, so its tension is never negative, and wherever holding the ball on that
// sphere would need a negative tension the rope goes slack and the ball falls free
// until the rope catches it again.
//
// The ball it holds is a ball: it is ticked by the ball's own tick -- its weight, the
// air, the floor, the shapes, the other ball -- and the rope is applied on top of that
// tick and to nothing else. There is no second kind of ball here and no second kind of
// motion; this file knows a length, an anchor and a ball, and nothing else about it.
//
// On a taut rope the ball's motion is not a straight line and is not approximated by
// one: it is a circle about the anchor, and the tick asks where along that circle the
// ball ends, rather than stepping towards it. In the angle from the downward vertical,
//
//     θ'' = -(g/L) sin θ
//
// is the whole of it -- that is Newton's second law along the tangent, with the
// rope's pull always square to the motion and therefore doing no work. The tick
// solves that equation on the circle, with a rule that is its own inverse: run a tick
// forwards and then backwards and the bob is exactly where it started, at exactly the
// speed it had. That is what makes a pendulum keep time instead of slowly gaining or
// losing swing, and it is why the equation is solved for the angle on the circle
// rather than integrated as a pair of straight steps.
//
// The tension is not a separate rule: it is what the circle costs.
//
//     m L ω² = T - m g cos θ   =>   T = m (L ω² + g cos θ)
//
// which is the centripetal force the rope has to supply at that angle and that speed.
// T = 0 is the instant the rope stops pulling, and that is exactly the instant it is
// allowed to go slack.

#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#include "client/playground_room_tick.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
using namespace ball_shape;

namespace {

// How hard the midpoint solve is pushed. It is a contraction whose factor is
// dt² g / (4 L) -- a hundredth -- so this is far more than it needs; it is a bound,
// not a tolerance, and the loop stops on the answer rather than on the count.
constexpr int kMidpointPasses = 24;
// How finely the instant the rope goes slack is closed in on inside one tick. The
// same rule as the room's own sweeps: the fraction is bracketed and halved, so there
// is no step size and no sample that could be missed.
constexpr int kSlackBisections = 40;
// A tick's worth of phases. A bob can tauten, go slack and tauten again inside one
// twentieth of a second only if something is throwing it about; the count is a guard
// against a loop, and four is more than the geometry can need.
constexpr int kMaxPhases = 4;

// The tension this rope would have to supply, in newtons, for a ball at this angle
// moving at this angular rate. Zero is the rope letting go.
double tensionFor(double mass, double length, double omega, double theta, double gravity) {
    return mass * (length * omega * omega + gravity * std::cos(theta));
}

// One symmetric step: the midpoint rule, solved rather than iterated over samples.
//   θ1 = θ0 + (dt/2)(ω0 + ω1),   ω1 = ω0 - dt (g/L) sin((θ0 + θ1)/2)
// with m = (θ0 + θ1)/2 the whole thing is one equation in one unknown,
//   m = θ0 + (dt ω0)/2 - (dt² g / (4L)) sin m,
// which is a contraction for any tick this room can run and is solved by asking it
// again until it stops changing. Being the rule at its own midpoint, it is exactly its
// own inverse: a step forwards and the same step backwards return the ball to where it
// was, at the speed it had.
void midpointStep(double theta0, double omega0, double gravityOverLength, double dt, double& theta1,
                  double& omega1) {
    const double pull = dt * gravityOverLength;      // the step, in units of sqrt(g/L)
    double middle = theta0 + 0.5 * dt * omega0;
    for (int pass = 0; pass < kMidpointPasses; ++pass) {
        const double next = theta0 + 0.5 * dt * omega0 - 0.25 * dt * pull * std::sin(middle);
        if (next == middle) break;
        middle = next;
    }
    theta1 = 2.0 * middle - theta0;
    omega1 = omega0 - pull * std::sin(middle);
}

// The tick's motion: three of those steps, composed with Yoshida's coefficients.
//
// One midpoint step is only second order, and the swing shows it: let a ball go from
// half a radian and four seconds later it is seven thousandths of a radian from where
// the equation says it is, which is a swing that gains a fraction of a percent of
// period -- the very slow drift a pendulum is not allowed to have. Composing three of
// the same steps with
//
//     c = 1/(2 - 2^(1/3)),  -2^(1/3)/(2 - 2^(1/3)),  1/(2 - 2^(1/3))
//
// cancels the second-order error and leaves a fourth-order method. It is built from a
// symmetric step, so it is still exactly its own inverse, and it is still symplectic,
// so it still cannot make energy. It costs two more solves of a six-term equation; it
// is worth it, because it is the difference between a swing that keeps the pendulum's
// own time and one that very slowly does not.
void arcStep(double theta0, double omega0, double gravityOverLength, double dt, double& theta1,
             double& omega1) {
    const double cubeRootOfTwo = std::cbrt(2.0);
    const double end = 1.0 / (2.0 - cubeRootOfTwo);
    const double middle = -cubeRootOfTwo * end;
    double theta = theta0, omega = omega0;
    for (const double share : {end, middle, end}) {
        double nextTheta = theta, nextOmega = omega;
        midpointStep(theta, omega, gravityOverLength, share * dt, nextTheta, nextOmega);
        theta = nextTheta;
        omega = nextOmega;
    }
    theta1 = theta;
    omega1 = omega;
}

}  // namespace

// The rope, on a ball that has already had its tick.
//
// The ball's own tick has given it this tick's motion: its weight, the air, and every
// surface it met, solved by the same code that solves the thrown ball. What this adds
// is the one thing the ball's tick does not know about -- that there is a rope on it.
//
// The question is where the ball's travel through the tick leaves the sphere the rope
// allows, and the answer is a solve and not a sample: |p + w s - a| = L is a quadratic,
// and the root that matters is the one where the ball is going out. At that instant the
// part of its speed that is running away from the anchor belongs to the rope, and the
// rest of the tick is spent on the circle -- not on a straight line tangent to it, which
// is what "take the radial part off and carry on" would be. The circle is the motion a
// taut rope gives, and it is solved: theta'' = -(g/L) sin theta, by a rule that is its
// own inverse, so a ball left swinging keeps the pendulum's own time instead of slowly
// gaining or losing it.
//
// The tension is not a separate rule: it is what the circle costs.
//
//     m L w^2 = T - m g cos theta   =>   T = m (L w^2 + g cos theta)
//
// which is the centripetal force the rope has to supply at that angle and that speed.
// T = 0 is the instant the rope stops pulling, and that is exactly the instant it is
// allowed to go slack. A slack rope writes no force at all: it is not holding anything,
// and a rope that pushes is not a rope.
// The rope's own reading of a speed: what a contact being kept must not answer.
//
// A contact being *kept* is not a blow, and the difference shows up the moment the ball is
// held by something else. A ball hanging on a taut rope that is touching the triangle's
// slope is in equilibrium: the rope is carrying the weight and the slope is carrying
// nothing. Measured with the speed the ball would have had if the rope were not there, the
// slope is asked to answer a fall the rope has already answered -- every tick, for ever.
// An *arrival* is the other case and is not touched: what comes at a surface comes at it
// with everything it has, and that is the blow the ball is meant to lose.
Vec3 PlaygroundRoom::ballOffTheRope(const Ball& ball, Vec3 speed) const {
    if (!rope_.attached || rope_.ball >= kBallCount || &ball != &balls_[rope_.ball]) return speed;
    if (!rope_.taut) return speed;
    const Vec3 arm = ball.position - rope_.anchor;
    const double reach = length(arm);
    if (!(reach > 0.0)) return speed;
    const Vec3 outward = arm / reach;
    const double leaving = dot(speed, outward);
    return leaving > 0.0 ? speed - outward * leaving : speed;
}

void PlaygroundRoom::holdOnRope(Ball& ball) {
    // A place the rope may not put the ball: inside a shape, or through the floor.
    //
    // The rope pulls and never pushes, so a ball wedged between it and a face is held
    // by the face. Putting the ball back on its own circle anyway drove it into the
    // shape, and the room then stood it up on the face -- for nothing. Measured in the
    // pressed scene: nine tenths of a millimetre inside the slope, twelve millijoules
    // handed over at each turn of the swing, and a ball that swung for ever because the
    // height it lost falling was given back to it every time it came round.
    const auto clear = [&](Vec3 place) {
        if (place.y < playground_tick::kSurfaceY + ball.radius - ball_shape::kTouch) return false;
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            if (body.count == 0) continue;
            const double gap =
                ball_shape::outlineGap(place, body.position.data(), body.position.data(), body.count, 1.0).gap;
            if (gap < ball.radius - ball_shape::kTouch) return false;
        }
        return true;
    };
    Rope& rope = rope_;
    if (!rope.attached || &ball != &balls_[rope.ball]) return;
    const std::size_t owner = ownerOf(ball);
    const double dt = playground_tick::kTick;
    const double gravity = playground_tick::kGravity;
    const Vec3 anchor = rope.anchor;
    const double ropeLength = rope.length;
    const double pull = gravity / ropeLength;  // g/L

    // Where the tick leaves the ball, and what it is doing there. The rope has two
    // ways to find out, and the difference between them is the whole of what a rope
    // is allowed to do: it may constrain a motion the room has already decided, and it
    // may be the motion itself when nothing else acted. It may not do both at once.
    Vec3 at{};
    Vec3 speedNow{};
    bool bent = false;
    double bendAt = 0.0;
    Vec3 bendPoint{}, bendIn{}, bendOut{};

    // A place on the rope, read as the pendulum state that waits there: the angle it
    // hangs at, the speed it has along its own arc, how far out it is, and which way is
    // away from the anchor.
    const auto stateOf = [&](const Vec3& place, const Vec3& v, double& theta, double& omega, double& reach,
                             Vec3& outward) {
        const Vec3 arm = place - anchor;
        reach = length(arm);
        outward = reach > 0.0 ? arm / reach : Vec3{0, 1, 0};
        theta = std::atan2(arm.x, -arm.y);
        const Vec3 tangent{std::cos(theta), std::sin(theta), 0};
        omega = dot(v, tangent) / std::max(reach, 1e-12);
    };

    // A new impact must not be reconstructed from the pre-impact velocity: the
    // other body has already taken its impulse. Keep that answer even when the
    // ballistic walk ended outside the rope's reach. A kept contact is different:
    // if it took the ball past the rope, the arc below still supplies its motion.
    const bool pastTheLength = length(ball.position - anchor) > ropeLength;
    if (ball.answered && (ball.impacted || !pastTheLength)) {
        // The room answered this tick, and its answer is the ball's motion. All the
        // rope is allowed to do to it is what a rope does: keep it no further than its
        // length, and take the speed that is running away from the anchor -- which is
        // the speed the rope would have to supply the force to hold, and which belongs
        // to the anchor, in a pair, exactly as it does below.
        //
        // Re-deriving the motion here instead -- which is what this function used to
        // do whenever the rope was taut at the start of the tick -- threw the room's
        // answer away. A ball that hit a shape bounced, and the bounce was discarded;
        // the shape kept the blow it was given, the ball kept the speed it arrived
        // with, and the pair of them did it again on the next tick. That is a ball
        // with an infinite force to shove a triangle across the room with, and it is
        // also why the ball itself never bounced away: nothing it met ever cost it
        // anything.
        at = ball.position;
        speedNow = ball.velocity;
        const Vec3 arm = at - anchor;
        const double reach = length(arm);
        if (reach > 0.0 && reach >= ropeLength - ball_shape::kTouch) {
            const Vec3 outward = arm / reach;
            // On the circle exactly, by the distance it was out by and no more.
            if (reach > ropeLength) at = anchor + outward * ropeLength;
            const double leaving = dot(speedNow, outward);
            if (leaving > 0.0) {
                const Vec3 loss = outward * -leaving;
                speedNow = speedNow + loss;
                addForce(at, loss * (ball.mass / dt), ForceKind::Rope, nullptr, owner);
                addForce(anchor, loss * (-ball.mass / dt), ForceKind::Rope, nullptr, kAnchorOwner);
            }
        }
    } else {

        // Where the ball's travel through the tick leaves the rope's sphere, going out.
        const Vec3 travel = ball.position - ball.previous;
        const Vec3 from = ball.previous - anchor;
        double crossed = 2.0;
        // A ball that begins the tick already on the sphere -- which is every tick a ball
        // on a taut rope begins, because the rope left it there -- is held from the start
        // whenever its travel runs away from the anchor, and tangential travel counts:
        // that is the whole of a swing. Reading that off the quadratic alone is reading it
        // off rounding: the ball sits at exactly one length, and (|from|^2 - L^2) is then
        // a fraction of a picometre either side of zero, so on the ticks where the
        // arithmetic rounded it *outwards* the equation had two roots behind the ball and
        // no crossing ahead of it -- the rope read as slack, let go of a ball that was
        // still on it, and never caught it again: measured, a swing released from 0.5 rad
        // was 1.38 m outside its own rope within a tick. Touching is not inside, here as
        // everywhere else in the room: a ball on the sphere is a ball the rope has hold of.
        if (length(from) >= ropeLength - ball_shape::kTouch && dot(from, travel) >= 0.0) crossed = 0.0;
        if (!(crossed <= 1.0)) {
            const double a = dot(travel, travel);
            if (a > 0.0) {
                const double b = 2.0 * dot(from, travel);
                const double c = dot(from, from) - ropeLength * ropeLength;
                const double disc = b * b - 4.0 * a * c;
                if (disc >= 0.0) {
                    const double root = std::sqrt(disc);
                    // The stable pair of roots: q / a for the near one and c / q for the far
                    // one, so neither is lost to cancellation.
                    const double q = -0.5 * (b + (b >= 0.0 ? root : -root));
                    for (const double s : {q / a, q != 0.0 ? c / q : 2.0}) {
                        if (!(s >= 0.0 && s <= 1.0)) continue;
                        if (!(dot(from + travel * s, travel) > 0.0)) continue;  // on its way in
                        if (s < crossed) crossed = s;
                    }
                }
            }
        }
        if (crossed > 1.0) {
            // Never left the sphere: the rope held nothing this tick, and a rope that holds
            // nothing pulls with nothing.
            rope.taut = false;
            rope.tension = 0.0;
            return;
        }

        // The meeting, at the fraction of the tick it happens, and the ball's speed there --
        // read the way every other contact in this room reads it, from what the ball arrived
        // with to what it has now, at the fraction of the tick.
        at = ball.previous + travel * crossed;
        // The speed the ball has at the crossing, read the way every other contact in this
        // room reads a speed mid-tick: from what the ball's own tick started with to what it
        // ended with, at the fraction of the tick the crossing is at.
        //
        // Not the speed the tick *ended* with, which is the speed half a tick later -- and not
        // the arrival either, which is what the ball had before the other ball was allowed to
        // hit it. `began` is the one number that is both: the state this ball's own tick
        // started from.
        //
        // That is also the whole of the weight's accounting, and it has to be. The ball's own
        // tick applied this tick's weight once; the arc below applies it again, over what is
        // left of the tick after the crossing. Read at the crossing, the interpolated speed
        // already carries the weight of the part of the tick behind it, so the two together
        // are one tick of weight and no more.
        //
        // What this line used to do was read the *end* speed and add another whole tick of
        // weight to it -- `ball.velocity + {0, -g dt, 0}`, under a comment that said it was
        // taking the weight off. On a ball in free swing that merely ran the pendulum fast.
        // On a ball that a surface was carrying it was the whole of the trouble: the surface
        // had already answered that weight, so the ball was handed a downward half metre a
        // second every tick that nothing had asked for, the rope's circle turned it into
        // travel round the arc, the ball dropped back onto the slope, and the slope answered
        // it again -- half a metre a second a tick, for ever, out of nothing. That is where
        // the King's infinite bouncing came from, and this is the line it came from.
        speedNow = ball.began + (ball.velocity - ball.began) * crossed;
        // And the air, over the part of the tick that is left. The ball's own tick spent one
        // tick of it, and the crossing carries the share of that which lies behind it -- the
        // room's air is linear in the time it is given -- so this is the share in front: the
        // whole tick, once, for a ball whose tick the rope takes over. Asked for here and not
        // in the arc because a ball on a rope is in the same air as any other ball: without
        // this line the rope's own arc has no air in it at all, and a swing in this room
        // would never lose anything, which is the one thing air is for.
        speedNow = ballAirDrag(ball, speedNow, dt * (1.0 - crossed));
        double theta = 0.0, omega = 0.0, radius = 0.0;
        Vec3 outward{};
        stateOf(at, speedNow, theta, omega, radius, outward);
        // The part of the speed that is running away from the anchor belongs to the rope,
        // and it is an impulse, so it is written on both ends of the rope: at the ball where
        // it pulls, and at the anchor where it is pulled.
        Vec3 along = speedNow;
        const double leaving = dot(speedNow, outward);
        if (leaving > 0.0) {
            const Vec3 loss = outward * -leaving;
            along = along + loss;
            addForce(at, loss * (ball.mass / dt), ForceKind::Rope, nullptr, owner);
            addForce(anchor, loss * (-ball.mass / dt), ForceKind::Rope, nullptr, kAnchorOwner);
        }
        // On the circle exactly, by the distance it was out by and no more.
        at = anchor + outward * ropeLength;
        stateOf(at, along, theta, omega, radius, outward);

        double left = 1.0 - crossed;
        bool taut = true;
        bent = false;
        bendAt = crossed;
        bendPoint = at;
        bendIn = along;
        bendOut = {};
        for (int phase = 0; phase < kMaxPhases && left > 1e-12; ++phase) {
            if (taut) {
                // The tick's motion on the circle. The rope holds until the tension it has
                // to supply reaches nought; past that it is slack, and the ball flies.
                double theta1 = theta, omega1 = omega;
                arcStep(theta, omega, pull, dt * left, theta1, omega1);
                const double endTension = tensionFor(ball.mass, ropeLength, omega1, theta1, gravity);
                double taken = left;
                if (endTension <= 0.0) {
                    // The instant, inside this piece, at which it stops pulling: bracketed
                    // and halved, so there is no step size and no sample to miss.
                    double clear = 0.0, slack = left;
                    for (int pass = 0; pass < kSlackBisections; ++pass) {
                        const double middle = 0.5 * (clear + slack);
                        double th = theta, om = omega;
                        arcStep(theta, omega, pull, dt * middle, th, om);
                        if (tensionFor(ball.mass, ropeLength, om, th, gravity) > 0.0) clear = middle;
                        else slack = middle;
                    }
                    taken = clear;
                    arcStep(theta, omega, pull, dt * taken, theta1, omega1);
                }
                // The face in the way clips the arc, and it is the same solve: the ball
                // travels along the circle as far as it may and stops against what is
                // there -- it does not pass through it and get carried back out of it
                // afterwards, which is a lift out of nothing and the pump above. Where
                // the arc is cut short the ball keeps the speed it has at the cut, which
                // is less than the arc would have given it, and that loss is the face's.
                const auto placeAt = [&](double turn, Vec3& place, Vec3& speed) {
                    double th = theta, om = omega;
                    arcStep(theta, omega, pull, dt * turn, th, om);
                    place = anchor + Vec3{std::sin(th), -std::cos(th), 0} * ropeLength;
                    speed = Vec3{std::cos(th), std::sin(th), 0} * (ropeLength * om);
                };
                double turn = taken;
                Vec3 next{}, nextSpeed{};
                placeAt(turn, next, nextSpeed);
                if (!clear(next)) {
                    double free = 0.0, blocked = turn;
                    for (int pass = 0; pass < kSlackBisections; ++pass) {
                        const double middle = 0.5 * (free + blocked);
                        Vec3 probe{}, probeSpeed{};
                        placeAt(middle, probe, probeSpeed);
                        if (clear(probe)) free = middle;
                        else blocked = middle;
                    }
                    turn = free;
                    placeAt(turn, next, nextSpeed);
                    // And the rope is no longer pulling: it has nothing left to hold.
                    theta1 = theta;
                    const Vec3 arm = next - anchor;
                    const double reach = length(arm);
                    if (reach > 0.0) {
                        const Vec3 out = arm / reach;
                        const double alongIt = dot(nextSpeed, out);
                        nextSpeed = nextSpeed - out * alongIt;
                        omega1 = dot(nextSpeed, Vec3{out.y, -out.x, 0}) / ropeLength;
                    }
                }
                at = next;
                speedNow = nextSpeed;
                // The rope let go at `taken`, and what is left of the tick is a fall -- not a
                // tick thrown away. It is the same weight, applied to the ball for the time
                // that is left, which is what a ball with no rope on it does: it is the
                // arc that ends, not the tick.
                const double rest = left - taken;
                if (endTension <= 0.0 && rest > 0.0) {
                    const double fall = dt * rest;
                    at = at + nextSpeed * fall + Vec3{0.0, -0.5 * gravity * fall * fall, 0.0};
                    speedNow = nextSpeed + Vec3{0.0, -gravity * fall, 0.0};
                    bendOut = speedNow;
                    bent = true;
                }
                left = 0.0;
                break;
            }
        }

    }  // the ball's own motion, whatever it was: the rope only held it no further

    // What the rope is doing at the end of the tick, and the pull it is giving: the
    // centripetal force the circle costs, written at the ball where it pulls and at the
    // anchor where it is pulled, so the pair is on the ledger as a pair. A rope that
    // would have to push reads as slack and writes nothing.
    double theta = 0.0, omega = 0.0, radius = 0.0;
    Vec3 outward{};
    stateOf(at, speedNow, theta, omega, radius, outward);
    const double endTension = tensionFor(ball.mass, ropeLength, omega, theta, gravity);
    rope.taut = radius >= ropeLength - ball_shape::kTouch && endTension > 0.0;
    rope.tension = rope.taut ? endTension : 0.0;
    if (rope.taut) {
        const Vec3 inward = outward * -1.0;
        addForce(at, inward * rope.tension, ForceKind::Rope, nullptr, owner);
        addForce(anchor, inward * (rope.tension * -1.0), ForceKind::Rope, nullptr, kAnchorOwner);
    }
    ball.position = at;
    ball.velocity = speedNow;
    // The path the drawing is given. A tick that the rope bent is two pieces, and the
    // drawing can show both -- unless the ball's own walk already bent this tick, in
    // which case that meeting is the one the drawing follows and it is closer to the
    // truth than a second bend recorded here would be.
    if (bent && !ball.bent) {
        ball.bent = true;
        ball.bendAt = bendAt;
        ball.bendPoint = bendPoint;
        ball.bendIn = bendIn;
        ball.bendOut = bendOut;
    }
}

}  // namespace gtc::native
