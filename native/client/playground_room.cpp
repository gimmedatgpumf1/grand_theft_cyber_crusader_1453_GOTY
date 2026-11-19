#include "client/playground_room.h"


#include "client/playground_room_tick.h"
#include "client/playground_shapes.h"
#include "client/playground_ball_shape.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <vector>

namespace gtc::native {
using namespace playground_detail;
using namespace playground_tick;
namespace {

constexpr double kPointGrabRadius = 0.14;  // how close the mouse has to be to take it
constexpr double kGrabMargin = 0.06;       // and to take a body by its outline
constexpr int kMaxContacts = 4;
// Friction, in the two forms it actually has. A surface holds a body still as
// long as mu_s * N is enough to answer the sideways force; that is static
// friction, and while it holds, nothing moves. Once the pull beats it, the body
// slides and mu_k * N slows it -- tick by tick, never in one instant.
// The point is smooth and keeps its sideways speed for a long way. The solids
// are gripped far harder, and how hard also depends on how much of them is
// touching the floor: the coefficients below are the solids' personal factors,
// and they are scaled by the length of the contact they actually have.
constexpr double kPointStaticFriction = 0.40;
constexpr double kPointKineticFriction = 0.30;
// How far a sleeping dot may drift from the surface it is lying on and still be
// lying on it. A micrometre: the same "these two are the same place" the walk uses.
constexpr double kPointSupportBand = 1e-6;

} // namespace

Vec3 PlaygroundRoom::outwardNormalAt(const Body& body, Vec3 point) {
    Vec3 result{0, 1, 0};
    double nearest = 1e30;
    for (std::size_t edge = 0; edge < body.count; ++edge) {
        const Vec3 a = body.position[edge];
        const Vec3 b = body.position[(edge + 1) % body.count];
        const Vec3 side = b - a;
        const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
        if (!(sideLength > 1e-12)) continue;
        const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
        const double gap = std::abs(dot(point - a, outward));
        if (gap < nearest) { nearest = gap; result = outward; }
    }
    return result;
}

Vec3 PlaygroundRoom::bodyCorner(std::size_t body, std::size_t index) const {
    return body < kBodyCount && index < bodies_[body].count ? bodies_[body].shown[index] : Vec3{};
}

Vec3 PlaygroundRoom::bodySimulatedCorner(std::size_t body, std::size_t index) const {
    return body < kBodyCount && index < bodies_[body].count ? bodies_[body].position[index] : Vec3{};
}

Vec3 PlaygroundRoom::bodyCornerVelocity(std::size_t body, std::size_t index) const {
    if (body >= kBodyCount || index >= bodies_[body].count) return Vec3{};
    return pointSpeed(bodies_[body], bodies_[body].position[index]);
}

Vec3 PlaygroundRoom::bodyMiddle(std::size_t body) const {
    if (body >= kBodyCount) return Vec3{};
    return bodies_[body].centre;
}

double PlaygroundRoom::answerPointOnBody(std::size_t index, Vec3 pointPosition, Vec3 pointVelocity,
                                         Vec3 normal) {
    Body& body = bodies_[index];
    if (bodyHeld(index)) return 0.0;
    const double closing = dot(pointVelocity - pointSpeed(body, pointPosition), normal);
    if (!(closing < 0.0)) return 0.0;
    const double give = 1.0 / kMass + giveAt(body, pointPosition, normal);
    if (!(give > 1e-12)) return 0.0;
    // The contact stops the approach and gives none of it back: nothing bounces in
    // this room.
    const double impulse = -closing / give;
    pushBody(body, pointPosition, normal * -impulse, ForceKind::Contact);
    return impulse;
}

// The point meets a body. The position has already been put at the contact by the
// walk; this is the momentum exchange there.
void PlaygroundRoom::hitBody(std::size_t index, Vec3 normal, double into) {
    velocity_ = velocity_ + normal * (answerPointOnBody(index, point_, velocity_, normal) / kMass);
    (void)into;
}

// The height of a body's top surface directly above a given x, taken from the
// two corners of whichever side is on top. False when that x is not over the
// body at all.
bool PlaygroundRoom::bodyTopAt(std::size_t index, double x, double& height) const {
    const Body& body = bodies_[index];
    for (std::size_t side = 0; side < body.count; ++side) {
        const Vec3 a = body.position[side];
        const Vec3 b = body.position[(side + 1) % body.count];
        const Vec3 along = b - a;
        const double span = along.x;
        if (std::abs(span) < 1e-12) continue;
        // A side is the top only if it faces upwards. Comparing heights against
        // the corners instead never works on a face that is the slightest bit
        // tilted, and the top of a settled body is exactly that.
        const double normalY = -along.x / std::sqrt(along.x * along.x + along.y * along.y);
        if (normalY < 0.5) continue;
        const double position = (x - a.x) / span;
        if (position < 0.0 || position > 1.0) continue;
        height = a.y + along.y * position;
        return true;
    }
    return false;
}

// Whether a body is still directly under the point, close enough to be holding
// it up. Used to decide whether a point resting on a body stays supported or has
// to start falling again.
bool PlaygroundRoom::supportedBy(std::size_t index) const {
    const Body& body = bodies_[index];
    bool onGround = false;
    for (std::size_t i = 0; i < body.count; ++i) onGround = onGround || body.position[i].y <= kSurfaceY + 1e-6;
    if (!onGround) return false;
    double height = 0.0;
    if (!bodyTopAt(index, point_.x, height)) return false;
    return std::abs(point_.y - height) < 1e-4;
}

void PlaygroundRoom::tick() {
    previous_ = point_;
    const Vec3 from = point_;
    // A point resting on a body stays there only while that body is still under
    // it. Move the body away and the point falls.
    if (restingBody_ >= 0 && dragTarget_ != DragTarget::Point &&
        !supportedBy(std::size_t(restingBody_))) {
        restingBody_ = -1;
        resting_ = false;
        pointFrozen_ = false;
    }
    // A point that ended the last tick inside a body is put back on its
    // boundary. The sweep cannot do that itself -- it reports a crossing and
    // stops -- so a point already inside would be nudged out by a hair each tick
    // and creep along the face it is inside instead of sliding down it.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (!pushOutOfPolygon(point_, body.position.data(), body.count)) continue;
        restingBody_ = int(index);
        resting_ = true;
        supportNormal_ = outwardNormalAt(body, point_);
    }
    // Forces: gravity is m*g downwards, always. What is holding the point up
    // answers only the part of that force which drives into it; whatever is left
    // over still acts, and on a slope that is a real force down the slope. The
    // surface holds the point only if static friction can answer what is left --
    // mu_s * N -- and on a 45 degree face it cannot, which is why a point on a
    // slope slides instead of creeping to a stop.
    // The forces on the point, in the one accumulator: its weight first, m*g
    // newtons down like any other force, and then whatever is holding it up. There
    // is no separate rule for gravity here: what the surface cannot answer is left
    // in the sum and the integrator below turns the whole of it into speed.
    pointArrival_ = velocity_;
    const Vec3 weight{0.0, -kMass * kGravity, 0.0};
    pointForce_ = {};
    addForce(point_, weight, ForceKind::Gravity, &pointForce_, kDotOwner);
    // And the air. The dot is a small sphere and it pushes through the same air the
    // ball does: F = half rho Cd A v^2, against its own motion. At the speeds it
    // reaches here it is a fraction of a newton, which is exactly what it should be
    // -- but it is a force in the ledger, drawn like the rest, and a dot falling in
    // a vacuum is a dot that is not in this room.
    {
        const double swift = length(velocity_);
        if (swift > 0.0) {
            const double area = ball_shape::kPi * kPointRadius * kPointRadius;
            const double drag = 0.5 * ball_shape::kAirDensity * ball_shape::kBallDragCoefficient * area * swift * swift;
            addForce(point_, velocity_ * (-drag / swift), ForceKind::Drag, &pointForce_, kDotOwner);
        }
    }
    heldStill_ = false;
    if (dragTarget_ == DragTarget::Point) {
        // The mouse holds it: the hold is the force that carries its weight.
        addForce(point_, weight * -1.0, ForceKind::Contact, &pointForce_, kDotOwner);
        heldStill_ = true;
    } else if (resting_) {
        // The surface answers in two parts, because it does: a normal force straight
        // out of it, and friction along it. They are written into the ledger apart,
        // so the arrow that holds a dot still on a slope can be seen to be friction
        // and the arrow that presses it into the slope can be seen to be the normal.
        const Vec3 normal = supportNormal_;
        const double into = dot(weight, normal);      // negative when pressing in
        const Vec3 along = weight - normal * into;    // what the surface cannot answer
        const double carried = std::max(0.0, -into);  // N, how hard it is pressed
        addForce(point_, normal * carried, ForceKind::Contact, &pointForce_, kDotOwner);
        // Static friction holds while the point is not moving along the surface and
        // the surface can answer what is pushing it sideways; past that it slips, and
        // then kinetic friction acts, against the slip, at mu_k times the load. That
        // is what makes a dot on a steep face accelerate down it instead of standing
        // on it, and it is a force like any other, not a rule about when to stop.
        const Vec3 slip = velocity_ - normal * dot(velocity_, normal);
        const double swift = length(slip);
        Vec3 friction{};
        if (swift > kRestSpeed) {
            // Coulomb, in the one form that does not hum. The surface rubs against
            // the slide with mu_k * N -- and it can never do more than take the
            // slide away, because friction stops a thing, it does not send it back.
            // Asking for the whole of mu_k * N when a centimetre a second is left
            // reverses the point, the next tick reverses it again, and a dot that
            // has come to rest sits there buzzing for ever. What friction gives is
            // whatever is needed to leave no slip, up to what the load can carry;
            // and if the load can carry it, the point holds and does not slide at
            // all, which is static friction, which is the same law.
            const double toStop = kMass * swift / kTick;  // N, what would stop it dead
            const double grip = std::min(toStop, (toStop <= kPointStaticFriction * carried)
                                                     ? toStop
                                                     : kPointKineticFriction * carried);
            friction = slip * (-grip / swift);
            if (toStop <= kPointStaticFriction * carried) heldStill_ = true;
        } else if (length(along) <= kPointStaticFriction * carried) {
            friction = along * -1.0;
            heldStill_ = true;
        } else if (length(along) > 0.0) {
            // Not moving yet, and not held: it is about to slide the way it is being
            // pushed, and until it does, the surface resists with all it has.
            friction = along * (-kPointStaticFriction * carried / length(along));
        }
        if (length(friction) > 0.0)
            addForce(point_, friction, ForceKind::Friction, &pointForce_, kDotOwner);
    }
    velocity_ = resolved(accelerated(velocity_, pointForce_, kMass));
    traceFrom_ = point_;
    resting_ = false;
    restingBody_ = -1;
    restingOnBall_ = false;
    slideSpan_ = 0.0;
    // The tick's travel, walked from contact to contact: find where the segment
    // meets something, move to it, answer it, and carry on with the time that is
    // left. However much time is left and however fast the point is going, it
    // cannot pass through anything, and it never loses speed it did not spend on
    // a contact.
    double remaining = 1.0;
    // The surfaces this tick's travel has already been answered against.
    //
    // A contact that is met at the very start of the time that is left costs none
    // of that time, and it is still there at the start of the next try, so a walk
    // that does not remember it meets it again, answers it again, and is done --
    // the tick is spent on one face and the point has not moved at all. That is a
    // point sitting on the floor at the foot of a slope with four metres a second
    // still in it: every tick it is "arriving" at the edge it is lying on, the walk
    // runs out of steps, and its speed is never spent. A surface answered once this
    // tick is a surface the point is sliding along, and sliding along it is the
    // only thing left to do -- so it is not in the way again.
    bool metBody[kBodyCount] = {};
    bool metFloor = false, metBall = false;
    for (int step = 0; step < kMaxContacts && remaining > 1e-12; ++step) {
        const Vec3 next = point_ + velocity_ * (kTick * remaining);
        // Whichever surface is met first: the plane, or an edge of a body.
        double first = 2.0;
        std::size_t hitIndex = kBodyCount;
        Vec3 normal{0, 1, 0};
        bool wall = false;
        bool ballHit = false;
        if (!metFloor && next.y < kSurfaceY && std::abs(next.y - point_.y) > kQuantum) {
            first = std::clamp((kSurfaceY - point_.y) / (next.y - point_.y), 0.0, 1.0);
            wall = true;
        }
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            if (metBody[index]) continue;
            const Body& body = bodies_[index];
            if (body.count == 0) continue;  // not in the room
            double along = 2.0;
            Vec3 facing{0, 1, 0};
            if (!segmentHitsPolygon(point_, next, body.position.data(), body.count, along, facing)) continue;
            // A point lying on a face and moving along it is touching, not
            // arriving -- and the room has one answer to what "touching" means:
            // the two are nearer than a micrometre, and what is left of this
            // tick's travel would take the point less than a micrometre into the
            // face. Answering that as a contact costs none of the tick -- it is
            // met at the very start of the time that is left -- so the walk goes
            // round, meets the same face in the same place, and spends every step
            // it has on it, and the tick ends with the point exactly where it
            // began and all of its speed still in it. That is the dot that used to
            // stop dead at the foot of the slope with four metres a second of
            // unused motion, and this is the line that says it is not arriving.
            if (along <= kQuantum && -dot(velocity_, facing) * (kTick * remaining) < kQuantum) continue;
            if (along < first) {
                first = along;
                hitIndex = index;
                normal = facing;
                wall = false;
            }
        }
        // The balls are more things in the way: points with radii, so the point meets
        // one when their surfaces touch. Both of them, because a ball is a ball -- the
        // dot can lie on the one hanging from the rope exactly as it can lie on the one
        // thrown, and there is nothing to say which it will be.
        if (!metBall) {
            double along = 2.0;
            Vec3 facing{0, 1, 0};
            for (std::size_t index = 0; index < kBallCount; ++index) {
                const Ball& ball = balls_[index];
                if (!ball.active) continue;  // not in the room
                double reach = 2.0;
                Vec3 way{0, 1, 0};
                if (!segmentHitsCircle(point_, next, ball.position, kPointRadius + ball.radius, reach, way)) continue;
                if (reach <= kQuantum && -dot(velocity_, way) * (kTick * remaining) < kQuantum) continue;
                if (!(reach < along)) continue;
                along = reach;
                facing = way;
                ballHit = true;
                hitIndex = index;
                wall = false;
            }
            if (ballHit && along < first) {
                first = along;
                normal = facing;
            } else {
                ballHit = false;
            }
        }
        if (!(first <= 1.0)) {
            point_ = next;
            break;
        }
        point_ = point_ + (next - point_) * first;
        remaining *= 1.0 - first;
        if (ballHit) {
            metBall = true;
            // Same rule as a body: what drives into the ball is answered by both
            // sides, and what runs along it is left alone.
            const Vec3 beforeBall = velocity_;
            pointAgainstBall(balls_[hitIndex < kBallCount ? hitIndex : kThrowBall], normal);
            // The ball's answer is a force in the ledger, like the plane's.
            addForce(point_, (velocity_ - beforeBall) * (kMass / kTick), ForceKind::Contact, nullptr, kDotOwner);
            // A bounce is not a rest.
            //
            // Being held up is a fact about where the point is, not about what it
            // last touched. The ball is the one surface here that answers with a
            // bounce, and a point that comes off it doing two and a half metres a
            // second is not standing on it -- but it went on being written down as
            // resting, and the next tick's weight was answered by a ball it had
            // already left. Weight that is answered is weight that does not act:
            // the point sailed up at a constant speed, gaining a joule of height
            // every bounce out of nothing, and a dot dropped on a ball bounced on
            // it for ever -- measured: it settled at a limit of twenty centimetres
            // of height and stayed there for eighty seconds, and every bounce gave
            // it back more than it arrived with.
            //
            // What the point leaves with is the answer. If that answer is away from
            // the surface, the surface is behind it, and it is falling.
            const bool held = dot(velocity_, normal) <= 0.0;
            resting_ = held;
            restingBody_ = -1;
            restingOnBall_ = held;
            restingBall_ = hitIndex < kBallCount ? hitIndex : kThrowBall;
            supportNormal_ = normal;
            slideSpan_ += kTick * remaining;
            if (!(length(velocity_) > 1e-12)) break;
            continue;
        }
        if (wall) {
            metFloor = true;
            // The plane answers only what drives into it. What is running along
            // it is the point's kinetic energy and friction is what takes that.
            const double vyBefore = velocity_.y;
            if (velocity_.y < 0.0) velocity_.y = 0.0;
            resting_ = true;
            restingBody_ = -1;
            supportNormal_ = {0, 1, 0};
            // The plane's answer is a force like any other, and it belongs in the
            // ledger: the blow that stopped the fall, and the weight it now
            // carries. Without it a landing shows the speed gone and nothing
            // that took it.
            const double impact = kMass * (0.0 - vyBefore) / kTick;
            addForce(point_, Vec3{0, impact + kMass * kGravity, 0}, ForceKind::Contact, nullptr, kDotOwner);
            slideSpan_ += kTick * remaining;
            continue;
        }
        // The same rule for a body: only the part of the speed that drives into
        // the face is answered here.
        metBody[hitIndex] = true;
        const Vec3 along = velocity_ - normal * dot(velocity_, normal);
        const double into = dot(velocity_, normal);
        if (into < 0.0) {
            hitBody(hitIndex, normal, into);
            // The body's answer is a force in the ledger, like the plane's.
            addForce(point_, normal * (kMass * (-into) / kTick), ForceKind::Contact, nullptr, kDotOwner);
        }
        velocity_ = along;
        resting_ = true;
        restingBody_ = int(hitIndex);
        supportNormal_ = normal;
        slideSpan_ += kTick * remaining;
        if (!(length(velocity_) > 1e-12)) break;
    }
    // Still touching something when the tick ends means it was touching all
    // along: the walk only reports an arrival, and a point that is already lying
    // on the floor never arrives at it. Without this it kept its sideways speed
    // forever, because nothing ever told friction there was a surface.
    if (slideSpan_ <= 0.0) {
        if (point_.y <= kSurfaceY + kQuantum) {
            resting_ = true;
            restingBody_ = -1;
            supportNormal_ = {0, 1, 0};
            // Lying on the plane: the plane carries the weight, and that is a
            // force in the ledger whether the walk arrived here or the point
            // was already down.
            addForce(point_, Vec3{0, kMass * kGravity, 0}, ForceKind::Contact, nullptr, kDotOwner);
            slideSpan_ = kTick;
        } else {
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                const Body& body = bodies_[index];
                Vec3 normal{0, 1, 0};
                double nearest = 1e30;
                for (std::size_t edge = 0; edge < body.count; ++edge) {
                    const Vec3 a = body.position[edge];
                    const Vec3 b = body.position[(edge + 1) % body.count];
                    const Vec3 side = b - a;
                    const double sideLength = std::sqrt(side.x * side.x + side.y * side.y);
                    if (!(sideLength > 1e-12)) continue;
                    const Vec3 outward{side.y / sideLength, -side.x / sideLength, 0};
                    const double gap = std::abs(dot(point_ - a, outward));
                    if (gap < nearest) { nearest = gap; normal = outward; }
                }
                // Only a face that can carry weight counts as standing on it.
                if (!touching(nearest) || normal.y < 0.5) continue;
                resting_ = true;
                restingBody_ = int(index);
                supportNormal_ = normal;
                slideSpan_ = kTick;
                break;
            }
            // The ball is a surface too, and for the same reason: a dot lying on
            // it never arrives at it, so nothing else would tell it there is
            // something underneath. Its own shape is a circle, so the test is the
            // distance between the two middles.
            for (std::size_t index = 0; index < kBallCount && !resting_; ++index) {
                const Ball& ball = balls_[index];
                const Vec3 gap = point_ - ball.position;
                const double distance = length(gap);
                const double reach = kPointRadius + ball.radius;
                const double room = distance - reach;
                if (room < kQuantum && distance > 0.0 && gap.y > 0.0 &&
                    dot(velocity_, gap / distance) <= 0.0) {
                    resting_ = true;
                    restingBody_ = -1;
                    restingOnBall_ = true;
                    restingBall_ = index;
                    supportNormal_ = gap / distance;
                    slideSpan_ = kTick;
                }
            }
        }
    }
    // Nothing may be left inside a body: a contact is found just before it, and
    // the walk stops there, but rounding can leave a point a hair deep.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        pushOutOfPolygon(point_, bodies_[index].position.data(), bodies_[index].count);
    }
    // Kinetic friction over the part of the tick the point spent on something.
    // The line that is drawn is the travel that was actually walked this tick.
    traceTo_ = point_;
    // Friction is not taken out of the speed here, and it never was meant to be.
    // It has already acted as what it is: a force along the surface, written into
    // the ledger at the top of this tick and turned into speed by the same
    // integrator that turns the weight into speed. What used to happen after the
    // walk -- the slide taken off the velocity by hand, against a normal force
    // written down as m*g*cos, whether or not that was the load the surface was
    // actually carrying, and whether or not the force above had already taken it --
    // charged the dot friction twice, the second time at a pressure nothing had
    // measured. There is one friction in this room and it is a force.
    slideSpan_ = 0.0;
    if (dragTarget_ != DragTarget::Point && resting_ && !(length(velocity_) > kRestSpeed) && heldStill_) {
        // No speed and no net force: this point cannot do anything next tick.
        // Freezing also ends the interpolation, so what is drawn is the resting
        // position and not some fraction of the way down the last tick.
        pointFrozen_ = true;
        velocity_ = {};
        previous_ = point_;
        shown_ = point_;
    }
    // The dot has finished its walk. Two things follow from where it ended:
    // if it is stopped, the drag that was computed from its arrival velocity
    // is a lie -- a still dot has no drag -- so it is struck from the ledger.
    // And every force the dot felt this tick is drawn at the dot, not at where
    // the dot was when the force was written down, or the arrows lag behind
    // the dot they belong to.
    if (length(velocity_) < kQuantum / kTick) {
        for (std::size_t i = 0; i < forceCount_; ++i) {
            ForceSample& sample = forces_[i];
            if (sample.owner == kDotOwner && sample.kind == ForceKind::Drag)
                sample.force = {};
        }
    }
    for (std::size_t i = 0; i < forceCount_; ++i) {
        ForceSample& sample = forces_[i];
        if (sample.owner == kDotOwner) sample.at = point_;
    }
    // A frozen body is not stepped at all. It has no velocity and no force to
    // resolve, and stepping it anyway let gravity and the floor clamp nibble at
    // it tick after tick, which drifted a body that was supposed to be asleep.
    // The bodies are stepped first, so that when the ball is swept nothing has to
    // be predicted: each body's own travel this tick is known exactly, and the
    // ball's is tested against it. That is what finds a body dropped onto a ball
    // lying still -- a ball that is not going anywhere has no travel of its own to
    // find anything with.
    // Every body is stepped together, because a step one of them takes is a step
    // the others have to be answered for at the same instant: the walk looks for
    // the first meeting of anything with anything, and that meeting is answered on
    // both sides at once.
    tickBodies();
    // Both balls, in order, each one a ball: it is swept through the room it has just
    // had written into it, answered at every surface it meets, and -- for the one on the
    // rope -- held on its rope once its own tick is over. The rope is a constraint on
    // the ball, not a second body, so it is applied after the ball has moved and before
    // the next ball is swept against it, which is why the rope ball goes first: the
    // thrown ball's sweep then meets it where the rope really left it.
    tickBalls();
    haveTrace_ = length(traceTo_ - traceFrom_) > kQuantum;
    if (length(point_ - from) > kQuantum) {
        ghosts_[ghostNext_] = point_;
        ghostNext_ = (ghostNext_ + 1) % ghosts_.size();
        ghostCount_ = std::min(ghostCount_ + 1, ghosts_.size());
    }
}

