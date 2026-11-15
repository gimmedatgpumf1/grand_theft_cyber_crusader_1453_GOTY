// What the screen shows, held to what the room solved.
//
// The room's own checks measure where the ball is at the end of a tick. The King is
// not looking at that: he is looking at a picture, and a picture is drawn at whatever
// fraction of a tick the frame lands on, from a position interpolated between the two
// ticks either side of it. Two things can go wrong there, and neither of them is a
// physics bug -- which is exactly why they survived every physics check and were still
// visible on screen:
//
//   * the drawn ball can be a little inside a drawn shape, because a tick's travel is a
//     straight line for the ball and a straight line for each corner, and two chords do
//     not keep a circle and a face the same distance apart;
//   * something can be *drawn over* the ball. The edge stroke is 16 mm wide and the
//     corner dots are 90 mm discs, both of them decoration that reaches outside the
//     outline it belongs to, so a ball resting exactly against a face had the light
//     edge of the triangle running across its disc and the corner dots sitting on top
//     of it. A picture that says "inside" about a ball that is touching is a picture
//     that has failed, whatever the numbers say.
//
// Both are checked here: on the drawn positions at every fraction of a tick, and on
// pixels -- every pixel of the ball in the finished frame must be the ball's own, so
// nothing in the room is drawn over it. The rest of the checks are in
// playground_check.cpp and playground_check_arrivals.cpp, which call this.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

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

namespace {

int localFailures = 0;

void say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
}

void check(bool holds, const char* what, const char* detail) {
    if (holds) {
        say("   ok    %s %s\n", what, detail);
    } else {
        say("   FAIL  %s %s\n", what, detail);
        ++localFailures;
    }
}

// ---- the two pictures a frame has to be compared with ------------------------------

const gtc::native::RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* pictureRoom = nullptr;
int shotWidth = 900, shotHeight = 600;
gtc::native::NativeFrame whole;    // everything, as the King sees it
gtc::native::NativeFrame ballOnly;  // the same camera, the ball and nothing else
bool wantOnlyTheBall = false;
bool haveWhole = false, haveBall = false;

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
        if (batch.isUi) continue;  // the labels are composited from the layer instead
        if (wantOnlyTheBall && b != pictureRoom->ball_.faceBatch && b != pictureRoom->ball_.markBatch) continue;
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
    if (wantOnlyTheBall) {
        ballOnly = frame;
        haveBall = true;
    } else {
        whole = frame;
        haveWhole = true;
    }
    return true;
}

// The King's corner: the cube standing beside the triangle's foot, the ball dropped
// into the gap between the slope, the cube's face and the floor.
void buildTheCorner(PlaygroundRoom& room, int settleTicks) {
    room.reset();
    for (int tick = 0; tick < settleTicks; ++tick) room.advance(1.0 / 20.0);
    auto& cube = room.bodies_[0];
    cube.centre = gtc::native::Vec3{-0.35, 0.3501, 0};
    cube.angle = 0.0;
    cube.velocity = {};
    cube.spin = 0.0;
    cube.frozen = true;
    room.placeCorners(cube);
    cube.previous = cube.position;
    // Put there by the test, not by the room: the interpolator reads the pose this
    // tick began in as well, and a body moved by the test is a body the screen would
    // otherwise draw travelling from where it used to be.
    cube.previousCentre = cube.centre;
    cube.previousAngle = cube.angle;
    room.bodies_[1].frozen = true;
    room.ball_.position = gtc::native::Vec3{-0.62, 1.20, 0};
    room.ball_.velocity = gtc::native::Vec3{0, -3.0, 0};
    room.ball_.spin = 0.0;
    room.ball_.frozen = false;
}

// How far the drawn ball is inside the drawn shapes, as the room is about to draw
// them. This is the room's own interpolation -- the frozen bodies, the bent tick, the
// projection that keeps the drawn place legal -- not a copy of it, so what is measured
// here is what the screen gets.
double drawnDepth(PlaygroundRoom& room, std::size_t& which) {
    room.interpolate();
    double depth = room.ball_.radius - room.ball_.shown.y;
    which = room.kBodyCount;
    for (std::size_t index = 0; index < room.kBodyCount; ++index) {
        const auto& body = room.bodies_[index];
        if (body.count == 0) continue;
        const double gap = gtc::native::ball_shape::outlineGap(room.ball_.shown, body.shown.data(),
                                                              body.shown.data(), body.count, 1.0)
                               .gap -
                           room.ball_.radius;
        if (gap > depth) {
            depth = gap;
            which = index;
        }
    }
    return depth;
}}  // namespace

