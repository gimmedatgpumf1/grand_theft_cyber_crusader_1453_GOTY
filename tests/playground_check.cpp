// The playground room, held to what it claims: no energy out of nothing, nothing
// left overlapping, nothing held still while it is moving, drag that depends on speed,
// a bounce that is the same from every direction, support that is statically right,
// friction that appears as its own force, and R that really resets the room.
//
// This is a program, not a picture. Everything it says is a number, and every number
// is one the room itself produced -- no copy of the physics lives in here, so a change
// to the room that breaks something cannot pass by agreeing with itself.

#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#define private public
#include "client/playground_pair.h"
#include "client/playground_room.h"
#undef private
#include "client/renderer.h"
#include "client/room_freecam.h"

using namespace gtc::native;

namespace {

int failures = 0;

void say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
}

void check(bool holds, const char* what, const char* detail = "") {
    if (holds) {
        say("   ok    %s %s\n", what, detail);
    } else {
        say("   FAIL  %s %s\n", what, detail);
        ++failures;
    }
}

// A renderer that draws nothing and says it drew it. The scene is still built and
// every transform still placed -- which is the part being checked here: what the
// room asks the screen to draw, not what a GPU does with it.
bool drawNothing(const GpuScene&, NativeFrame&) { return true; }

// Puts a room through one frame exactly as the client does: build the scene if it
// has none, interpolate to the frame, place every marker. This is the only way the
// drawing handles are ever set, so a room that has never been rendered has none.
void drawFrame(PlaygroundRoom& room) {
    ViewState view;
    view.width = 1168;
    view.height = 672;
    RoomFreecam camera;
    camera.reset({0, 1.4, 3.2}, 0, 0);
    NativeFrame frame;
    frame.width = view.width;
    frame.height = view.height;
    room.render(frame, view, camera);
}

// Where the screen is told to put one corner of one body: the translation of the
// batch that corner was given. If this is not the corner's place, the room is
// drawing something other than what it is simulating.
Vec3 drawnCorner(const PlaygroundRoom& room, std::size_t body, std::size_t corner) {
    const auto& matrix = room.scene_.batches[room.bodies_[body].cornerBatches[corner]].modelMatrix;
    return {matrix[12], matrix[13], matrix[14]};
}

Vec3 drawnBall(const PlaygroundRoom& room) {
    const auto& matrix = room.scene_.batches[room.ball_.faceBatch].modelMatrix;
    return {matrix[12], matrix[13], matrix[14]};
}

void ray(PlaygroundRoom& room, double x, double y) {
    room.setDragRay(Vec3{x, y, 1.0}, Vec3{0, 0, -1});
}

// Carries a body to a place with the mouse, the way a hand does: up, across, down.
void carryTo(PlaygroundRoom& room, std::size_t which, double x, double y) {
    const Vec3 start = room.bodyMiddle(which);
    ray(room, start.x, start.y);
    room.beginDrag();
    const double top = std::max(start.y, y) + 0.9;
    for (double step = start.y; step <= top; step += 0.05) {
        ray(room, start.x, step);
        room.advance(1.0 / 20.0);
    }
    for (double step = start.x; std::abs(x - step) > 0.02; step += (x > step ? 0.05 : -0.05)) {
        ray(room, step, top);
        room.advance(1.0 / 20.0);
    }
    for (double step = top; step >= y; step -= 0.05) {
        ray(room, x, step);
        room.advance(1.0 / 20.0);
    }
    ray(room, x, y);
    room.advance(1.0 / 20.0);
    room.endDrag();
}

void settle(PlaygroundRoom& room, int ticks) {
    for (int tick = 0; tick < ticks; ++tick) room.advance(1.0 / 20.0);
}

// The worst amount by which the room's total energy ever rose above where it started,
// over `ticks` of sitting there.
double highestGain(PlaygroundRoom& room, int ticks) {
    const double start = room.totalEnergy();
    double highest = start;
    for (int tick = 0; tick < ticks; ++tick) {
        room.advance(1.0 / 20.0);
        highest = std::max(highest, room.totalEnergy());
    }
    return highest - start;
}