// A body is its corner points of equal mass. Every tick each corner is given
// gravity and asked to travel its own v/20, its own hitscan segment is traced
// against the plane, and then the fixed distances are pushed back to what they
// were. A corner that moves therefore pulls and pushes the others through those
// distances: that is the whole of the coupling.
// Whether the dot's own support has gone away underneath it.
//
// A dot goes to sleep on a floor, a shape or the ball, and while it sleeps nothing
// moves it. Take the thing it is lying on out from under it -- drag the shape, roll
// the ball away -- and it would hang in the air for ever: asleep, and wrong. So the
// surface it was resting on is asked, every tick, whether it is still there, and if
// it is not the dot wakes and falls, which is what it was about to do anyway.
bool PlaygroundRoom::pointSupportGone() const {
    if (restingOnBall_) {
        const Ball& ball = balls_[restingBall_ < kBallCount ? restingBall_ : kThrowBall];
        const Vec3 gap{point_.x - ball.position.x, point_.y - ball.position.y, 0.0};
        return length(gap) > ball.radius + kPointSupportBand;
    }
    if (restingBody_ >= 0 && restingBody_ < int(kBodyCount)) {
        const Body& body = bodies_[std::size_t(restingBody_)];
        if (body.count == 0) return true;
        if (ball_shape::insideOutline(point_, body.position.data(), body.count)) return false;
        const ball_shape::OutlineGap gap =
            ball_shape::outlineGap(point_, body.position.data(), body.position.data(), body.count, kPointRadius);
        return gap.gap > kPointSupportBand;
    }
    return point_.y > kSurfaceY + kPointSupportBand;
}

