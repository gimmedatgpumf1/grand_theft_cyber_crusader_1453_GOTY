// The rope, and the ball hanging on it.
//
// A rope is the one thing in this room that is a constraint and not a solid. Nothing
// collides with it: a ball thrown across it passes through it. What it does is hold
// the bob inside a sphere of radius L about the anchor, and it does that the way
// every other constraint in this room is done -- with a force. The rope pulls and it
// never pushes, so its tension is never negative, and wherever holding the bob on
// that sphere would need a negative tension the rope goes slack and the bob falls
// free until it catches it again.
//
// On a taut rope the bob's motion is not a straight line and is not approximated by
// one: it is a circle about the anchor, and the tick asks where along that circle the
// bob ends, rather than stepping towards it. In the angle from the downward vertical,
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

// The tension this rope would have to supply, in newtons, for a bob at this angle
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
// own inverse: a step forwards and the same step backwards return the bob to where it
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
// One midpoint step is only second order, and the swing shows it: let a bob go from
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

// Where, along what is left of the ball's walk, the ball's circle and the bob's first
// meet. The bob is stepped before the ball, so its whole travel through this tick is
// known, and the sweep is between two moving circles: the ball at its velocity through
// the walk and the bob along its own step. Solved outright -- a quadratic in how far
// along the walk the two centres come within the sum of their radii -- so neither can be
// overtaken by the other, at any speed either is going.
bool PlaygroundRoom::bobFirstTouch(Vec3 from, Vec3 travel, double remaining, double& along, Vec3& normal,
                                   Vec3& contact, bool& resting) const {
    const Pendulum& bob = pendulum_;
    const Vec3 bobWas = bob.previous + (bob.position - bob.previous) * (1.0 - remaining);
    const Vec3 bobStep = bob.position - bobWas;
    const Vec3 arm = from - bobWas;
    const Vec3 closing = travel - bobStep;
    const double reach = ball_.radius + bob.radius;
    const double c = dot(arm, arm) - reach * reach;
    double metAt = 2.0;
    if (c <= 0.0) {
        metAt = 0.0;  // already against it: a contact, not an arrival
    } else {
        const double a = dot(closing, closing);
        if (a > 0.0) {
            const double b = 2.0 * dot(arm, closing);
            const double disc = b * b - 4.0 * a * c;
            if (disc >= 0.0) {
                const double root = std::sqrt(disc);
                // The stable pair of roots: c / q for the far one, q / a for the near
                // one, so neither is lost to cancellation.
                const double q = -0.5 * (b + (b >= 0.0 ? root : -root));
                const double nearRoot = q / a;
                const double farRoot = q != 0.0 ? c / q : 2.0;
                if (nearRoot >= 0.0 && nearRoot <= 1.0) metAt = nearRoot;
                if (farRoot >= 0.0 && farRoot <= 1.0) metAt = std::min(metAt, farRoot);
            }
        }
    }
    if (metAt > 1.0) return false;
    const Vec3 where = arm + closing * metAt;
    const double distance = length(where);
    along = metAt;
    normal = distance > 0.0 ? where / distance : Vec3{0, 1, 0};
    contact = from + travel * metAt - normal * ball_.radius;
    resting = c <= 0.0;
    return true;
}