bool anythingOverlapping(PlaygroundRoom& room) {
    for (std::size_t one = 0; one < PlaygroundRoom::kBodyCount; ++one) {
        const auto& body = room.bodies_[one];
        for (std::size_t i = 0; i < body.count; ++i)
            if (body.position[i].y < -1e-6) return true;
        Vec3 way{};
        Vec3 at{};
        if (room.bodyBallGap(one, way, at) < -1e-6) return true;
        for (std::size_t two = 0; two < PlaygroundRoom::kBodyCount; ++two) {
            if (two == one) continue;
            pair_shape::PairManifold meeting;
            if (pair_shape::collide(body.position.data(), body.count, room.bodies_[two].position.data(),
                                    room.bodies_[two].count, 0.0, meeting))
                return true;
        }
    }
    return false;
}

double largestForce(PlaygroundRoom& room, PlaygroundRoom::ForceKind kind) {
    double largest = 0.0;
    for (std::size_t i = 0; i < room.forceCount(); ++i)
        if (room.forces_[i].kind == kind) largest = std::max(largest, std::sqrt(dot(room.forces_[i].force, room.forces_[i].force)));
    return largest;
}

// ---- the room's own energy, and the ledger's
void energyNeverRises() {
    say("\nno arrangement may conjure energy\n");
    struct Case {
        const char* name;
        int kind;
    } cases[]{{"triangle on the cube", 0}, {"cube on the ball", 1}, {"cube on the triangle", 2}};
    for (const Case& one : cases) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 300);
        if (one.kind == 0) {
            const Vec3 cube = room.bodyMiddle(0);
            carryTo(room, 1, cube.x, cube.y + 0.90);
        } else if (one.kind == 1) {
            const Vec3 ball = room.ballPosition();
            carryTo(room, 0, ball.x, ball.y + 0.12 + 0.35 + 0.28);
        } else {
            carryTo(room, 0, room.bodyMiddle(1).x, 1.4);
        }
        const double gain = highestGain(room, 400);
        char detail[160];
        std::snprintf(detail, sizeof detail, "%.6f J over 400 ticks, ended at b0 (%.3f,%.3f) b1 (%.3f,%.3f)", gain,
                      room.bodies_[0].centre.x, room.bodies_[0].centre.y, room.bodies_[1].centre.x,
                      room.bodies_[1].centre.y);
        check(gain < 1e-6, one.name, detail);
    }
    // And from any pose at all, dropped from rest, as long as nothing starts inside
    // anything (which is the arrangement's fault, not the room's).
    std::mt19937 rng(20261112);
    std::uniform_real_distribution<double> pick(-2.5, 2.5);
    std::uniform_real_distribution<double> turn(-3.14159, 3.14159);
    int ran = 0;
    double worst = 0.0;
    for (int trial = 0; trial < 80; ++trial) {
        PlaygroundRoom room;
        room.reset();
        settle(room, 200);
        for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
            auto& body = room.bodies_[index];
            body.centre = Vec3{pick(rng), 1.4 + 0.8 * index + std::abs(pick(rng)) * 0.5, 0};
            body.angle = turn(rng);
            body.velocity = {};
            body.spin = 0.0;
            body.frozen = false;
            room.placeCorners(body);
        }
        if (anythingOverlapping(room)) continue;
        ++ran;
        worst = std::max(worst, highestGain(room, 300));
    }
    char detail[160];
    std::snprintf(detail, sizeof detail, "%d drops from rest, worst rise %.6f J", ran, worst);
    check(worst < 1e-6, "random poses", detail);
}