// Whether any body's step this tick has arrived at the point.
//
// The dot is a point with a radius, and a body arriving at it is a face or a corner
// arriving at a circle -- the same question the ball's own hitscan answers, so it is
// asked here with the same geometry. What it is for is the one thing a sleeping point
// cannot do for itself: a point that is not stepped has no sweep, and a body that
// falls on it while it sleeps has nothing to stop it. The point wakes, and from then
// on its own tick -- which does have a sweep -- answers whatever met it.
bool PlaygroundRoom::pointReachedByBodies() const {
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;
        // It has already overtaken the point: wherever the point is, a body's outline
        // around it is a meeting that has happened and has to be answered. A body that
        // has come to rest is not skipped here: it is asleep, but the point inside it
        // is still inside it, and a sleeping shape is exactly what a shape that landed
        // on a sleeping dot becomes -- skip it and the dot is left inside it for ever.
        if (ball_shape::insideOutline(point_, body.position.data(), body.count)) return true;
        if (body.frozen) continue;
        // And where the shape was, to where it is now: the piece of its outline the
        // step swept, and every corner of it through the same travel. A body falling a
        // quarter of a metre a tick is swept, not sampled.
        for (std::size_t i = 0; i < body.count; ++i) {
            const Vec3 before = body.previous[i], after = body.position[i];
            const Vec3 side = body.position[(i + 1) % body.count] - body.position[i];
            const Vec3 sideBefore = body.previous[(i + 1) % body.count] - body.previous[i];
            if (length(side) <= 0.0 && length(sideBefore) <= 0.0) continue;
            // A corner's travel, against the point's own radius.
            const Vec3 travel = after - before;
            const double lengthSquared = dot(travel, travel);
            const double along = lengthSquared > 1e-18 ? std::clamp(dot(point_ - before, travel) / lengthSquared, 0.0, 1.0) : 0.0;
            if (length(point_ - (before + travel * along)) <= kPointRadius) return true;
            // And the side swept through the tick, which is what catches a face coming
            // down flat across the point with neither of its ends anywhere near it.
            for (const double fraction : {0.0, 0.5, 1.0}) {
                const Vec3 a = body.previous[i] + travel * fraction;
                const Vec3 b = body.previous[(i + 1) % body.count] + travel * fraction;
                const Vec3 edge = b - a;
                const double edgeSquared = dot(edge, edge);
                const double place = edgeSquared > 1e-18 ? std::clamp(dot(point_ - a, edge) / edgeSquared, 0.0, 1.0) : 0.0;
                if (length(point_ - (a + edge * place)) <= kPointRadius) return true;
            }
        }
    }
    return false;
}

