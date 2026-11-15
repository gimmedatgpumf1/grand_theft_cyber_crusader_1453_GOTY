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
        if (wantOnlyTheBall && b != pictureRoom->ball(PlaygroundRoom::kThrowBall).faceBatch &&
            b != pictureRoom->ball(PlaygroundRoom::kThrowBall).markBatch) continue;
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
    room.ball(PlaygroundRoom::kThrowBall).position = gtc::native::Vec3{-0.62, 1.20, 0};
    room.ball(PlaygroundRoom::kThrowBall).velocity = gtc::native::Vec3{0, -3.0, 0};
    room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
    room.ball(PlaygroundRoom::kThrowBall).frozen = false;
}

// How far the drawn ball is inside the drawn shapes, as the room is about to draw
// them. This is the room's own interpolation -- the frozen bodies, the bent tick, the
// projection that keeps the drawn place legal -- not a copy of it, so what is measured
// here is what the screen gets.
double drawnDepth(PlaygroundRoom& room, std::size_t& which) {
    room.interpolate();
    // A gap is a distance, not a depth: the closest surface wins, so the answer is the
    // smallest gap there is, and a depth is what is left of the ball's radius when that
    // gap is smaller than it.
    double closest = room.ball(PlaygroundRoom::kThrowBall).shown.y;  // the floor: the height of the ball's middle
    which = room.kBodyCount;
    for (std::size_t index = 0; index < room.kBodyCount; ++index) {
        const auto& body = room.bodies_[index];
        if (body.count == 0) continue;
        const double gap = gtc::native::ball_shape::outlineGap(room.ball(PlaygroundRoom::kThrowBall).shown, body.shown.data(),
                                                              body.shown.data(), body.count, 1.0)
                               .gap;
        if (gap < closest) {
            closest = gap;
            which = index;
        }
    }
    return room.ball(PlaygroundRoom::kThrowBall).radius - closest;
}