// ---- a body at rest: the ledger must be the weight and its supports, summing to nothing
void restingStatics() {
    say("\nsupport at rest is statically right\n");
    PlaygroundRoom room;
    room.reset();
    settle(room, 300);
    double sumX = 0.0, sumY = 0.0, moment = 0.0;
    for (std::size_t i = 0; i < room.forceCount(); ++i) {
        const auto& sample = room.forces_[i];
        if (sample.owner > 1) continue;
        sumX += sample.force.x;
        sumY += sample.force.y;
        moment += sample.at.x * sample.force.y - sample.at.y * sample.force.x;
    }
    char detail[200];
    std::snprintf(detail, sizeof detail, "sum F (%.6f, %.6f) N, sum M %.6f N m", sumX, sumY, moment);
    // A millionth of a newton, and a millionth of a newton metre, on a load of forty
    // newtons: the solver stops sweeping when what is left is this small, and nothing
    // this small can move anything.
    check(std::abs(sumX) < 1e-6 && std::abs(sumY) < 1e-6 && std::abs(moment) < 1e-6, "every body as it starts", detail);
    // The cube is four corners of four kilograms: thirty nine point two newtons of
    // weight, carried at two places. Split between them is the load's business; the
    // sum is not.
    double weight = 0.0, support = 0.0;
    for (std::size_t i = 0; i < room.forceCount(); ++i) {
        const auto& sample = room.forces_[i];
        if (sample.owner != 0) continue;
        weight += sample.kind == PlaygroundRoom::ForceKind::Gravity ? -sample.force.y : 0.0;
        support += sample.kind == PlaygroundRoom::ForceKind::Contact ? sample.force.y : 0.0;
    }
    std::snprintf(detail, sizeof detail, "cube: weight %.6f N carried by %.6f N", weight, support);
    check(std::abs(weight - support) < 1e-9 && std::abs(weight - 4.0 * 9.8) < 1e-9, "the cube's weight", detail);
}

void frictionIsAForce() {
    say("\nfriction is a force of its own, against the sliding\n");
    PlaygroundRoom room;
    room.reset();
    settle(room, 300);
    room.bodies_[0].frozen = false;
    room.bodies_[0].velocity = Vec3{2.0, 0, 0};
    // The ledger belongs to a tick. Running one by hand is the same as the room
    // running one, so it starts the same way: with nothing in it.
    room.clearForces();
    room.tickBodies();
    double friction = 0.0, load = 0.0;
    for (std::size_t i = 0; i < room.forceCount(); ++i) {
        const auto& sample = room.forces_[i];
        if (sample.owner != 0) continue;
        if (sample.kind == PlaygroundRoom::ForceKind::Friction) friction += std::abs(sample.force.x);
        if (sample.kind == PlaygroundRoom::ForceKind::Contact) load += sample.force.y;
    }
    char detail[200];
    std::snprintf(detail, sizeof detail, "friction %.4f N against a load of %.4f N (mu_k 0.55 -> %.4f N)", friction, load,
                  0.55 * load);
    check(friction > 0.0 && std::abs(friction - 0.55 * load) < 1e-6, "mu_k times the load", detail);
    const double first = room.bodies_[0].velocity.x;
    settle(room, 40);
    std::snprintf(detail, sizeof detail, "2.0 m/s -> %.4f m/s after one tick, %.4f m/s after forty", first,
                  room.bodies_[0].velocity.x);
    check(std::abs(room.bodies_[0].velocity.x) < 1e-3, "a shove along the floor stops", detail);
    // Friction only ever slows: never drives a body backwards.
    check(first <= 2.0 && first >= 0.0, "the first tick of friction is a slowing", detail);
}

void airDependsOnSpeed() {
    say("\nair drag is half rho Cd A v squared, and acts on motion\n");
    PlaygroundRoom room;
    room.reset();
    settle(room, 300);
    check(largestForce(room, PlaygroundRoom::ForceKind::Drag) == 0.0, "no drag arrow in a room that is standing still");
    for (double speed : {0.5, 1.0, 2.0, 4.0, 8.0}) {
        PlaygroundRoom moving;
        moving.reset();
        settle(moving, 300);
        moving.bodies_[0].frozen = false;
        moving.bodies_[0].velocity = Vec3{0, speed, 0};
        moving.applyAirDrag();
        const double shape = largestForce(moving, PlaygroundRoom::ForceKind::Drag);
        PlaygroundRoom bally;
        bally.reset();
        settle(bally, 300);
        bally.ball_.velocity = Vec3{speed, 0, 0};
        bally.ball_.frozen = false;
        bally.tickBall();
        const double ball = largestForce(bally, PlaygroundRoom::ForceKind::Drag);
        char detail[220];
        std::snprintf(detail, sizeof detail,
                      "%4.1f m/s -> shape %.5f N (half rho Cd A v^2 %.5f), ball %.5f N (%.5f)", speed, shape,
                      0.5 * 1.225 * 1.28 * 0.7 * 1.0 * speed * speed, ball,
                      0.5 * 1.225 * 0.47 * 3.14159265358979 * 0.12 * 0.12 * speed * speed);
        // A cube on the screen is 0.7 m on a side and one metre deep, so the face it
        // drives into the air is 0.7 square metres -- and only that face is pressed:
        // the one behind it is in the shape's shadow.
        const double expected = 0.5 * 1.225 * 1.28 * 0.7 * 1.0 * speed * speed;
        const double ballExpected = 0.5 * 1.225 * 0.47 * 3.14159265358979 * 0.12 * 0.12 * speed * speed;
        check(std::abs(shape - expected) < 1e-6 && std::abs(ball - ballExpected) < 1e-6, "against the formula", detail);
    }
}