void PlaygroundRoom::freezeCheck() {
    if (!pointFrozen_ && dragTarget_ != DragTarget::Point && resting_ && length(velocity_) * kTick < kQuantum &&
        (point_.y <= kSurfaceY || restingBody_ >= 0 || restingOnBall_)) {
        pointFrozen_ = true;
        velocity_ = {};
    }
}

void PlaygroundRoom::advance(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return;
    if (dragTarget_ != DragTarget::None) updateDrag();
    // One fixed tick per 20 Hz interval, never more than five after a stall.
    accumulator_ = std::min(accumulator_ + seconds, 0.25);
    int ticks = 0;
    while (accumulator_ >= kTick && ticks < 5) {
        accumulator_ -= kTick;
        runOneTick();
        ++ticks;
    }
}

// One tick, whole. advance() calls this from its accumulator; stepOnce() calls
// it directly. The snapshot is taken before anything moves, so stepping back
// restores the state this tick started from, and the log built at the end is
// the log of this tick.
void PlaygroundRoom::runOneTick() {
        pushDebugSnapshot();
        double kineticBefore = 0.0, potentialBefore = 0.0;
        energyBudget(kineticBefore, potentialBefore);
        const bool tracePhysics = std::getenv("GTC_TRACE_CONTEXT") != nullptr;
        const char* traceContext = tracePhysics ? std::getenv("GTC_TRACE_CONTEXT") : "";
        const double traceInitialEnergy = totalEnergy();
        const auto tracePhase = [&](const char* phase, double before) {
            if (!tracePhysics) return;
            std::printf("ROOM_TRACE context=%s phase=%s E_before=%.12g E_after=%.12g dE=%+.12g\n",
                        traceContext, phase, before, totalEnergy(), totalEnergy()-before);
            for (std::size_t i=0; i<kBodyCount; ++i) {
                const Body& b=bodies_[i];
                if (!b.count) continue;
                std::printf("  BODY_TRACE id=%zu p=(%.9g,%.9g) v=(%.9g,%.9g) w=%.9g frozen=%d\n",
                            i,b.centre.x,b.centre.y,b.velocity.x,b.velocity.y,b.spin,int(b.frozen));
            }
            for (std::size_t i=0; i<kBallCount; ++i) {
                const Ball& b=balls_[i];
                std::printf("  BALL_TRACE id=%zu p=(%.9g,%.9g) v=(%.9g,%.9g) w=%.9g frozen=%d\n",
                            i,b.position.x,b.position.y,b.velocity.x,b.velocity.y,b.spin,int(b.frozen));
            }
        };
        double traceBeforePhase = totalEnergy();
        // What the ball arrived with, taken before anything in the room moved this
        // tick. A bounce is the answer to arriving at something; a push it gets
        // part-way through the tick is not an arrival, and must not be bounced back.
        for (Ball& ball : balls_) ball.arrival = ball.velocity;
        // The ball's account for the tick, opened at the same instant and for the
        // same reason: the blows written into it below have to be every blow of this
        // tick, including the ones the shapes deal it before its own tick begins.
        for (Ball& ball : balls_) ball.blows = {};
        // A point parked on a body is only held up by that body. Take it away --
        // drag it, or let it slide -- and the support is gone, so the point goes
        // back to being a falling body.
        if (pointFrozen_ && restingBody_ >= 0 && !supportedBy(std::size_t(restingBody_))) {
            pointFrozen_ = false;
            resting_ = false;
            restingBody_ = -1;
        }
        // The same for a ball: take it away and the dot has nothing under it.
        if (pointFrozen_ && restingOnBall_) {
            const Ball& ball = balls_[restingBall_ < kBallCount ? restingBall_ : kThrowBall];
            const double distance = length(point_ - ball.position);
            if (distance > kPointRadius + ball.radius + kQuantum) {
                pointFrozen_ = false;
                resting_ = false;
                restingOnBall_ = false;
            }
        }
        // A resting point is still something a shape can be dropped on, and a point
        // that is not stepped runs no sweep: the shape would go through it and be
        // answered a tick later, from inside, which is the clipping. So the dot is
        // asked, every tick, whether a body has arrived at it -- and if one has, the
        // point wakes and its own tick answers it.
        if (pointFrozen_ && (pointReachedByBodies() || pointSupportGone())) {
            pointFrozen_ = false;
            resting_ = false;
            restingBody_ = -1;
            restingOnBall_ = false;
        }
        // The ledger belongs to one tick: every force written into it was applied
        // during that tick, and a stale arrow is worse than no arrow.
        clearForces();
        // What this tick is going to step, worked out before it starts stepping: an
        // object that is stepped writes its own forces, so an object that is not
        // stepped is the only one whose forces have to be written for it below. A
        // body that falls asleep at the end of its own tick has already said what
        // was acting on it, and adding a second, static, pair on top of that would
        // be counting the same weight twice.
        //
        // "Stepped" is not the same question as "awake now": a body that began the tick
        // asleep and was woken by a load arriving on it was stepped, and its forces
        // for this tick are the solver's. Asking only what it was when the tick began
        // wrote the replay on top of the solver's answer, and the triangle under a
        // cube was drawn carrying twice what holds it up.
        for (std::size_t index = 0; index < kBodyCount; ++index) bodies_[index].woken = false;
        std::array<bool, kBodyCount> stepped{};
        for (std::size_t index = 0; index < kBodyCount; ++index) stepped[index] = !bodies_[index].frozen;
        const bool pointStepped = !pointFrozen_;
        std::array<bool, kBallCount> ballStepped{};
        for (std::size_t index = 0; index < kBallCount; ++index)
            ballStepped[index] = !balls_[index].frozen || ballNeighboursMoving(balls_[index]);
        if (!pointFrozen_) {
            tick();
            tracePhase("room.tick", traceBeforePhase);
        } else {
            // The dot is asleep, so the bodies and the ball are stepped on their
            // own. The dot's own handle is the only thing left out.
            for (std::size_t index = 0; index < kBodyCount; ++index) {
                Body& body = bodies_[index];
                if (body.frozen) {
                    body.previousCentre = body.centre;
                    body.previousAngle = body.angle;
                    body.previous = body.position;
                    body.velocity = {};
                    body.spin = 0.0;
                }
            }
            tickBodies();
            tracePhase("room.tickBodies", traceBeforePhase);
            traceBeforePhase = totalEnergy();
            tickBalls();
            tracePhase("room.tickBalls", traceBeforePhase);
        }
        traceBeforePhase = totalEnergy();
        // Whatever a turn carried through the ball is put back out, once, after both
        // halves of the tick have moved. It is here rather than inside either of them
        // because it is about the state the tick ends in, whichever of the two put it
        // there -- and the ball's sweep cannot see a shape turning into it.
        for (Ball& ball : balls_) separateBall(ball);
        tracePhase("room.separateBall", traceBeforePhase);
        traceBeforePhase = totalEnergy();
        // And now the tick is over, having asked everything it is going to ask: this
        // is where a shape is measured for stillness, so that nothing later in the
        // same tick can hand it a push. See the note at the call this replaces.
        settleBodies();
        tracePhase("room.settleBodies", traceBeforePhase);
        traceBeforePhase = totalEnergy();

        // Read after the tick, so it covers the bodies the tick woke as well as the
        // ones that were already awake when it began.
        for (std::size_t index = 0; index < kBodyCount; ++index) stepped[index] = stepped[index] || bodies_[index].woken;
        freezeCheck();
        tracePhase("room.freezeCheck", traceBeforePhase);
        traceBeforePhase = totalEnergy();
        // What is still acting, now that the tick has finished with everything. It
        // is written here, at the end, because that is when it is known which things
        // were stepped and which were not: a body that was woken by this tick's
        // contacts has already written the forces that woke it, and a body that is
        // still asleep has written nothing at all.
        // Every impulse the tick applied, turned into the arrows that show it. This
        // is the end of the tick and the only place it happens: the bodies answer
        // first and the ball after them, and both of their blows belong to this one
        // tick. Flushed between the two, as it used to be, the ball's half of every
        // contact was drawn a frame later than the shape's -- a pair of forces that
        // never appear together, which reads as one body pushing with nothing pushing
        // back.
        writeImpulses();
        recordStaticForces(stepped, pointStepped, ballStepped);
        tracePhase("room.writeImpulses+staticForces", traceBeforePhase);
        if (tracePhysics)
            std::printf("ROOM_TRACE_TOTAL context=%s E_start=%.12g E_end=%.12g dE=%+.12g\n",
                        traceContext, traceInitialEnergy, totalEnergy(), totalEnergy()-traceInitialEnergy);
        buildTickLog(traceInitialEnergy, kineticBefore, potentialBefore);
        ++tickCount_;
}

