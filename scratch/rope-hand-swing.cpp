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
    const double dt = 1.0 / 20.0;
    PlaygroundRoom room;
    room.reset();
    for (int tick = 0; tick < 200; ++tick) room.advance(dt);

    gtc::native::RoomFreecam camera;
    camera.reset({3.60, 1.45, 3.10}, 0.0, 0.0);

    // The other ball out of the way; this is about the one on the rope.
    auto& other = room.ball(PlaygroundRoom::kThrowBall);
    other.position = Vec3{-3.0, other.radius, 0};
    other.previous = other.shown = other.position;
    other.velocity = {};
    other.frozen = false;

    // The triangle stood on its vertical edge with its slope just touching the hanging
    // ball -- the King's picture.
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
    // A hand's width clear of the hanging ball, so the ball has room to swing away
    // from the face instead of being pressed on it.
    triangle.centre = Vec3{onTheSlope + 0.29, 0.7, 0};
    room.placeCorners(triangle);
    triangle.previous = triangle.position;
    triangle.previousCentre = triangle.centre;
    triangle.previousAngle = triangle.angle;
    triangle.velocity = {};
    triangle.spin = 0.0;
    triangle.frozen = false;

    // The hand takes the hanging ball and swings it back along its own arc, then lets
    // go: from here the swing is the room's.
    auto& ball = room.ball(PlaygroundRoom::kRopeBall);
    const Vec3 anchor = room.ropeAnchor();
    const double L = room.ropeLength();
    auto place = [&](double a) { return anchor + Vec3{std::sin(a), -std::cos(a), 0.0} * L; };
    const Vec3 grabbed = ball.position;
    room.setDragRay(Vec3{grabbed.x, grabbed.y, 5.0}, Vec3{0, 0, -1});
    room.beginDrag();
    for (double a = 0.0; a >= -0.55 - 1e-9; a -= 0.07) {
        const Vec3 p = place(a);
        room.setDragRay(Vec3{p.x, p.y, 5.0}, Vec3{0, 0, -1});
        room.advance(dt);
    }
    std::printf("let go at %.4f m/s\n", gtc::native::length(room.carryStep_) * 20.0);
    room.endDrag();

    Vec3 normal{}, at{};
    room.bodyBallGap(ball, 1, normal, at);
    const Vec3 face = normal * -1.0;
    const double start = room.totalEnergy();
    double worst = 0.0, bestBounce = 0.0;
    int bounces = 0;
    double before = 0.0;
    for (int tick = 0; tick < 400; ++tick) {
        before = gtc::native::dot(ball.velocity, face);
        room.advance(dt);
        const double now = gtc::native::dot(ball.velocity, face);
        worst = std::max(worst, room.totalEnergy() - start);
        if (before < -0.15 && now > 0.0) {
            ++bounces;
            bestBounce = std::max(bestBounce, gtc::native::length(ball.velocity));
            std::printf("      -- bounce %d at t%d: came off at %.3f m/s\n", bounces, tick,
                        gtc::native::length(ball.velocity));
        }
        if (tick >= 14 && tick <= 34)
            std::printf("t%-3d ball (%.3f,%.3f) %.3f m/s | tri x %.4f %.3f m/s\n", tick, ball.position.x,
                        ball.position.y, gtc::native::length(ball.velocity), triangle.centre.x,
                        gtc::native::length(triangle.velocity));
        if (tick >= 15 && tick <= 33 && tick % 2 == 1)
            shoot(room, camera, "scratch/rope-7-swing-" + std::to_string(tick) + ".png");
    }
    std::printf("\nbounces in 20 s: %d; the liveliest came off at %.3f m/s; the room's energy rose by at most %.3e J; the triangle moved %.4f m\n",
                bounces, bestBounce, worst, triangle.centre.x - (onTheSlope + 0.29));
    return 0;
}