// A bounce is one number, and it must be the same number whichever way the surface
// happens to point. It is measured on the speed the two bodies *close* at and the
// speed they *separate* at: a cube the ball can move takes part of the blow with it,
// and that is conservation of momentum, not a softer bounce.
//
// The hit has to land on the middle of the face, though. A ball fired from a metre
// away has fallen a hand's width by the time it arrives -- gravity, which is the whole
// point of the room -- so it strikes below the middle, and a blow below the middle
// turns the cube as well as moving it. The bounce is then smaller because some of it
// became a turn. That is not a weaker coefficient, so it is checked separately, and
// what is checked about it is that the missing bounce is in the cube's spin.
void bounceIsTheSameEveryWay() {
    say("\nbounciness is not only for vertical hits\n");
    PlaygroundRoom room;
    room.reset();
    settle(room, 300);
    const Vec3 cube = room.bodyMiddle(0);
    // Fire the ball at the flat middle of a face: a hand's width away, so that what
    // gravity takes out of its line on the way is nearly nothing.
    const auto hitTheSide = [&](double away, double& separation) {
        separation = 0.0;
        PlaygroundRoom into;
        into.reset();
        settle(into, 300);
        into.ball_.position = Vec3{cube.x - 0.35 - into.ball_.radius - away, cube.y, 0};
        into.ball_.velocity = Vec3{6.0, 0, 0};
        into.ball_.frozen = false;
        for (int tick = 0; tick < 40; ++tick) {
            const double approach = into.ballVelocity().x - into.bodies_[0].velocity.x;
            into.advance(1.0 / 20.0);
            if (approach > 0.5 && into.ballVelocity().x < into.bodies_[0].velocity.x) {
                separation = (into.bodies_[0].velocity.x - into.ballVelocity().x) / approach;
                return into.bodies_[0].spin;
            }
        }
        return 0.0;
    };
    double side = 0.0, floor = 0.0;
    hitTheSide(0.02, side);
    {
        PlaygroundRoom down;
        down.reset();
        settle(down, 300);
        down.ball_.position = Vec3{3.1, 2.0, 0};
        down.ball_.velocity = {};
        down.ball_.frozen = false;
        for (int tick = 0; tick < 40; ++tick) {
            const double before = down.ballVelocity().y;
            down.advance(1.0 / 20.0);
            if (before < -0.5 && down.ballVelocity().y > 0.0) {
                floor = down.ballVelocity().y / -before;
                break;
            }
        }
    }
    char detail[220];
    std::snprintf(detail, sizeof detail, "side restitution %.4f, floor %.4f", side, floor);
    check(side > 0.5 && floor > 0.5, "the ball leaves both a side and the floor", detail);
    check(std::abs(side - floor) < 0.01, "by exactly the same amount", detail);

    double lowSide = 0.0;
    const double spin = hitTheSide(1.2, lowSide);
    std::snprintf(detail, sizeof detail, "hit a hand's width low: restitution %.4f, the cube turned at %.3f rad/s",
                  lowSide, spin);
    check(lowSide > 0.6 && lowSide < side && spin > 0.5,
          "a hit below the middle turns the cube, and the turn is taken out of the bounce", detail);
}