// --- debug stepping ---------------------------------------------------------
//
// The snapshot is a plain copy of everything a tick reads or writes. It is
// taken at the start of every tick, kept for the last 600, and restored by
// stepBack(). Copying it is a memcpy of tens of kilobytes -- nothing next to
// the tick itself -- so it runs always, not only while the log is open: the
// moment D is pressed the history is already there.

void PlaygroundRoom::pushDebugSnapshot() {
    DebugHistoryEntry entry;
    DebugSnapshot& s = entry.snapshot;
    s.bodies = bodies_;
    s.balls = balls_;
    s.rope = rope_;
    s.extraRopes = extraRopes_;
    s.point = point_; s.previous = previous_; s.shown = shown_;
    s.velocity = velocity_; s.pointForce = pointForce_;
    s.traceFrom = traceFrom_; s.traceTo = traceTo_;
    s.ghosts = ghosts_; s.ghostCount = ghostCount_; s.ghostNext = ghostNext_;
    s.forces = forces_; s.forceCount = forceCount_;
    s.impulses = impulses_; s.impulseCount = impulseCount_;
    s.tickPatches = tickPatches_;
    s.accumulator = accumulator_;
    s.dragTarget = dragTarget_; s.dragBall = dragBall_; s.dragBody = dragBody_;
    s.handSpeed = handSpeed_; s.planeGrab = planeGrab_; s.grabOffset = grabOffset_;
    s.dragOrigin = dragOrigin_; s.dragDirection = dragDirection_;
    s.slideSpan = slideSpan_; s.supportNormal = supportNormal_;
    s.pointArrival = pointArrival_; s.heldStill = heldStill_;
    s.resting = resting_; s.pointFrozen = pointFrozen_;
    s.restingBody = restingBody_; s.restingOnBall = restingOnBall_;
    s.restingBall = restingBall_;
    s.bodyStepClamped = bodyStepClamped_;
    s.tickCount = tickCount_;
    debugHistory_.push_back(std::move(entry));
    if (debugHistory_.size() > kDebugHistoryCapacity)
        debugHistory_.erase(debugHistory_.begin());
}