void PlaygroundRoom::tickPendulum() {
    Pendulum& bob = pendulum_;
    bob.previous = bob.position;
    bob.bent = false;

    const double dt = playground_tick::kTick;
    const double gravity = playground_tick::kGravity;
    const double pull = gravity / bob.length;  // g/L

    // The forces on the bob this tick, written down where they act: its weight, and
    // the air. The weight is the force; the tick's motion below is what gravity does
    // with it, and it is done exactly on the circle rather than by one impulse's
    // worth of fall.
    addForce(bob.position, Vec3{0.0, -bob.mass * gravity, 0.0}, ForceKind::Gravity, nullptr, kBobOwner);
    // The air, over the tick, solved the way the ball's own drag is solved: the exact
    // answer of dv/dt = -(c/m) v², which never crosses nought and never invents speed.
    const double speed = length(bob.velocity);
    const double area = kPi * bob.radius * bob.radius;
    const double drag = 0.5 * kAirDensity * kBallDragCoefficient * area;
    if (speed > 0.0) {
        addForce(bob.position, bob.velocity * (-(drag * speed)), ForceKind::Drag, nullptr, kBobOwner);
        const double after = speed / (1.0 + (drag / bob.mass) * speed * dt);
        bob.velocity = bob.velocity * (after / speed);
    }

    Vec3 at = bob.position;
    Vec3 speedNow = bob.velocity;
    bool taut = false;
    double bendAt = 0.0;
    bool bent = false;
    Vec3 bendPoint{}, bendIn{}, bendOut{};

    // Where the bob is on the rope, as an angle from the downward vertical, and how
    // fast it is going round it.
    const auto stateOf = [&](const Vec3& place, const Vec3& v, double& theta, double& omega,
                             double& radius, Vec3& outward) {
        const Vec3 arm = place - bob.anchor;
        radius = length(arm);
        outward = radius > 0.0 ? arm / radius : Vec3{0, 1, 0};
        theta = std::atan2(arm.x, -arm.y);
        const Vec3 tangent{std::cos(theta), std::sin(theta), 0};
        omega = dot(v, tangent) / std::max(radius, 1e-12);
    };

    double theta = 0.0, omega = 0.0, radius = 0.0;
    Vec3 outward{};
    stateOf(at, speedNow, theta, omega, radius, outward);
    // Taut is not a flag that is set: it is the answer to whether the circle can be
    // held with a rope that only pulls. A bob that is at its length and leaning on the
    // rope is on it; one that is inside is hanging on nothing.
    taut = radius >= bob.length - kTouch && tensionFor(bob.mass, bob.length, omega, theta, gravity) > 0.0;

    double left = 1.0;
    for (int phase = 0; phase < kMaxPhases && left > 1e-12; ++phase) {
        if (taut) {
            // The rope's own pull, once: the bob is put exactly on the circle, and the
            // part of its speed that is trying to leave the circle is taken away. That
            // is the whole of what a taut rope does to a bob, and it is an impulse, so
            // it is written on both ends of the rope.
            const Vec3 inward = outward * -1.0;
            Vec3 pulled = at + inward * (radius - bob.length);
            const double leaving = dot(speedNow, outward);
            if (leaving > 0.0) {
                const Vec3 loss = outward * -leaving;
                speedNow = speedNow + loss;
                addForce(pulled, loss * (bob.mass / dt), ForceKind::Rope, nullptr, kBobOwner);
                addForce(bob.anchor, loss * (bob.mass / dt) * -1.0, ForceKind::Rope, nullptr, kAnchorOwner);
            }
            at = pulled;
            radius = bob.length;
            stateOf(at, speedNow, theta, omega, radius, outward);
            // Now the tick's motion on the circle. The rope holds until the tension it
            // has to supply reaches nought; past that it is slack, and the bob flies.
            double theta1 = theta, omega1 = omega;
            arcStep(theta, omega, pull, dt * left, theta1, omega1);
            const double endTension = tensionFor(bob.mass, bob.length, omega1, theta1, gravity);
            double taken = left;
            if (endTension <= 0.0) {
                // Find the instant, inside this piece, at which it stops pulling.
                double clear = 0.0, slack = left;
                for (int pass = 0; pass < kSlackBisections; ++pass) {
                    const double middle = 0.5 * (clear + slack);
                    double th = theta, om = omega;
                    arcStep(theta, omega, pull, dt * middle, th, om);
                    if (tensionFor(bob.mass, bob.length, om, th, gravity) > 0.0) clear = middle;
                    else slack = middle;
                }
                taken = clear;
                arcStep(theta, omega, pull, dt * taken, theta1, omega1);
            }
            const Vec3 tangent{std::cos(theta1), std::sin(theta1), 0};
            const Vec3 next = bob.anchor + Vec3{std::sin(theta1), -std::cos(theta1), 0} * bob.length;
            const Vec3 nextSpeed = tangent * (bob.length * omega1);
            if (!bent && taken < left - 1e-12) {
                bent = true;
                bendAt = 1.0 - left + taken;
                bendPoint = next;
                bendIn = speedNow;
            }
            if (endTension > 0.0 && taken >= left - 1e-12) {
                at = next;
                speedNow = nextSpeed;
                left = 0.0;
                break;
            }
            at = next;
            speedNow = nextSpeed;
            // The rope has let go at this instant, and what is left of the tick is a
            // fall.
            left -= taken;
            stateOf(at, speedNow, theta, omega, radius, outward);
            taut = false;
            if (bent) {
                bendOut = speedNow;
                break;  // one bend is all the drawing can show; the rest is next tick
            }
            continue;
        }
        // Slack: the rope is not pulling, so the bob is a thrown ball with a weight on
        // it and nothing else. The tick's travel is at the speed the weight has left it
        // with -- the same rule every other thing in this room falls by -- and the
        // question is where, along that travel, the rope catches it.
        Vec3 falling = speedNow + Vec3{0.0, -gravity * dt * left, 0.0};
        const Vec3 travel = falling * (dt * left);
        // |d + w s| = L: where the straight travel crosses the rope's own sphere.
        const Vec3 arm = at - bob.anchor;
        const double a = dot(travel, travel);
        const double b = 2.0 * dot(arm, travel);
        const double c = dot(arm, arm) - bob.length * bob.length;
        double catchAt = 2.0;
        if (a > 0.0) {
            const double disc = b * b - 4.0 * a * c;
            if (disc >= 0.0) {
                const double root = std::sqrt(disc);
                // The stable form: the smaller root first, and it is the one the bob
                // reaches first on its way out.
                const double q = -0.5 * (b + (b >= 0.0 ? root : -root));
                const double s1 = q / a;
                const double s2 = q != 0.0 ? c / q : 2.0;
                for (const double s : {s1, s2}) {
                    if (!(s >= 0.0 && s <= 1.0)) continue;
                    const double radial = dot(arm + travel * s, travel);
                    if (!(radial > 0.0)) continue;  // on its way in: no rope to catch
                    if (s < catchAt) catchAt = s;
                }
            }
        }
        if (catchAt > 1.0) {
            at = at + travel;
            speedNow = falling;
            left = 0.0;
            break;
        }
        // Caught. The bob stops at the instant the rope is at its length; the part of
        // its speed running away from the anchor is taken by the rope in one pull, and
        // what is left of the tick is spent on the circle.
        const Vec3 met = at + travel * catchAt;
        Vec3 radial{};
        double metTheta = 0.0, metOmega = 0.0, metRadius = 0.0;
        stateOf(met, falling, metTheta, metOmega, metRadius, radial);
        const double leaving = dot(falling, radial);
        const Vec3 before = falling;
        Vec3 after = falling;
        if (leaving > 0.0) {
            const Vec3 loss = radial * -leaving;
            after = falling + loss;
            addForce(met, loss * (bob.mass / dt), ForceKind::Rope, nullptr, kBobOwner);
            addForce(bob.anchor, loss * (bob.mass / dt) * -1.0, ForceKind::Rope, nullptr, kAnchorOwner);
        }
        if (!bent) {
            bent = true;
            bendAt = 1.0 - left + catchAt * left;
            bendPoint = met;
            bendIn = before;
            bendOut = after;
        }
        at = met;
        speedNow = after;
        left *= 1.0 - catchAt;
        stateOf(at, speedNow, theta, omega, radius, radial);
        outward = radial;
        taut = tensionFor(bob.mass, bob.length, omega, theta, gravity) > 0.0;
        if (bent) break;
    }

    bob.position = at;
    bob.velocity = speedNow;
    // What the rope is doing at the end of the tick, and the pull it is giving: the
    // centripetal force the circle costs, m(L ω² + g cos θ), written at the bob where
    // it pulls and at the anchor where it is pulled, so the pair is on the ledger as a
    // pair. A rope that would have to push reads as slack and writes nothing.
    stateOf(bob.position, bob.velocity, theta, omega, radius, outward);
    const double endTension = tensionFor(bob.mass, bob.length, omega, theta, gravity);
    bob.taut = radius >= bob.length - kTouch && endTension > 0.0;
    bob.tension = bob.taut ? endTension : 0.0;
    if (bob.taut) {
        const Vec3 inward = outward * -1.0;
        addForce(bob.position, inward * bob.tension, ForceKind::Rope, nullptr, kBobOwner);
        addForce(bob.anchor, inward * bob.tension * -1.0, ForceKind::Rope, nullptr, kAnchorOwner);
    }
    // The path the drawing is given, built from the two places the pieces really run
    // between. A taut piece is an arc and is drawn as its chord, which is inside the
    // circle: the drawn bob can be nearer the anchor than the rope allows and can
    // never be further, which is the one direction that must not be wrong.
    if (bent) {
        const Vec3 met = bendPoint;
        if (bendAt > 0.0) bendIn = (met - bob.previous) / (dt * bendAt);
        const double rest = 1.0 - bendAt;
        if (rest > 0.0) bendOut = (bob.position - met) / (dt * rest);
    }
    bob.awake = length(bob.position - bob.previous) > 0.0 || bob.taut;
}

}  // namespace gtc::native