// The ball is the one thing in the room small enough to be swallowed, and the one
// thing whose collision has to be a hitscan rather than a search: at 40 m/s it
// crosses two metres in a tick. Every one of these shots is solved outright, so no
// speed may be too fast to see -- and what is measured is not the sweep but the room
// after the tick, because a sweep that finds the surface and then leaves the ball a
// millimetre into it is not a hitscan that works.
//
// The second half is the case a sweep alone cannot be: a shape arriving at a ball the
// floor has already stopped, where the tick's own contact sees nothing closing (both
// of them falling at the same speed) and the overlap is made by the two positions.
// That is what put a cube around a ball and left it there for the rest of a session.
void nothingIsEverInsideAnything() {
    say("\nnothing ends a tick inside the ball\n");
    // A deterministic spread of shots, from every side, at speeds a hand can give a
    // ball and speeds it cannot.
    const double speeds[] = {0.5, 2.0, 6.0, 15.0, 40.0};
    double worst = 0.0;
    int ticks = 0;
    int worstTrial = -1;
    for (int trial = 0; trial < 60; ++trial) {
        const double angle = -3.141592653589793 + 6.283185307179586 * trial / 60.0;
        const double speed = speeds[trial % 5];
        PlaygroundRoom room;
        room.reset();
        const Vec3 cube = room.bodyMiddle(0);
        const Vec3 start{cube.x - 1.6 * std::cos(angle), cube.y + 1.2 * std::sin(angle), 0};
        if (!room.ballPlacementClear(start)) continue;
        room.ball_.position = start;
        room.ball_.velocity = Vec3{std::cos(angle), std::sin(angle), 0} * speed;
        room.ball_.frozen = false;
        for (int tick = 0; tick < 40; ++tick) {
            room.advance(1.0 / 20.0);
            ++ticks;
            for (std::size_t index = 0; index < PlaygroundRoom::kBodyCount; ++index) {
                Vec3 way{}, at{};
                const double inside = -room.bodyBallGap(index, way, at);
                if (inside > worst) {
                    worst = inside;
                    worstTrial = trial;
                }
            }
        }
    }
    char detail[200];
    std::snprintf(detail, sizeof detail, "%d ticks of shots from every side, worst %.6f m inside", ticks, worst);
    check(worst < 1e-6, "a ball fired at anything is stopped by it", detail);

    // A cube dropped onto a ball that is sitting on the floor: the shapes are the ones
    // arriving, and the ball has nowhere to go.
    double dropped = 0.0;
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 300);
        const Vec3 under{room.bodyMiddle(0).x, 0.5, 0};
        room.ball_.position = Vec3{under.x + 1.2, room.ball_.radius, 0};
        room.ball_.velocity = {};
        room.ball_.frozen = false;
        // The cube, straight above it, falling.
        room.bodies_[0].centre = Vec3{under.x + 1.2, 2.4, 0};
        room.bodies_[0].angle = 0.0;
        room.bodies_[0].frozen = false;
        room.bodies_[0].velocity = Vec3{0, -6.0, 0};
        room.bodies_[0].spin = 0.0;
        room.placeCorners(room.bodies_[0]);
        room.bodies_[0].previous = room.bodies_[0].position;
        for (int tick = 0; tick < 60; ++tick) {
            room.advance(1.0 / 20.0);
            Vec3 way{}, at{};
            dropped = std::max(dropped, -room.bodyBallGap(0, way, at));
        }
    }
    std::snprintf(detail, sizeof detail, "a cube dropped on a ball on the floor, worst %.6f m inside", dropped);
    check(dropped < 1e-6, "nor is one that arrives at the ball", detail);
}