void PlaygroundRoom::restoreDebugSnapshot(const DebugSnapshot& s) {
    bodies_ = s.bodies;
    balls_ = s.balls;
    rope_ = s.rope;
    extraRopes_ = s.extraRopes;
    point_ = s.point; previous_ = s.previous; shown_ = s.shown;
    velocity_ = s.velocity; pointForce_ = s.pointForce;
    traceFrom_ = s.traceFrom; traceTo_ = s.traceTo;
    ghosts_ = s.ghosts; ghostCount_ = s.ghostCount; ghostNext_ = s.ghostNext;
    forces_ = s.forces; forceCount_ = s.forceCount;
    impulses_ = s.impulses; impulseCount_ = s.impulseCount;
    tickPatches_ = s.tickPatches;
    accumulator_ = s.accumulator;
    dragTarget_ = s.dragTarget; dragBall_ = s.dragBall; dragBody_ = s.dragBody;
    handSpeed_ = s.handSpeed; planeGrab_ = s.planeGrab; grabOffset_ = s.grabOffset;
    dragOrigin_ = s.dragOrigin; dragDirection_ = s.dragDirection;
    slideSpan_ = s.slideSpan; supportNormal_ = s.supportNormal;
    pointArrival_ = s.pointArrival; heldStill_ = s.heldStill;
    resting_ = s.resting; pointFrozen_ = s.pointFrozen;
    restingBody_ = s.restingBody; restingOnBall_ = s.restingOnBall;
    restingBall_ = s.restingBall;
    bodyStepClamped_ = s.bodyStepClamped;
    tickCount_ = s.tickCount;
}

