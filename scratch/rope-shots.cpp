// Pictures of the rope, out of the real room and onto a file.
//
// Two things the King asked for that no number in a check can show him:
//
//   * the ball hanging on the rope can be taken by the mouse and carried, and the rope
//     holds it while it is carried -- so the frames of a drag are rendered here, one
//     after another, from the same camera;
//   * the ball he throws collides with the ball on the rope, equal and opposite -- so a
//     throw into it is rendered frame by frame, as the picture draws it.
//
// This is a probe, not a test: it draws what the room is already doing and writes PNGs.
// The headless renderer is the same one the picture checks use, so what lands in these
// files is the picture the client draws, not a second drawing of the same numbers.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>

#define private public
#include "client/playground_pair.h"
#include "client/playground_room_tick.h"
#include "client/playground_ball_shape.h"
#include "client/playground_room.h"
#undef private
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

using gtc::native::PlaygroundRoom;
using gtc::native::Vec3;

namespace {

const PlaygroundRoom* pictureRoom = nullptr;
const gtc::native::RoomFreecam* cameraFrom = nullptr;
int shotWidth = 1100, shotHeight = 700;

gtc::native::Vec3 applyTo(const std::array<double, 16>& m, const gtc::native::Vec3& p, bool point) {
    const double w = point ? 1.0 : 0.0;
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * w, m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * w,
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * w};
}

bool renderOnHost(const gtc::native::GpuScene& scene, gtc::native::NativeFrame& frame) {
    if (cameraFrom == nullptr || pictureRoom == nullptr) return false;
    const gtc::native::Camera camera = cameraFrom->camera(shotWidth, shotHeight, false);
    gtc::native::Rasterizer raster(shotWidth, shotHeight, scene.background, camera.focal(), false);
    for (std::size_t b = 0; b < scene.batches.size(); ++b) {
        const gtc::native::GpuBatch& batch = scene.batches[b];
        if (batch.isUi) continue;
        for (std::size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
            std::array<gtc::native::RenderVertex, 3> corners;
            for (int k = 0; k < 3; ++k) {
                const gtc::native::GpuVertex& v = batch.vertices[batch.indices[i + static_cast<std::size_t>(k)]];
                const gtc::native::Vec3 world = applyTo(batch.modelMatrix, {v.x, v.y, v.z}, true);
                const gtc::native::Vec3 normal = applyTo(batch.modelMatrix, {v.nx, v.ny, v.nz}, false);
                corners[static_cast<std::size_t>(k)] = gtc::native::cameraVertex(
                    camera, world, gtc::native::normalized(normal), world, {v.u, v.v});
            }
            raster.triangle(corners, batch.material);
        }
    }
    frame = raster.finish();
    frame.wantsBgra = false;
    return true;
}

void shoot(PlaygroundRoom& room, gtc::native::RoomFreecam& camera, const std::string& path) {
    gtc::native::ViewState view;
    view.width = shotWidth;
    view.height = shotHeight;
    view.gpu = true;
    gtc::native::NativeFrame frame;
    cameraFrom = &camera;
    pictureRoom = &room;
    gtc::native::setGpuRenderer(&renderOnHost);
    room.render(frame, view, camera);
    gtc::native::setGpuRenderer(nullptr);
    std::printf("wrote %s\n", path.c_str());
    frame.save(path);
}

}  // namespace