void interactionDoesNotStick() {
    say("\nnothing sticks to anything\n");
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 300);
        carryTo(room, 0, room.bodyMiddle(1).x - 0.4, 1.6);
        const double was = room.bodies_[0].centre.x;
        settle(room, 100);
        char detail[200];
        std::snprintf(detail, sizeof detail, "a cube left on the triangle's slope slid from x %.3f to %.3f", was,
                      room.bodies_[0].centre.x);
        check(std::abs(room.bodies_[0].centre.x - was) > 0.2, "it slides down the slope", detail);
    }
    {
        PlaygroundRoom room;
        room.reset();
        settle(room, 300);
        const Vec3 cube = room.bodyMiddle(0);
        carryTo(room, 1, cube.x, cube.y + 0.90);
        settle(room, 60);
        const double resting = room.bodies_[1].centre.y;
        carryTo(room, 0, cube.x + 3.0, 0.5);
        settle(room, 40);
        char detail[200];
        std::snprintf(detail, sizeof detail, "with its support carried away the triangle fell from y %.3f to %.3f", resting,
                      room.bodies_[1].centre.y);
        check(room.bodies_[1].centre.y < resting - 0.4, "support removed wakes it", detail);
    }
}

// ---- R
void resetIsAReset() {
    say("\nR puts the room back the way it starts\n");
    PlaygroundRoom fresh;
    fresh.reset();
    const size_t ledgerWhenFresh = fresh.forceCount_;
    PlaygroundRoom used;
    used.reset();
    settle(used, 400);
    used.setDragRay(Vec3{used.bodies_[1].centre.x, used.bodies_[1].centre.y, 1.0}, Vec3{0, 0, -1});
    used.beginDrag();
    for (int tick = 0; tick < 40; ++tick) {
        used.setDragRay(Vec3{used.bodies_[1].centre.x + 0.05, used.bodies_[1].centre.y + 0.05, 1.0}, Vec3{0, 0, -1});
        used.advance(1.0 / 20.0);
    }
    used.endDrag();
    used.ball_.velocity = Vec3{7.0, 4.0, 0};
    used.ball_.frozen = false;
    settle(used, 100);
    used.bodies_[0].frozen = false;
    used.bodies_[0].velocity = Vec3{-3.0, 1.0, 0};
    settle(used, 40);
    used.reset();
    char detail[220];
    std::snprintf(detail, sizeof detail, "ledger %zu samples (a fresh room has %zu)", used.forceCount_, ledgerWhenFresh);
    check(used.forceCount_ == 0, "no arrows left over from the room before", detail);
    check(used.dragTarget_ == PlaygroundRoom::DragTarget::None && used.dragBody_ == PlaygroundRoom::kBodyCount,
          "nothing still held by the mouse");
    check(!used.restingOnBall_ && used.restingBody_ == -1 && !used.pointFrozen_, "nothing still resting on anything");
    double worst = 0.0;
    worst = std::max(worst, std::abs(used.point_.x - fresh.point_.x));
    worst = std::max(worst, std::abs(used.point_.y - fresh.point_.y));
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        worst = std::max(worst, std::abs(used.bodies_[i].centre.x - fresh.bodies_[i].centre.x));
        worst = std::max(worst, std::abs(used.bodies_[i].centre.y - fresh.bodies_[i].centre.y));
        worst = std::max(worst, std::abs(used.bodies_[i].angle - fresh.bodies_[i].angle));
        check(!used.bodies_[i].frozen, "no body left asleep", "");
    }
    worst = std::max(worst, std::abs(used.ball_.position.x - fresh.ball_.position.x));
    worst = std::max(worst, std::abs(used.ball_.position.y - fresh.ball_.position.y));
    std::snprintf(detail, sizeof detail, "largest difference from a room that was never touched: %.3e", worst);
    check(worst == 0.0, "every position is the starting one", detail);

    // And it is drawn there. A reset that puts the simulation back at the start but
    // leaves the screen showing the old places is not a reset of the room: the eye,
    // the mouse and every click land where the picture is, not where the numbers are.
    // The scene is not rebuilt by a reset, so the handles each body's face, edges and
    // corners were given must survive it -- and on the first frame after R every one
    // of them must be where a room that has never been touched puts it.
    setGpuRenderer(&drawNothing);
    drawFrame(fresh);
    drawFrame(used);
    double drawnApart = 0.0;
    bool shownThere = true;
    for (std::size_t b = 0; b < PlaygroundRoom::kBodyCount; ++b) {
        for (std::size_t i = 0; i < used.bodies_[b].count; ++i) {
            const Vec3 was = drawnCorner(fresh, b, i), now = drawnCorner(used, b, i);
            drawnApart = std::max(drawnApart, std::max(std::abs(was.x - now.x), std::abs(was.y - now.y)));
            // Where the body actually is, not just where a fresh room is: a pair of
            // rooms agreeing with each other about the wrong place is still wrong.
            shownThere = shownThere && std::abs(now.x - used.bodies_[b].position[i].x) < 1e-9 &&
                         std::abs(now.y - used.bodies_[b].position[i].y) < 1e-9;
        }
    }
    const Vec3 ballWas = drawnBall(fresh), ballNow = drawnBall(used);
    drawnApart = std::max(drawnApart, std::max(std::abs(ballWas.x - ballNow.x), std::abs(ballWas.y - ballNow.y)));
    shownThere = shownThere && std::abs(ballNow.x - used.ball_.position.x) < 1e-9 &&
                 std::abs(ballNow.y - used.ball_.position.y) < 1e-9;
    std::snprintf(detail, sizeof detail, "the screen differs from a fresh room's by %.3e", drawnApart);
    check(drawnApart == 0.0, "the first frame after R draws the starting room", detail);
    check(shownThere, "every corner and the ball are drawn where they are", "");

    // Which is also why the mouse has to work again: a click on a body that is drawn
    // there takes that body. Clicking a picture of a body that has moved away is
    // exactly the "I can drag the background but not the objects" report.
    bool takesBodies = true;
    for (std::size_t b = 0; b < PlaygroundRoom::kBodyCount; ++b) {
        const Vec3 middle = used.bodies_[b].centre;
        // At the body's own middle, which is unambiguously inside it rather than on
        // the edge, where the dot sitting on the body might be the nearer thing.
        used.setDragRay(Vec3{middle.x, middle.y, 1.0}, Vec3{0, 0, -1});
        used.beginDrag();
        takesBodies = takesBodies && used.dragBody_ == b;
        used.endDrag();
    }
    check(takesBodies, "a click on a body takes that body", "");
    setGpuRenderer(nullptr);
    // And the room it becomes is the same room: four hundred ticks of it land where
    // four hundred ticks of a never-touched room land, to the last bit.
    settle(used, 400);
    settle(fresh, 400);
    double apart = 0.0;
    for (std::size_t i = 0; i < PlaygroundRoom::kBodyCount; ++i) {
        apart = std::max(apart, std::abs(used.bodies_[i].centre.x - fresh.bodies_[i].centre.x));
        apart = std::max(apart, std::abs(used.bodies_[i].centre.y - fresh.bodies_[i].centre.y));
    }
    apart = std::max(apart, std::abs(used.ball_.position.x - fresh.ball_.position.x));
    apart = std::max(apart, std::abs(used.totalEnergy() - fresh.totalEnergy()));
    std::snprintf(detail, sizeof detail, "after four hundred more ticks the two rooms differ by %.3e", apart);
    check(apart == 0.0, "a reset room behaves exactly like a new one", detail);
}

void costOfATick() {
    say("\nwhat a tick costs\n");
    PlaygroundRoom room;
    room.reset();
    settle(room, 300);
    carryTo(room, 1, room.bodyMiddle(0).x, room.bodyMiddle(0).y + 0.9);
    const auto start = std::chrono::steady_clock::now();
    const int ticks = 2000;
    settle(room, ticks);
    const double micros =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / ticks;
    char detail[160];
    std::snprintf(detail, sizeof detail, "%.2f us a tick, and 20 Hz allows 50 000", micros);
    check(micros < 50000.0, "cheap enough for 20 Hz", detail);
}

}  // namespace

int main() {
    resetIsAReset();
    energyNeverRises();
    restingStatics();
    frictionIsAForce();
    airDependsOnSpeed();
    bounceIsTheSameEveryWay();
    nothingIsEverInsideAnything();
    interactionDoesNotStick();
    costOfATick();
    say("\n%s\n", failures == 0 ? "playground: PASS" : "playground: FAIL");
    return failures == 0 ? 0 : 1;
}