void PlaygroundRoom::stepOnce() {
    // One tick and no more, whatever the accumulator was holding. The
    // accumulator is left at a whole tick so the picture shows the tick's end
    // state; resumeFromPause() clears it again when the room is let run.
    accumulator_ = 0.0;
    runOneTick();
    accumulator_ = kTick;
}

bool PlaygroundRoom::stepBack() {
    if (debugHistory_.empty()) return false;
    const DebugHistoryEntry entry = std::move(debugHistory_.back());
    debugHistory_.pop_back();
    restoreDebugSnapshot(entry.snapshot);
    // The log of the tick that produced this state is the new top entry's --
    // it ran from the state before this one to the state now on screen.
    tickLog_ = debugHistory_.empty() ? std::vector<std::string>{} : debugHistory_.back().log;
    // Show the restored tick's end state, not its start.
    accumulator_ = kTick;
    return true;
}

namespace {
// The names the ledger writes, for the log. One table, so the log and the
// arrows can never disagree about what a kind is called.
const char* forceKindName(PlaygroundRoom::ForceKind kind) {
    switch (kind) {
        case PlaygroundRoom::ForceKind::Gravity: return "Gravity";
        case PlaygroundRoom::ForceKind::Contact: return "Contact";
        case PlaygroundRoom::ForceKind::Friction: return "Friction";
        case PlaygroundRoom::ForceKind::Pair: return "Pair";
        case PlaygroundRoom::ForceKind::Ball: return "Ball";
        case PlaygroundRoom::ForceKind::Drag: return "Drag";
        case PlaygroundRoom::ForceKind::Rolling: return "Rolling";
        case PlaygroundRoom::ForceKind::Rope: return "Rope";
        default: return "?";
    }
}
const char* bodyName(std::size_t index) {
    switch (index) {
        case PlaygroundRoom::kCube: return "cube";
        case PlaygroundRoom::kTriangle: return "triangle";
        case PlaygroundRoom::kSeesaw: return "seesaw";
        default: return "body";
    }
}
}  // namespace