// How far a place is from the straight piece between two places.
double offSegment(const gtc::native::Vec3& p, const gtc::native::Vec3& a, const gtc::native::Vec3& b) {
    const gtc::native::Vec3 ab = b - a;
    const double length2 = gtc::native::dot(ab, ab);
    if (!(length2 > 0.0)) return gtc::native::length(p - a);
    double t = gtc::native::dot(p - a, ab) / length2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return gtc::native::length(p - (a + ab * t));
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
                solved = std::max(solved, -room.bodyBallGap(room.ball(PlaygroundRoom::kThrowBall), index, way, at));
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
        // Two statements, and the first is the one that matters: the picture adds
        // nothing. Whatever depth the room left the ball at -- which is inside the
        // room's own touching band of a micrometre, and 0.5 of a micrometre here -- the
        // drawn ball is never deeper inside than that. The second says the room is
        // holding its own line, so the first cannot be satisfied by both of them being
        // deep inside something.
        std::snprintf(detail, sizeof detail,
                      "the corner in the picture, 200 ticks at every eighth of a tick: the drawn ball is "
                      "%.3e m inside the drawn shapes (log2 %.1f), the ball the room left is %.3e m (log2 %.1f)",
                      drawn, drawn > 0.0 ? std::log2(drawn) : -1000.0, solved,
                      solved > 0.0 ? std::log2(solved) : -1000.0);
        check(drawn <= solved + 1.0e-18 && solved <= gtc::native::playground_tick::kQuantum,
              "the picture never draws the ball deeper inside a shape than the room left it", detail);
        (void)worstTick;
        (void)worstBody;
    }

    // 3. The drawn place is on the path its own tick took. This is the check the ball
    //    needed and did not have: the drawing is handed two pieces of path, and the
    //    second one used to be built from the speed the ball had *after* a meeting --
    //    a speed the tick then resolved away when it stopped the ball at the face. The
    //    drawn ball therefore flew up to 6.2 cm past the cube's face every tick it
    //    bounced and was pulled back at the end of the tick: on screen, a ball that
    //    stutters and lags in exactly the corner the King was looking at. Nothing the
    //    drawing shows may be off the path the tick really took.
    {
        PlaygroundRoom room;
        buildTheCorner(room, 300);
        room.sceneReady_ = true;
        double worst = 0.0;
        int worstFrame = -1;
        for (int frame = 0; frame < 180; ++frame) {
            room.advance(1.0 / 60.0);
            const gtc::native::Vec3 previous = room.ball(PlaygroundRoom::kThrowBall).previous;
            const gtc::native::Vec3 position = room.ball(PlaygroundRoom::kThrowBall).position;
            const bool bent = room.ball(PlaygroundRoom::kThrowBall).bent;
            const gtc::native::Vec3 bend = room.ball(PlaygroundRoom::kThrowBall).bendPoint;
            room.interpolate();
            const gtc::native::Vec3 drawn = room.ball(PlaygroundRoom::kThrowBall).shown;
            const double off = bent ? std::min(offSegment(drawn, previous, bend), offSegment(drawn, bend, position))
                                    : offSegment(drawn, previous, position);
            if (off > worst) {
                worst = off;
                worstFrame = frame;
            }
        }
        std::snprintf(detail, sizeof detail,
                      "180 frames of a drop at 60 Hz: the drawn ball is at most %.3e m off the path its tick "
                      "took (log2 %.1f), worst frame %d", worst, worst > 0.0 ? std::log2(worst) : -1000.0,
                      worstFrame);
        check(worst <= 1.0e-9, "the ball is drawn on the path its own tick took, never off it", detail);
    }

    // 4. On a surface, the ball is drawn where the frame is -- not a tick behind.
    //
    // This is the check that fell out of the King's slide. A ball rolling down the
    // triangle's slope sits at exactly one radius from the face, and the arithmetic
    // that puts it there reads a fraction of a micrometre either side of zero; the
    // drawing used to ask "is it deeper than zero?" and answered yes, so the draw-walk
    // went back to where the tick began and the ball was drawn 50 ms behind the room
    // on 100 of 132 frames of a slide, held back by up to 0.127 m. Here it is asked
    // for directly: on every frame of a slide the ball is drawn at the place the
    // frame's own fraction of the tick gives, and on no frame is the drawing left
    // standing while the ball moves.
    {
        PlaygroundRoom room;
        room.reset();
        for (int tick = 0; tick < 300; ++tick) room.advance(1.0 / 20.0);
        room.ball(PlaygroundRoom::kThrowBall).position = gtc::native::Vec3{-2.20, 1.35, 0};
        room.ball(PlaygroundRoom::kThrowBall).previous = room.ball(PlaygroundRoom::kThrowBall).shown = room.ball(PlaygroundRoom::kThrowBall).position;
        room.ball(PlaygroundRoom::kThrowBall).velocity = {};
        room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
        room.ball(PlaygroundRoom::kThrowBall).frozen = false;
        room.sceneReady_ = true;
        double worst = 0.0, travel = 0.0;
        int stuck = 0;
        gtc::native::Vec3 lastShown{};
        bool haveLast = false;
        for (int frame = 0; frame < 120; ++frame) {
            room.advance(1.0 / 60.0);
            if (frame < 20 || room.ball(PlaygroundRoom::kThrowBall).frozen) continue;
            const double fraction = std::clamp(room.accumulator_ / gtc::native::playground_tick::kTick, 0.0, 1.0);
            const gtc::native::Vec3 previous = room.ball(PlaygroundRoom::kThrowBall).previous;
            const gtc::native::Vec3 position = room.ball(PlaygroundRoom::kThrowBall).position;
            gtc::native::Vec3 ownPlace = previous + (position - previous) * fraction;
            if (room.ball(PlaygroundRoom::kThrowBall).bent) {
                const double when = room.ball(PlaygroundRoom::kThrowBall).bendAt;
                ownPlace = fraction <= when
                               ? previous + room.ball(PlaygroundRoom::kThrowBall).bendIn * (gtc::native::playground_tick::kTick * fraction)
                               : room.ball(PlaygroundRoom::kThrowBall).bendPoint + room.ball(PlaygroundRoom::kThrowBall).bendOut *
                                     (gtc::native::playground_tick::kTick * (fraction - when));
            }
            room.interpolate();
            const gtc::native::Vec3 shown = room.ball(PlaygroundRoom::kThrowBall).shown;
            worst = std::max(worst, gtc::native::length(shown - ownPlace));
            if (haveLast && gtc::native::length(shown - lastShown) < 1.0e-9 &&
                gtc::native::length(position - previous) > 1.0e-6) ++stuck;
            lastShown = shown;
            haveLast = true;
            travel += gtc::native::length(position - previous);
        }
        std::snprintf(detail, sizeof detail,
                      "132 frames of a slide down the triangle's slope, %.3f m of travel: the drawn ball is at most "
                      "%.3e m (log2 %.1f) from the place its frame is entitled to, and on %d frames the drawing stood "
                      "still while the ball moved", travel, worst, worst > 0.0 ? std::log2(worst) : -1000.0, stuck);
        check(worst <= gtc::native::playground_tick::kQuantum && stuck == 0 && travel > 1.0,
              "a ball sliding down a slope is drawn where the frame is, never a tick behind", detail);
    }

    // 2. The pixels, over a whole drop. The whole frame and a frame of the ball alone are
    //    drawn from the same camera, frame after frame, and every pixel the ball paints
    //    must be the ball's in both. A pixel that differs is a pixel of the ball that
    //    something else was drawn over -- which is what the King saw: the triangle's
    //    light edge running across the disc as a chord, and the corner dots sitting on
    //    it. Measured on his own screenshot, the chord was 5.5 px deep, which at its
    //    scale is 34 mm.
    {
        PlaygroundRoom room;
        buildTheCorner(room, 300);
        gtc::native::RoomFreecam camera;
        camera.reset({-0.55, 0.45, 0.62}, 0.0, 0.0);
        cameraFrom = &camera;
        pictureRoom = &room;
        gtc::native::setGpuRenderer(&renderOnHost);
        gtc::native::ViewState view;
        view.width = shotWidth;
        view.height = shotHeight;
        view.gpu = true;
        std::size_t ballPixels = 0, covered = 0, frames = 0;
        long coveredBy[3] = {0, 0, 0};
        int worstFrame = -1;
        for (int frame = 0; frame < 24; ++frame) {
            // A frame's worth of a tick, then a whole tick: the drop, the landing, the
            // settling and the ball at rest in the corner, at every phase of the tick.
            room.advance(gtc::native::playground_tick::kTick * 0.37);
            haveWhole = haveBall = false;
            gtc::native::NativeFrame frameOne;
            wantOnlyTheBall = false;
            room.render(frameOne, view, camera);
            wantOnlyTheBall = true;
            room.render(frameOne, view, camera);
            wantOnlyTheBall = false;
            if (!haveWhole || !haveBall || whole.rgb.size() != ballOnly.rgb.size()) continue;
            ++frames;
            const std::size_t sky = 0;
            std::size_t coveredHere = 0;
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
                ++coveredHere;
                const int r = whole.rgb[i], g = whole.rgb[i + 1], b = whole.rgb[i + 2];
                if (g > r && b > r) ++coveredBy[0];           // the edges: light blue
                else if (r > 200 && g > 100) ++coveredBy[1];  // the corner dots: orange
                else ++coveredBy[2];                          // the fill, or anything else
            }
            if (coveredHere > 0 && worstFrame < 0) worstFrame = frame;
        }
        gtc::native::setGpuRenderer(nullptr);
        std::snprintf(detail, sizeof detail,
                      "%zu frames of the drop at %dx%d, %zu pixels of the ball, %zu of them covered by "
                      "something else (light blue %ld, orange %ld, other %ld), first bad frame %d",
                      frames, shotWidth, shotHeight, ballPixels, covered, coveredBy[0], coveredBy[1], coveredBy[2],
                      worstFrame);
        check(frames >= 20 && ballPixels > 500 && covered == 0, "nothing in the room is drawn over the ball", detail);
    }

    // 3. The rope is behind the ball too. The rope, the bob and the anchor plate are
    //    drawn at depths of their own, and the one place the question is worth asking
    //    is where the ball is on top of them: hanging on the rope, a third of the way
    //    down it, right at the top under the plate, and standing against the bob. In
    //    every one of those the ball's pixels must be its own.
    {
        PlaygroundRoom room;
        room.reset();
        for (int tick = 0; tick < 200; ++tick) room.advance(gtc::native::playground_tick::kTick);
        gtc::native::RoomFreecam camera;
        camera.reset({room.ropeAnchor().x, room.ropeAnchor().y - 0.55, 1.60}, 0.0, 0.0);
        cameraFrom = &camera;
        pictureRoom = &room;
        gtc::native::setGpuRenderer(&renderOnHost);
        gtc::native::ViewState view;
        view.width = shotWidth;
        view.height = shotHeight;
        view.gpu = true;
        const gtc::native::Vec3 places[] = {{room.ropeAnchor().x, room.ropeAnchor().y - 0.30, 0},
                                            {room.ropeAnchor().x, room.ropeAnchor().y - 0.10, 0},
                                            {room.ropeAnchor().x, room.ropeAnchor().y - room.ropeLength() + 0.10, 0}};
        std::size_t ballPixels = 0, covered = 0;
        int worstPlace = -1;
        for (int place = 0; place < 3; ++place) {
            // Frozen where the test put it: a ball falling through the rope would ask
            // the same question, but it would ask it somewhere else every frame.
            room.ball(PlaygroundRoom::kThrowBall).position = places[place];
            room.ball(PlaygroundRoom::kThrowBall).previous = room.ball(PlaygroundRoom::kThrowBall).shown = room.ball(PlaygroundRoom::kThrowBall).position;
            room.ball(PlaygroundRoom::kThrowBall).velocity = {};
            room.ball(PlaygroundRoom::kThrowBall).spin = 0.0;
            room.ball(PlaygroundRoom::kThrowBall).frozen = true;
            haveWhole = haveBall = false;
            gtc::native::NativeFrame frameOne;
            wantOnlyTheBall = false;
            room.render(frameOne, view, camera);
            wantOnlyTheBall = true;
            room.render(frameOne, view, camera);
            wantOnlyTheBall = false;
            if (!haveWhole || !haveBall || whole.rgb.size() != ballOnly.rgb.size()) continue;
            std::size_t coveredHere = 0;
            for (std::size_t i = 0; i + 2 < ballOnly.rgb.size(); i += 3) {
                const bool isBall = ballOnly.rgb[i] != ballOnly.rgb[0] || ballOnly.rgb[i + 1] != ballOnly.rgb[1] ||
                                    ballOnly.rgb[i + 2] != ballOnly.rgb[2];
                if (!isBall) continue;
                ++ballPixels;
                if (whole.rgb[i] != ballOnly.rgb[i] || whole.rgb[i + 1] != ballOnly.rgb[i + 1] ||
                    whole.rgb[i + 2] != ballOnly.rgb[i + 2]) {
                    ++covered;
                    ++coveredHere;
                }
            }
            if (coveredHere > 0 && worstPlace < 0) worstPlace = place;
        }
        room.ball(PlaygroundRoom::kThrowBall).frozen = false;
        gtc::native::setGpuRenderer(nullptr);
        std::snprintf(detail, sizeof detail,
                      "the ball frozen on the rope, under the plate and against the bob at %dx%d: %zu pixels of it, "
                      "%zu of them covered by the rope or the bob (first bad place %d)",
                      shotWidth, shotHeight, ballPixels, covered, worstPlace);
        check(ballPixels > 500 && covered == 0, "the rope is drawn behind the ball, never across it", detail);
    }

    return localFailures;
}