// Returns how many of the checks failed.
int playgroundPictureChecks() {
    char detail[280];

    // 1. The drawn positions. Every fraction of every tick of the corner scene, the
    //    ball the screen draws is outside every shape the screen draws -- and so is
    //    the ball the room left at the end of the tick, which is the same statement
    //    about the physics and is measured here as well so the two cannot drift.
    {
        PlaygroundRoom room;
        buildTheCorner(room, 300);
        double drawn = 0.0, solved = 0.0;
        int worstTick = -1;
        std::size_t worstBody = 99;
        for (int tick = 0; tick < 200; ++tick) {
            room.advance(1.0 / 20.0);
            for (std::size_t index = 0; index < room.kBodyCount; ++index) {
                gtc::native::Vec3 way{}, at{};
                solved = std::max(solved, -room.bodyBallGap(index, way, at));
            }
            room.sceneReady_ = true;
            for (int eighth = 0; eighth <= 8; ++eighth) {
                std::size_t which = 99;
                room.accumulator_ = gtc::native::playground_tick::kTick * eighth / 8.0;
                const double depth = drawnDepth(room, which);
                if (depth > drawn) {
                    drawn = depth;
                    worstTick = tick;
                    worstBody = which;
                }
            }
        }
        std::snprintf(detail, sizeof detail,
                      "the corner in the picture, 200 ticks at every eighth of a tick: the drawn ball is "
                      "%.3e m inside the drawn shapes (log2 %.1f), the room's own %.3e m (log2 %.1f)",
                      drawn, drawn > 0.0 ? std::log2(drawn) : -1000.0, solved,
                      solved > 0.0 ? std::log2(solved) : -1000.0);
        check(drawn <= 1.0e-15 && solved <= 1.0e-6, "the picture never draws the ball inside a shape", detail);
        (void)worstTick;
        (void)worstBody;
    }

    // 2. The pixels. The whole frame and a frame of the ball alone are drawn from the
    //    same camera, and every pixel the ball paints must be the ball's in both. A
    //    pixel that differs is a pixel of the ball that something else was drawn over
    //    -- which is what the King saw: the triangle's light edge running across the
    //    disc, and the corner dots sitting on it.
    {
        PlaygroundRoom room;
        buildTheCorner(room, 300);
        for (int tick = 0; tick < 40; ++tick) room.advance(1.0 / 20.0);
        gtc::native::RoomFreecam camera;
        camera.reset({-0.55, 0.45, 0.62}, 0.0, 0.0);
        cameraFrom = &camera;
        pictureRoom = &room;
        gtc::native::setGpuRenderer(&renderOnHost);
        gtc::native::ViewState view;
        view.width = shotWidth;
        view.height = shotHeight;
        view.gpu = true;
        haveWhole = haveBall = false;
        // A little way into the tick, which is where a frame usually lands.
        room.advance(gtc::native::playground_tick::kTick * 0.25);
        gtc::native::NativeFrame frame;
        wantOnlyTheBall = false;
        room.render(frame, view, camera);
        wantOnlyTheBall = true;
        room.render(frame, view, camera);
        wantOnlyTheBall = false;
        gtc::native::setGpuRenderer(nullptr);
        std::size_t ballPixels = 0, covered = 0;
        long coveredBy[3] = {0, 0, 0};
        if (haveWhole && haveBall && whole.rgb.size() == ballOnly.rgb.size()) {
            const std::size_t sky = 0;  // the top-left pixel is sky, whatever the sky is
            for (std::size_t i = 0; i + 2 < ballOnly.rgb.size(); i += 3) {
                const bool isBall = ballOnly.rgb[i] != ballOnly.rgb[sky] ||
                                    ballOnly.rgb[i + 1] != ballOnly.rgb[sky + 1] ||
                                    ballOnly.rgb[i + 2] != ballOnly.rgb[sky + 2];
                if (!isBall) continue;
                ++ballPixels;
                if (whole.rgb[i] == ballOnly.rgb[i] && whole.rgb[i + 1] == ballOnly.rgb[i + 1] &&
                    whole.rgb[i + 2] == ballOnly.rgb[i + 2])
                    continue;
                ++covered;
                const int r = whole.rgb[i], g = whole.rgb[i + 1], b = whole.rgb[i + 2];
                if (g > r && b > r) ++coveredBy[0];        // the edges: light blue
                else if (r > 200 && g > 100) ++coveredBy[1];  // the corner dots: orange
                else ++coveredBy[2];                       // the fill, or anything else
            }
        }
        std::snprintf(detail, sizeof detail,
                      "%zu pixels of the ball at 900x600, %zu of them covered by something else "
                      "(light blue %ld, orange %ld, other %ld)",
                      ballPixels, covered, coveredBy[0], coveredBy[1], coveredBy[2]);
        check(ballPixels > 500 && covered == 0, "nothing in the room is drawn over the ball", detail);
    }

    return localFailures;
}
