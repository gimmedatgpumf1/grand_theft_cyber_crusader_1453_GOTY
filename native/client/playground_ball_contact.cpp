#include <cstdio>
#include <cstdlib>

#include "client/playground_ball_shape.h"
#include "client/playground_shapes.h"

namespace gtc::native {
using namespace ball_shape;
using namespace playground_detail;

// What the ball's contacts answer.
//
// A contact is two things at once: a blow along the normal, which both sides share,
// and a grip across it, which is friction -- and because friction acts at the bottom
// of the ball rather than at its middle, it turns the ball as well as slowing it.
//
// Everything here is answered where the two surfaces met, not where the tick began:
// the ball has moved since, and a blow measured at the start of a tick is a blow
// against a surface the ball never reached.

// The ball against something. The normal part is answered by both sides: if the
// other side can move, the ball keeps whatever it did not hand over. The rest of
// the contact is friction, and because it acts at the bottom of the ball rather
// than at its middle it turns the ball as well as slowing it.
void PlaygroundRoom::ballContact(Vec3 normal, int bodyIndex, bool ballHeld, Vec3 contactPoint, double depth,
                                bool exchange) {
    Ball& ball = ball_;
    Body* body = bodyIndex >= 0 ? &bodies_[std::size_t(bodyIndex)] : nullptr;
    // What the ball was doing before this surface answered it. When the call is
    // done, the difference is the momentum the surface took from it or gave it,
    // which is the force the ball's arrows are drawn from -- recorded where the
    // two met, because that is where it acts.
    const Vec3 ballBefore = ball.velocity;
    const double spinBefore = ball.spin;
    // The body's side is written by giveBodyTurn, which is the call that hands it
    // over; what is left for this one is the ball's own change, recorded at the
    // place the two met.
    const auto recordBall = [&]() {
        const Vec3 push = (ball.velocity - ballBefore) * (ball.mass / kTick);
        if (length(push) > 1e-6) addForce(contactPoint, push, ForceKind::Ball, nullptr, kBallOwner);
        // And the turn it took, as the couple that turned it. A blow off the middle
        // of the ball changes its spin as well as its speed, and a blow is a force
        // whose moment is the arm it lands on -- so if the spin changed, the moment
        // that did it belongs in the ledger too, or the picture adds up in a straight
        // line and not about the middle.
        const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;
        const double turned = inertia * (ball.spin - spinBefore) / kTick;
        if (std::abs(turned) > 1e-9) addCouple(contactPoint, turned, Vec3{}, ForceKind::Ball, kBallOwner);
    };
    if (std::getenv("DBGB"))
        std::fprintf(stderr, "contact body %d depth %.6f exchange %d normal(%+.2f,%+.2f) v(%+.2f,%+.2f) at(%.3f,%.3f)\n",
                     bodyIndex, depth, exchange ? 1 : 0, normal.x, normal.y, ball.velocity.x, ball.velocity.y,
                     contactPoint.x, contactPoint.y);
    Vec3 answer = normal;
    bool canMove = false;
    // A body at rest is not stepped, and what it is not stepping it cannot give
    // either. Letting it take a share of the ball's momentum and then discarding
    // that share is not a contact, it is a pump: the ball keeps more speed than a
    // solid surface would leave it and ends up rolling under whatever is on top of
    // it faster and faster. So a resting body answers as the solid it is, and a
    // real blow wakes it -- which is the only thing that should ever move a body
    // that had come to rest.
    if (body != nullptr && body->frozen) {
        // A body asleep is a body the ball can wake, and only a real blow does it:
        // what the two are closing at, along the normal they met on.
        const Vec3 closingVelocity = ball.velocity - pointSpeed(*body, contactPoint);
        if (std::abs(dot(closingVelocity, normal)) > kRestSpeed) body->frozen = false;
    }
    if (body != nullptr) {
        // The ball's contact is answered by the body's own mass and its lever about
        // the body's centre -- the same law every other contact in the room
        // follows, at the place the two of them actually met. A body the mouse is
        // carrying has nowhere to go, and neither has one that is asleep until a
        // real blow wakes it.
        answer = (dragTarget_ == DragTarget::Body || body->frozen) ? Vec3{} : normal;
        canMove = dot(answer, normal) > 1e-9;
    }
    // What the body's step was before anything below shortens it. A corner's stored
    // velocity is not the body's motion -- the floor cuts it, and a shortened step
    // is not motion either -- so the step is read here, once, and used from here on.
    //
    // It is the speed *where the surfaces touch*, not the body's average speed: a body
    // tipping over a ball has one edge standing still and the other going twice as
    // fast as the middle, and the piece between the corners is moving at the lever's
    // interpolation of the two of them -- which is what outlineTouch already says for
    // the position. Reading the average instead says a body turning on the spot is not
    // moving at all where it is touching, so a shape rocking on a ball never rubbed it.
    const Vec3 surfaceVelocity = body != nullptr ? pointSpeed(*body, contactPoint) : Vec3{};

    // A ball found through an outline is the ball's own way out: the body's tick
    // keeps its outline clear of the ball -- that is what the sweep in tickBody is
    // for -- so what is left for the ball is where the ball itself has come to be
    // inside the shape. The ball goes back out along the way it is nearest to
    // being outside, and the floor is the one thing it cannot go out through.
    if (depth > 0.0) {
        const OutlineGap now = outlineGap(ball.position, body ? body->previous.data() : nullptr,
                                          body ? body->position.data() : nullptr, body ? body->count : 0, 1.0);
        normal = now.normal;
        const double through = ball.radius - now.gap;
        if (through > 0.0) {
            // The floor is under the ball, so a way out that goes downwards is no
            // way out: what is left over is the ball rolling along the floor, out
            // from under whatever is on it, rather than being left with a shape cut
            // into it.
            const double ground = (normal.y < 0.0)
                                      ? std::max(0.0, (ball.radius - ball.position.y) / normal.y)
                                      : through;
            const double byBall = std::min(through, ground);
            ball.position = ball.position + normal * byBall;
            const double left = through - byBall;
            if (body != nullptr && left > kSkin) {
                const OutlineGap after = outlineGap(ball.position, body->previous.data(), body->position.data(),
                                                    body->count, 1.0);
                if (after.gap < ball.radius) {
                    const auto clearAt = [&](double x) {
                        Vec3 moved{ball.position.x + x, std::max(ball.position.y, ball.radius), 0};
                        return outlineGap(moved, body->previous.data(), body->position.data(), body->count, 1.0)
                                   .gap >= ball.radius;
                    };
                    // Which way is out along the floor: the way that puts the
                    // ball further from the shape.
                    const double side = clearAt(-0.01) || !clearAt(0.01) ? -1.0 : 1.0;
                    double failed = 0.0, found = -1.0;
                    for (int step = 1; step <= 200 && found < 0.0; ++step) {
                        const double reach = 0.001 * double(step);
                        if (clearAt(side * reach)) {
                            found = reach;
                            break;
                        }
                        failed = reach;
                    }
                    if (found > 0.0) {
                        double low = failed, high = found;
                        for (int step = 0; step < kBisections; ++step) {
                            const double middle = 0.5 * (low + high);
                            if (clearAt(side * middle)) {
                                high = middle;
                            } else {
                                low = middle;
                            }
                        }
                        ball.position.x += side * high;
                        ball.position.y = std::max(ball.position.y, ball.radius);
                        if (ball.velocity.x * side < 0.0) ball.velocity.x = 0.0;
                    }
                }
            }
            contactPoint = contactPoint - normal * (through);
        }
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
    }

    if (!exchange) {
        // The blow for this body was struck earlier in the tick. What is left is
        // where the two are, and the ball may not travel into it any further.
        const double into = dot(ball.velocity - surfaceVelocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
        if (normal.y > 1e-9)
            ball.standing = ball.standing &&
                            kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        recordBall();
        return;
    }

    // Both sides stop where they met: the ball loses the part of its speed that
    // was driving into the shape, and the shape takes the blow. The bodies are
    // stepped before the ball, so where the body is, is where it has already been
    // solved to be -- the ball is the one that has travelled since.
    // What the struck corner actually did this tick. Its stored velocity can be
    // zero -- the floor cuts it when the corner lands -- while the corner is still
    // travelling down into the ball, and the ball has to answer the travel.

    const double arrivedSpeed = dot(ball.arrival - surfaceVelocity, normal);
    const double closing = dot(ball.velocity - surfaceVelocity, normal);
    // A bounce is the answer to arriving at a surface, measured against what the
    // ball arrived with rather than the speed gravity lent it on the way: a ball on
    // the floor gets that every tick, and bouncing it would be bouncing it off
    // nothing.
    const double rebound = arrivedSpeed < 0.0 ? -kBallRestitution * arrivedSpeed : 0.0;
    if (std::getenv("DBGB"))
        std::fprintf(stderr, "    arrival(%+.2f,%+.2f) surf(%+.2f,%+.2f) arrivedSpeed %+.3f closing %+.3f rebound %+.3f canMove %d\n",
                     ball.arrival.x, ball.arrival.y, surfaceVelocity.x, surfaceVelocity.y, arrivedSpeed, closing,
                     rebound, canMove ? 1 : 0);
    double pressed = 0.0;
    if (closing < 0.0) {
        if (canMove) {
            // A body struck by the ball takes the blow as the solid it is: every
            // corner of it moves together, turning about the middle by the amount
            // that blow would turn it. Where the two shapes are touching, not the
            // nearest corner: the ball meeting the middle of a face turns the body
            // by nothing, and a corner of a face is not where the blow lands.
            const double bodyShare = giveAt(*body, contactPoint, answer);
            // What the ball can take of this blow is only what it is free to do with
            // it. A ball already held by a surface, or one on the floor that the
            // blow would drive downwards, has nowhere to go: the body takes the
            // whole answer, and stops on the ball instead of sinking into it and
            // being answered again next tick.
            // A face above the ball pressing it down is a face the ball has
            // nowhere to go under: the floor is under it and the floor wins. But
            // "above" has to mean above. A face that is level points sideways, and
            // in floating point it points sideways by about a part in a million
            // billion -- so testing the sign alone said a horizontal face was
            // pressing the ball into the floor whenever the ball happened to be
            // resting on it, gave the ball no say in the blow, and a ball that ran
            // into the side of a cube stopped dead instead of bouncing off it.
            // Bounciness is not a property of up, and a side hit is a hit.
            const bool pressing = normal.y < -kSkin && ball.position.y <= ball.radius + kTouch;
            const bool ballGives = !ballHeld && !pressing;
            const double ballShare = ballGives ? 1.0 : 0.0;
            const double impulse = (rebound - closing) / (ballShare / ball.mass + bodyShare);
            if (std::getenv("DBGB"))
                std::fprintf(stderr, "      canMove: bodyShare %.4f ballShare %.1f impulse %+.4f -> v(%+.3f,%+.3f)\n",
                             bodyShare, ballShare, impulse, ball.velocity.x + normal.x * (impulse * ballShare / ball.mass),
                             ball.velocity.y + normal.y * (impulse * ballShare / ball.mass));
            ball.velocity = ball.velocity + normal * (impulse * ballShare / ball.mass);
            // What this surface can rub against the ball is what it can press the
            // ball with, and the ball is only pressed by the part of the blow it
            // actually took. A ball lying on the floor with a shape landing flat on
            // top of it takes none of that blow -- the floor under it does -- and a
            // friction solve given the whole of it span the ball sideways and down
            // at metres a second off a body that never touched it that way.
            pushBody(*body, contactPoint, answer * -impulse, ForceKind::Ball);
            pressed = impulse * ballShare;
        } else {
            // Nowhere to go: the ball takes the whole answer itself.
            const double impulse = (rebound - closing) * ball.mass;
            ball.velocity = ball.velocity + normal * (impulse / ball.mass);
            pressed = impulse;
        }
    }


    // Can this surface hold the ball still? That is the whole of static friction:
    // mu_s * N against the pull along the surface. The floor can, a slope cannot.
    // Only a surface that is underneath the ball is asked: a ball leaning on the
    // cube's upright side is held up by the floor, and letting that side answer for
    // the floor dropped a ball resting against it every other tick.
    // A face is never exactly level in floating point, so a normal that is level to
    // within this much is a wall, not a floor: it carries nothing, and asking it to
    // carry the ball made a ball resting against the cube drop and bounce every
    // other tick.
    if (normal.y > 1e-9) {
        ball.standing = ball.standing &&
                        kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
        // The surface that is under it, remembered: a ball left standing on
        // something is pressed at this point, so this is where its weight comes
        // back up and where the arrow for it belongs.
        ball.supportNormal = normal;
    }

    // A contact that is not a blow is still a contact. The surface carries the
    // ball's weight for as long as the ball is on it, and friction is that weight
    // times mu -- the same thing the floor already answers, because gravity drives
    // the ball into the floor and the ball loses that speed to it every tick. A
    // slope is not that: the ball runs *along* the face, so nothing drives it into
    // the face and the blow is nothing, and with no normal force there was no
    // friction at all. The seam stood still while the ball ran away down the face
    // and then jumped when the next blow arrived. What the surface carries when it
    // is only holding the ball up is the weight pressed into it, over the tick.
    if (!(pressed > 0.0)) pressed = kGravity * ball.mass * std::max(0.0, normal.y) * kTick;

    // Friction is not applied here. A grip at one surface changes what is left to
    // slip at every other surface the ball is against, so the whole tick's contacts
    // are solved against each other once they are all known -- see
    // settleBallFriction(). What is recorded is what each contact is: the face, how
    // it is moving there, and how hard it is pressed.
    if (pressed > 0.0 && ballTouchCount_ < kMaxBallTouches)
        ballTouches_[ballTouchCount_++] = BallTouch{normal, surfaceVelocity, pressed};
    // Rolling resistance is not done here, and it is not a force taken off the
    // ball's speed. It is a couple, it is applied to the ball's turn, and it is
    // applied once for the whole tick in settleBallFriction(), against the load the
    // contacts are really carrying. See that function: a ball that is rolling
    // perfectly has nothing sliding anywhere for friction to act on, and something
    // has to be what stops it rolling for ever.

    // And whatever is left pointing into the surface is speed the ball cannot spend.
    // The bodies are stepped before the ball, so what a body gave way by is already
    // in its positions and not in what is left of the tick: for the rest of the tick
    // this surface is where it is, and a ball that kept its inward part would drive
    // itself through a face that has already finished moving -- which is exactly
    // what a ball landing on a shape that gives a little under it does. A bounce has
    // turned the ball around before this runs, so nothing that was leaving is caught.
    {
        const double into = dot(ball.velocity, normal);
        if (into < 0.0) ball.velocity = ball.velocity - normal * into;
    }
    recordBall();
}

// The ball runs into the dot. Both have mass, so they exchange momentum along the
// line between them.
void PlaygroundRoom::hitBallWithPoint(Vec3 normal) {
    Ball& ball = ball_;
    const Vec3 ballBefore = ball.velocity, pointBefore = velocity_;
    const double spinBefore = ball.spin;
    const double closing = dot(ball.velocity - velocity_, normal);
    // Nothing springs back between two things that are both free to move.
    const double rebound = 0.0;
    if (closing < 0.0) {
        // A dot that is not free to move -- frozen, or held by the mouse -- cannot
        // take a share of the blow, and giving it one only leaves the ball with a
        // speed the dot can never have given it.
        const double pointShare = pointFrozen_ || dragTarget_ == DragTarget::Point ? 0.0 : 1.0;
        const double impulse = (rebound - closing) / (1.0 / ball.mass + pointShare / kMass);
        ball.velocity = ball.velocity + normal * (impulse / ball.mass);
        velocity_ = velocity_ - normal * (impulse * pointShare / kMass);
        ball.frozen = false;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    // A face is never exactly level in floating point, so a normal that is level to
    // within this much is a wall, not a floor: it carries nothing, and asking it to
    // carry the ball made a ball resting against the cube drop and bounce every
    // other tick.
    if (normal.y > 1e-9)
        ball.standing = ball.standing &&
                        kBallStaticFriction * normal.y >= std::sqrt(std::max(0.0, 1.0 - normal.y * normal.y));
    // The dot and the ball are two bodies in one blow: each one's share is written
    // down at the place they met, so F shows the pair of arrows the exchange is.
    {
        const Vec3 onBall = (ball.velocity - ballBefore) * (ball.mass / kTick);
        const Vec3 onPoint = (velocity_ - pointBefore) * (kMass / kTick);
        if (length(onBall) > 1e-6) addForce(ball.position, onBall, ForceKind::Ball, nullptr, kBallOwner);
        const double inertia = 0.4 * ball.mass * ball.radius * ball.radius;
        const double turned = inertia * (ball.spin - spinBefore) / kTick;
        if (std::abs(turned) > 1e-9) addCouple(point_, turned, Vec3{}, ForceKind::Ball, kBallOwner);
        if (length(onPoint) > 1e-6) addForce(point_, onPoint, ForceKind::Ball, nullptr, kDotOwner);
    }
    // Separate them, so they are not still overlapping next tick.
    const Vec3 gap = ball.position - point_;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        ball.position = ball.position + gap * (overlap * 0.5 / distance);
        point_ = point_ - gap * (overlap * 0.5 / distance);
    }
}

// The dot runs into the ball. The normal points from the ball to the dot, so the
// dot is the one that gets pushed back, and whatever it hands over is what the
// ball takes.
void PlaygroundRoom::pointAgainstBall(Vec3 normal) {
    Ball& ball = ball_;
    const double closing = dot(velocity_ - ball.velocity, normal);
    const double rebound = 0.0;
    if (closing < 0.0) {
        // A ball that a surface is already holding cannot give way: the dot is
        // stopped by the ball, not by half of it. Dividing the blow with a ball
        // that has nowhere to go leaves the dot falling into it a little more
        // every tick, which is what turns a dot resting on a ball into a dot
        // slowly winding it up.
        const bool ballGives = !ball.frozen && !(ball.touching && ball.standing && length(ball.velocity) < kRestSpeed);
        const double ballShare = ballGives ? 1.0 : 0.0;
        const double impulse = (rebound - closing) / (1.0 / kMass + ballShare / ball.mass);
        velocity_ = velocity_ + normal * (impulse / kMass);
        ball.velocity = ball.velocity - normal * (impulse * ballShare / ball.mass);
        ball.frozen = false;
        pointFrozen_ = false;
        resting_ = false;
        restingBody_ = -1;
    }
    const Vec3 gap = point_ - ball.position;
    const double distance = length(gap);
    const double overlap = kPointRadius + ball.radius - distance;
    if (overlap > 0.0 && distance > 1e-12) {
        point_ = point_ + gap * (overlap * 0.5 / distance);
        ball.position = ball.position - gap * (overlap * 0.5 / distance);
    }
}

}  // namespace gtc::native
