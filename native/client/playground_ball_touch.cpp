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
// How many times the rolling couple and the grip may be asked to agree. It is a
// guard and not a rule: the loop stops the first time neither of them moves the ball,
// which is a round or two once the two laws have settled. Eight is there so that a
// state that never settles cannot hold the tick.
constexpr int kRollGripRounds = 8;

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
    // The face as it is now, not as it was when the contact was first noticed.
    //
    // noteBallTouch() is called from tickBodies(), and it is called *before* the
    // shapes' own contacts are solved: at that instant a cube standing on the floor
    // still has the whole of this tick's fall in its speed -- half a metre a second
    // -- which the solver is about to take away. Recording that as "the face is
    // moving" told the ball's grip that a face which never moves was sliding past it
    // at half a metre a second, and friction duly dragged the ball along with it.
    // Measured in the King's corner (a ball wedged between a cube's side and the
    // triangle's slope): the grip was given (-0.043,-0.542) m/s for the cube's face,
    // and the same point's real speed was (-0.037,-0.016). The ball was shaken by a
    // surface that was standing still, and the corner it was jammed in never settled.
    //
    // By the time the grip is solved the bodies have been answered and stepped, so
    // their speed is the one they are really carrying and their spin is the one they
    // are really turning with. That is the face the ball is against.
    for (std::size_t i = 0; i < ballTouchCount_; ++i) {
        const std::size_t other = ballTouches_[i].body;
        if (other >= kBodyCount) continue;  // the floor is the one surface that never moves
        const Body& mate = bodies_[other];
        if (mate.frozen || bodyHeld(other)) {
            ballTouches_[i].surfaceVelocity = Vec3{};
            continue;
        }
        // From the face where the ball met it: see ballTickSpeed_.
        const Vec3 at = ball.position + ballTouches_[i].normal * -ball.radius;
        const Vec3 arm = at - mate.centre;
        ballTouches_[i].surfaceVelocity =
            ballTickSpeed_[other] +
            Vec3{-ballTickSpin_[other] * arm.y, ballTickSpin_[other] * arm.x, 0.0};
    }
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
    // The statics above is an estimate of the normal force, and in a squeeze it is a
    // low one: two surfaces pushing a ball between them press it harder than the
    // weight standing on it, and the solver knows the real number -- it is the normal
    // impulse it applied at that face. Using it gives the pinch four times the grip
    // (a ball spun under a resting cube goes 20 -> 0.15 rad/s in one tick instead of
    // 0.63), and it is the physically right load, because Coulomb's limit is mu times
    // the normal force and not mu times the weight.
    //
    // It is not in yet. Turning it on makes two of the squeeze placements gain energy
    // (+0.0096 J and +0.0133 J), and no amount of grip is worth a joule from nowhere:
    // the extra grip is answered by the prevention sweep, which hands back a shape's
    // travel while leaving it the speed that made it. That pair of faults has to be
    // put right before the stronger load can be switched on, and until then the
    // weaker, statics-based load is what ships.
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
    // The couple and the grip happen at the same time, so they are solved together.
    //
    // They are two laws about the same contact -- the ground holds the bottom of the
    // ball still (grip), and the ground's own squash sits a little ahead of the bottom
    // (the Crr couple) -- and each of them changes what the other one has to answer.
    // Applied once each, in either order, the tick finishes on whichever ran first:
    // the couple takes the roll of the ball as it *arrived*, and the grip then makes a
    // fresh roll out of the slide that is left. Measured in the King's corner, on a
    // ball 0.02 um from the triangle's face with nothing in the room moving:
    //
    //   the shapes' half hands the ball   v -4.58e-04, spin +8.95e-03   (a slide)
    //   the couple runs                    v -4.58e-04, spin  0.00e+00
    //   the grip runs                      v -3.83e-05, spin +3.19e-04   (a roll, v = -wr)
    //
    // The grip is right: friction cannot stop a roll, it can only stop a slide, and
    // what was left of the slide IS the roll of a ball that is being pushed. What
    // never happened is the second round: the couple that can take 0.36 rad/s in a
    // tick -- three hundred times the roll that was left -- was spent before there
    // was any roll for it to take. So the ball rolled 1.9 um a tick, 2.3 mm a minute,
    // across a floor with nothing on the screen touching it.
    //
    // Both laws are dissipative and neither can hand back what the other took, so
    // solving them in rounds converges, and it converges to the state the room would
    // be in if a tick were what it is: a hundredth of a second of both happening.
    //
    // Rolling resistance is a *bounded* couple, in exactly the sense friction is a
    // bounded force, and that is the whole difference between a roll that ends and a
    // roll that crawls for ever. The ground can hold the ball's turn against anything
    // up to M = Crr * N * r. If the couple a tick of turn costs -- I * w / dt -- is
    // inside that, the ground holds the ball still: the turn is not reduced, it is
    // *ended*, and friction is then left with nothing to answer but a slide. If it is
    // outside, the couple is spent at its limit and the ball decelerates at Crr * g,
    // which is the same number for a roll at walking pace and for a roll across the
    // room.
    //
    // Without the bound, a couple that is a *resistance* was being modelled as a
    // *removal*: the spin was taken off and then, because friction can only ever see
    // the slide left behind, the ball was given a fresh roll out of it every round --
    // each round handing back a 1/1.4 share of what the last one took, converging on
    // nothing only after eight rounds and never reaching it. Measured: 1.9 um a tick
    // before, 1.38 um after eight rounds of that, with the answer still on its way.
    Vec3 drawWay = ball.velocity;
    if (!(length(drawWay) > 1e-9)) drawWay = Vec3{-ball.spin, 0.0, 0.0};
    const double coupleLimit = ball_shape::kBallRollingResistance * normalForce * ball.radius;  // N.m
    // What a tick of the turn the ball arrived with costs, in N.m of couple.
    bool spinHeld = normalForce > 0.0 && std::abs(ball.spin) * inertia / ball_shape::kTick <= coupleLimit;
    double heldMoment = 0.0;  // N.m.s of turn the ground has held against, this tick
    double resisted = 0.0;    // rad/s of turn the ground's couple took over the whole tick
    const auto resistRoll = [&]() {
        if (!(normalForce > 0.0) || ball.spin == 0.0) return;
        if (spinHeld) {
            // The ground has the couple to end this turn, so it does: held, not
            // shaved. It costs the same couple either way, and taking it whole is what
            // makes the tick end rolled-out instead of mid-roll.
            resisted += ball.spin;
            ball.spin = 0.0;
            return;
        }
        const double take = coupleLimit * ball_shape::kTick / inertia;  // rad/s, this tick
        const double turn = std::copysign(std::min(std::abs(ball.spin), take), ball.spin);
        ball.spin -= turn;
        resisted += turn;
    };
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
        // What the surface can take along itself: the same law as everywhere else,
        // and read from the same place. A pinned body's middle cannot be moved by a
        // grip, so the share of the slip it answers is the turn alone -- read as 1/m
        // it looked like a body that gives way where it does not, the solve took more
        // slip than the face had, and the alternation that follows is momentum out of
        // nothing: a ball wedged under a seesaw's end left with two hundred radians a
        // second of turn it was never given.
        // A face standing on the floor does not give way downwards, and the floor
        // answers that part instead of the face: the response the ball's grip is
        // measured against is the response the face will actually give -- the same
        // one pushBody() will hand it. Measured as though the whole mass were free,
        // the grip takes less slip than there was, the slip is left in the ball, and
        // a ball pinched between two faces keeps a speed it can never spend.
        const Vec3 takes = bodyFree(mate, tangents[i]);
        partner[i] = dot(takes, tangents[i]) * inverseMass(mate) +
                     (arm.x * takes.y - arm.y * takes.x) * lever / mate.moment;
    }
    std::array<double, kMaxBallTouches> lambda{};
    // Rounds, and only as many as it takes: a round is over when neither law has
    // anything left to say about the state the other one left.
    for (int round = 0; round < kRollGripRounds; ++round) {
    const Vec3 roundVelocity = ball.velocity;
    const double roundSpin = ball.spin;
    resistRoll();
    for (int pass = 0; pass < kBallFrictionPasses; ++pass) {
        double worst = 0.0;
        for (std::size_t i = 0; i < ballTouchCount_; ++i) {
            const double slip = slipAt(i, ball.velocity, ball.spin);
            worst = std::max(worst, std::abs(slip));
            if (slip == 0.0) continue;
            const double lever = arms[i].x * tangents[i].y - arms[i].y * tangents[i].x;
            // A ball whose turn the ground is holding answers a friction impulse with
            // its *travel* alone: the friction still tries to spin it, and the ground
            // takes that attempt as the couple it is holding with. Read as a free
            // sphere the solver believes the ball yields to a turn it cannot yield to,
            // takes less slide than there is, and leaves the rest in the ball.
            const double effective = 1.0 / ball.mass + (spinHeld ? 0.0 : lever * lever / inertia) + partner[i];
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
            // And it opposes the slip, always. That is the whole of what a surface
            // does along itself: it resists the slide, and it can stop a slide, and it
            // can never drive one backwards. A pass that pushes with the slip rather
            // than against it is not friction, it is the solve overshooting -- the
            // impulse asked for was measured against a compliance that does not quite
            // describe the pair, so it takes more than the slip there was, and the next
            // pass takes more again in the other direction. The two of them hand the
            // ball energy, tick after tick, which is exactly what an overshooting
            // friction solve does and exactly what must never happen here.
            if (impulse * slip > 0.0) continue;
            lambda[i] = next;
            ball.velocity = ball.velocity + tangents[i] * (impulse / ball.mass);
            if (spinHeld) {
                heldMoment += lever * impulse;
                // The couple has a limit like everything else. Too much asked of it
                // and it lets go, and every newton metre second the ball has been
                // holding back is the ball's turn after all -- given to it, not
                // thrown away.
                if (std::abs(heldMoment) > coupleLimit * ball_shape::kTick) {
                    ball.spin += heldMoment / inertia;
                    heldMoment = 0.0;
                    spinHeld = false;
                }
            } else {
                ball.spin += lever * impulse / inertia;
            }
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
                    // The ball's own grip, given to the face it is on, and through
                    // the door the floor reads: this push is solved after the
                    // shapes' tick, so the floor under the face is not in this
                    // solve and the face's refused share is answered here.
                    pushBody(mate, at, tangents[i] * -impulse, ForceKind::Ball);
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
    // Neither law moved the ball this round: the two of them agree, and a further
    // round would be asked the same question and give the same answer. The test is
    // the room's own resolution -- a slide below a micrometre a tick and a rim that
    // moves less than that are not motion -- and not a round count.
    const double moved = length(ball.velocity - roundVelocity) + std::abs(ball.spin - roundSpin) * ball.radius;
    if (moved <= playground_tick::kQuantum) break;
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
        resisted = 0.0;
        heldMoment = 0.0;
    }
    // The couple, drawn once for the whole tick and in total: it was applied in rounds
    // but it is one force, and a picture that showed it eight times would be showing a
    // room that pushed eight times as hard as it did. Drawn as the force that couple is
    // worth at the rolling radius, against the way the ball is going, at the place it
    // meets the ground; a couple has no place of its own to be drawn at, and the ledger
    // knows it is a couple.
    const double coupleMoment = inertia * resisted + heldMoment;  // N.m.s of angular impulse
    if (coupleMoment != 0.0 && length(drawWay) > 1e-9) {
        const double coupleTotal = std::abs(coupleMoment) / ball_shape::kTick;  // N.m
        const Vec3 against = drawWay * (-1.0 / length(drawWay));
        addCouple(ball.position + Vec3{0.0, -ball.radius, 0.0}, coupleTotal,
                  against * (coupleTotal / ball.radius), ForceKind::Rolling, kBallOwner);
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