int main() {
    PlaygroundRoom room;
    room.reset();
    for (int tick = 0; tick < 200; ++tick) room.advance(1.0 / 20.0);

    gtc::native::RoomFreecam camera;
    // Far enough back to hold the corner, the floor and both balls in one frame.
    camera.reset({3.60, 1.35, 3.60}, 0.0, 0.0);

    // 1. The room as it stands: the rope in the corner, the ball hanging on it, the
    //    ball the room throws lying on the floor to its left.
    shoot(room, camera, "scratch/rope-0-rest.png");

    // 2. The mouse takes the hanging ball and carries it out to the side, tick by tick.
    //    The hand asks for a place; the rope is what says how far it may have one.
    gtc::native::Vec3 grabbed = room.ropeBall().position;
    room.setDragRay(Vec3{grabbed.x, grabbed.y, 1.0}, Vec3{0, 0, -1});
    room.beginDrag();
    std::printf("drag took: %s\n", room.dragTarget_ == PlaygroundRoom::DragTarget::Ball ? "a ball" : "something else");
    // The hand asks for places that are further from the anchor than the rope is long,
    // and the rope is what says no: the ball is carried out until the rope is at its
    // length, and past that it goes nowhere the hand asks.
    for (int step = 1; step <= 6; ++step) {
        const double x = grabbed.x - 0.22 * step;
        const double y = grabbed.y + 0.10 * step;
        room.setDragRay(Vec3{x, y, 1.0}, Vec3{0, 0, -1});
        room.advance(1.0 / 20.0);
        const Vec3 at = room.ropeBall().position;
        std::printf("  carried to (%.3f, %.3f), %.4f m from the anchor (rope %.2f m)\n", at.x, at.y,
                    room.ropeBallDistance(), room.ropeLength());
        if (step == 2 || step == 6) shoot(room, camera, "scratch/rope-1-drag-" + std::to_string(step) + ".png");
    }
    room.endDrag();
    // And it is let go from there: it swings back and comes to rest hanging again.
    for (int tick = 0; tick < 60; ++tick) room.advance(1.0 / 20.0);
    std::printf("after the let-go: %.4f m from the anchor, taut %d\n", room.ropeBallDistance(),
                static_cast<int>(room.ropeTaut()));
    shoot(room, camera, "scratch/rope-2-swinging.png");

    // 3. The throw. The ball comes in from the left at six metres a second, straight at
    //    the ball on the rope, and the frames are the picture's own frames -- three of
    //    them a tick, at the fraction of the tick the screen would have reached.
    room.reset();
    for (int tick = 0; tick < 200; ++tick) room.advance(1.0 / 20.0);
    PlaygroundRoom& r = room;
    const Vec3 hanging = r.ropeBall().position;
    r.ball(PlaygroundRoom::kThrowBall).position = Vec3{hanging.x - 0.62, hanging.y, 0};
    r.ball(PlaygroundRoom::kThrowBall).previous = r.ball(PlaygroundRoom::kThrowBall).shown =
        r.ball(PlaygroundRoom::kThrowBall).position;
    r.ball(PlaygroundRoom::kThrowBall).velocity = Vec3{6.0, 0.0, 0.0};
    r.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
    r.ball(PlaygroundRoom::kThrowBall).frozen = false;
    for (int frame = 0; frame < 9; ++frame) {
        r.advance(1.0 / 60.0);
        // The frames with something to see in them: the approach, the instant they meet,
        // and the two of them afterwards.
        if (frame == 1 || frame == 3 || frame == 5 || frame == 7)
            shoot(r, camera, "scratch/rope-3-hit-" + std::to_string(frame) + ".png");
        std::printf("  frame %d: thrown (%.3f, %.3f) %.3f m/s | hanging (%.3f, %.3f) %.3f m/s | gap %.4f m\n", frame,
                    r.ballPosition().x, r.ballPosition().y, gtc::native::length(r.ballVelocity()),
                    r.ropeBall().position.x, r.ropeBall().position.y,
                    gtc::native::length(r.ropeBall().velocity),
                    gtc::native::length(r.ballPosition() - r.ropeBall().position) -
                        (r.ballRadius() + r.ropeBall().radius));
    }
    // 4. The other ball in the hand, with the rope ball swinging. The one on the rope is
    //    not simulated while it is being told about the other one, and the numbers say
    //    so: its angle and its speed carry on through the frames of the drag.
    room.reset();
    for (int tick = 0; tick < 200; ++tick) room.advance(1.0 / 20.0);
    {
        auto& hanging = room.ball(PlaygroundRoom::kRopeBall);
        const Vec3 anchor = room.ropeAnchor();
        hanging.position = anchor + Vec3{std::sin(0.7), -std::cos(0.7), 0.0} * room.ropeLength();
        hanging.previous = hanging.shown = hanging.position;
        hanging.velocity = {};
        hanging.frozen = false;
    }
    for (int frame = 0; frame < 10; ++frame) room.advance(1.0 / 20.0);
    const Vec3 taken = room.ball(PlaygroundRoom::kThrowBall).position;
    room.setDragRay(Vec3{taken.x, taken.y, 1.0}, Vec3{0, 0, -1});
    room.beginDrag();
    std::printf("the hand took: %s\n",
                room.dragTarget_ == PlaygroundRoom::DragTarget::Ball ? "a ball" : "something else");
    for (int frame = 0; frame < 8; ++frame) {
        room.setDragRay(Vec3{taken.x - 0.06 * frame, taken.y + 0.08 * frame, 1.0}, Vec3{0, 0, -1});
        room.advance(1.0 / 20.0);
        const Vec3 arm = room.ropeBall().position - room.ropeAnchor();
        std::printf("  held ball at (%.3f, %.3f) | the ball on the rope: %.4f rad, %.3f m/s\n",
                    room.ballPosition().x, room.ballPosition().y, std::atan2(arm.x, -arm.y),
                    gtc::native::length(room.ropeBall().velocity));
        if (frame == 0 || frame == 4 || frame == 7)
            shoot(room, camera, "scratch/rope-4-hold-the-other-" + std::to_string(frame) + ".png");
    }
    room.endDrag();

    // 5. The King's picture: the triangle standing on the floor with its slope under the
    //    hanging ball, and the ball set down exactly on that slope with the rope taut.
    //    Two pictures 150 seconds apart, from the same camera: nothing moves, and nothing
    //    appears.
    room.reset();
    for (int tick = 0; tick < 200; ++tick) room.advance(1.0 / 20.0);
    {
        // The triangle stood on its vertical edge, slope rising to the right, its face
        // passing exactly under the hanging ball -- the King's picture, to the millimetre.
        auto& triangle = room.bodies_[1];
        triangle.angle = 1.5707963267948966;
        triangle.centre = Vec3{3.6697, 0.7, 0};
        room.placeCorners(triangle);
        triangle.previous = triangle.position;
        triangle.previousCentre = triangle.centre;
        triangle.previousAngle = triangle.angle;
        triangle.velocity = {};
        triangle.spin = 0.0;
        triangle.frozen = true;
    }
    gtc::native::RoomFreecam close;
    close.reset({3.60, 1.50, 2.60}, 0.0, 0.0);
    // Ten seconds for everything the placing disturbed to finish going where it is going;
    // the two pictures below are of the ten-second mark and of a hundred and sixty.
    for (int tick = 0; tick < 200; ++tick) room.advance(1.0 / 20.0);
    const Vec3 settled = room.ropeBall().position;
    const double energyThen = room.totalEnergy();
    const auto ballEnergy = [&] {
        const auto& b = room.ropeBall();
        return 0.5 * b.mass * gtc::native::dot(b.velocity, b.velocity) + b.mass * 9.8 * b.position.y;
    };
    const double ballEnergyThen = ballEnergy();
    shoot(room, close, "scratch/rope-5-on-the-slope-0.png");
    for (int tick = 0; tick < 3000; ++tick) room.advance(1.0 / 20.0);
    const Vec3 later = room.ropeBall().position;
    const double ballEnergyAfter = ballEnergy();
    std::printf("150 s on the slope: the ball moved %.3e m, its own energy changed by %+.3e J, the room's by "
                "%+.3e J; it is %.6f m from its anchor with the rope carrying %.3f N and is at %.3e m/s\n",
                gtc::native::length(later - settled), ballEnergyAfter - ballEnergyThen,
                room.totalEnergy() - energyThen, room.ropeBallDistance(), room.ropeTension(),
                gtc::native::length(room.ropeBall().velocity));
    shoot(room, close, "scratch/rope-5-on-the-slope-3000.png");

    // 6. The King's picture: a basketball on a string swung into the slope, frame by
    //    frame through the hit. A basketball comes off the face, goes away and comes
    //    back; a wrecking ball arrives, stops, and hangs. The frames are 1/20 s apart,
    //    straight after the ball is let go from half a radian back.
    room.reset();
    for (int tick = 0; tick < 200; ++tick) room.advance(1.0 / 20.0);
    {
        auto& triangle = room.bodies_[1];
        triangle.angle = 1.5707963267948966;
        triangle.centre = Vec3{3.6697, 0.7, 0};
        room.placeCorners(triangle);
        double onTheSlope = 3.6697;
        {
            Vec3 way{}, at{};
            const double gap = room.bodyBallGap(room.ropeBall(), 1, way, at);
            if (std::abs(way.x) > 0.0) onTheSlope -= gap / way.x;
        }
        triangle.centre = Vec3{onTheSlope, 0.7, 0};
        room.placeCorners(triangle);
        triangle.previous = triangle.position;
        triangle.previousCentre = triangle.centre;
        triangle.previousAngle = triangle.angle;
        triangle.velocity = {};
        triangle.spin = 0.0;
        triangle.frozen = false;
        auto& ball = room.ball(PlaygroundRoom::kRopeBall);
        ball.position = room.ropeAnchor() + Vec3{std::sin(-0.55), -std::cos(-0.55), 0.0} * room.ropeLength();
        ball.previous = ball.shown = ball.position;
        ball.velocity = {};
        ball.spin = 0.0;
        ball.touching = ball.standing = false;
    }
    for (int frame = 0; frame < 30; ++frame) {
        room.advance(1.0 / 20.0);
        std::printf("  bounce frame %2d: ball (%.4f, %.4f) %.4f m/s | triangle %.4f m/s, spin %+.3f\n", frame,
                    room.ropeBall().position.x, room.ropeBall().position.y,
                    gtc::native::length(room.ropeBall().velocity), gtc::native::length(room.bodies_[1].velocity),
                    room.bodies_[1].spin);
        if (frame >= 8 && frame <= 17) shoot(room, close, "scratch/rope-6-bounce-" + std::to_string(frame) + ".png");
    }
    return 0;
}