// What one tick did, line by line. Read at the end of the tick, from the state
// the tick left and the ledger it wrote: the energy before and after, every
// speed and spin, every contact patch with its gaps and impulses, every force
// and impulse in the ledger, and the counters. Nothing here is recomputed --
// it is the room's own numbers, written down.
void PlaygroundRoom::buildTickLog(double energyBefore, double kineticBefore, double potentialBefore) {
    tickLog_.clear();
    double kineticAfter = 0.0, potentialAfter = 0.0;
    energyBudget(kineticAfter, potentialAfter);
    const double energyAfter = kineticAfter + potentialAfter;
    char line[512];
    const auto add = [&](const char* format, ...) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        tickLog_.emplace_back(line);
    };
    add("=== TICK %llu (dt %.3f s) ===", static_cast<unsigned long long>(tickCount_ + 1), kTick);
    add("ENERGY start KE %.9g PE %.9g TOTAL %.9g", kineticBefore, potentialBefore, energyBefore);
    add("ENERGY end   KE %.9g PE %.9g TOTAL %.9g  dE %+.9g", kineticAfter, potentialAfter, energyAfter,
        energyAfter - energyBefore);
    for (std::size_t i = 0; i < kBodyCount; ++i) {
        const Body& b = bodies_[i];
        if (b.count == 0) continue;
        const double ke = 0.5 * b.mass * dot(b.velocity, b.velocity) + 0.5 * b.moment * b.spin * b.spin;
        add("BODY %zu %s mass %.6g I %.6g centre (%.9g, %.9g) angle %.9g v (%.9g, %.9g) spin %.9g frozen %d "
            "contacts %d met %d KE %.9g PE %.9g",
            i, bodyName(i), b.mass, b.moment, b.centre.x, b.centre.y, b.angle, b.velocity.x, b.velocity.y,
            b.spin, int(b.frozen), b.contacts, b.met, ke, b.mass * playground_tick::kGravity * b.centre.y);
        for (std::size_t c = 0; c < b.count; ++c)
            add("  corner %zu (%.9g, %.9g)", c, b.position[c].x, b.position[c].y);
        if (b.appliedCount > 0) {
            add("  rest forces replayed: %zu", b.appliedCount);
            for (std::size_t a = 0; a < b.appliedCount; ++a)
                add("    %s at (%.9g, %.9g) J (%.9g, %.9g)", forceKindName(b.applied[a].kind),
                    b.applied[a].at.x, b.applied[a].at.y, b.applied[a].impulse.x, b.applied[a].impulse.y);
        }
    }
    for (std::size_t i = 0; i < kBallCount; ++i) {
        const Ball& b = balls_[i];
        if (!b.active) continue;  // not in the room: no data for what is not there
        const double inertia = 0.4 * b.mass * b.radius * b.radius;
        const double ke = 0.5 * b.mass * dot(b.velocity, b.velocity) + 0.5 * inertia * b.spin * b.spin;
        add("BALL %zu pos (%.9g, %.9g) v (%.9g, %.9g) spin %.9g angle %.9g frozen %d touching %d standing %d "
            "blows (%.9g, %.9g) KE %.9g PE %.9g",
            i, b.position.x, b.position.y, b.velocity.x, b.velocity.y, b.spin, b.angle, int(b.frozen),
            int(b.touching), int(b.standing), b.blows.x, b.blows.y, ke,
            b.mass * playground_tick::kGravity * b.position.y);
        for (std::size_t t = 0; t < b.touchCount; ++t)
            add("  touch %zu body %zu normal (%.6g, %.6g) surfaceV (%.6g, %.6g) pressed %.9g given %.9g arrival %.9g",
                t, b.touches[t].body, b.touches[t].normal.x, b.touches[t].normal.y,
                b.touches[t].surfaceVelocity.x, b.touches[t].surfaceVelocity.y, b.touches[t].pressed,
                b.touches[t].given, b.touches[t].arrival);
        for (std::size_t t = 0; t < b.supportCount; ++t)
            add("  support %zu body %zu normal (%.6g, %.6g)", t, b.supports[t].body, b.supports[t].normal.x,
                b.supports[t].normal.y);
        for (std::size_t t = 0; t < Ball::kSurfaces; ++t)
            if (b.meetingGiven[t] != 0.0 || b.meetingArrival[t] != 0.0)
                add("  meeting surface %zu given %.9g arrival %.9g speedIn %.9g speedOut %.9g", t,
                    b.meetingGiven[t], b.meetingArrival[t], b.meetingSpeedIn[t], b.meetingSpeedOut[t]);
    }
    add("POINT pos (%.9g, %.9g) v (%.9g, %.9g) resting %d frozen %d supportBody %d onBall %d slideSpan %.9g",
        point_.x, point_.y, velocity_.x, velocity_.y, int(resting_), int(pointFrozen_), restingBody_,
        int(restingOnBall_), slideSpan_);
    if (rope_.attached)
        add("ROPE anchor (%.9g, %.9g) length %.9g taut %d tension %.9g ballDist %.9g", rope_.anchor.x,
            rope_.anchor.y, rope_.length, int(rope_.taut), rope_.tension, ropeBallDistance());
    add("PATCHES %zu", tickPatches_.count);
    for (std::size_t p = 0; p < tickPatches_.count; ++p) {
        const Manifold& m = tickPatches_.items[p];
        add("  patch %zu body %zu other %zu ball %zu ends %zu normal (%.6g, %.6g) kind %s quiet %d", p, m.body,
            m.other, m.ball, m.ends, m.normal.x, m.normal.y, forceKindName(m.kind), int(m.quiet));
        for (std::size_t e = 0; e < m.ends; ++e)
            add("    end %zu point (%.9g, %.9g) gap %.9g Jn %.9g Jt %.9g rebound %.9g", e, m.point[e].x,
                m.point[e].y, m.gap[e], m.normalImpulse[e], m.tangentImpulse[e], m.rebound[e]);
    }
    add("FORCES %zu", forceCount_);
    for (std::size_t f = 0; f < forceCount_; ++f) {
        const ForceSample& s = forces_[f];
        add("  force %zu %s owner %zu at (%.9g, %.9g) F (%.9g, %.9g) moment %.9g couple %d", f,
            forceKindName(s.kind), s.owner, s.at.x, s.at.y, s.force.x, s.force.y, s.moment, int(s.couple));
    }
    add("IMPULSES %zu", impulseCount_);
    for (std::size_t f = 0; f < impulseCount_; ++f) {
        const RecordedImpulse& s = impulses_[f];
        add("  impulse %zu %s on %zu at (%.9g, %.9g) J (%.9g, %.9g)", f, forceKindName(s.kind), s.on, s.at.x,
            s.at.y, s.impulse.x, s.impulse.y);
    }
    std::size_t foundThrough = 0, liftedOut = 0;
    for (const Ball& b : balls_) { foundThrough += b.foundThrough; liftedOut += b.liftedOut; }
    add("COUNTERS bodyStepClamped %zu foundThrough %zu liftedOut %zu", bodyStepClamped_, foundThrough, liftedOut);
    // Hand the log to the history entry this tick started from, so stepping
    // back to that state shows this tick's log with it.
    if (!debugHistory_.empty()) debugHistory_.back().log = tickLog_;
}

} // namespace gtc::native
