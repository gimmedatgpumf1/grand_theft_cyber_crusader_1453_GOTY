// The King's arrow, hunted in the scene he saw it in, and then drawn.
//
// Two questions, one scene. First: does anything in the room ever write a contact
// that points down -- the arrow that started this, a normal force with a sign the
// wrong way -- when the shape it acts on is not simply being pushed away from
// something? A contact cannot pull a body towards the surface it presses on, and it
// cannot be a normal force out of the air. Second: the state that used to write it,
// drawn, so it can be looked at instead of read off a table.
#include "common.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr double kDt = 1.0 / 20.0;
int shotWidth = 700, shotHeight = 500;
const RoomFreecam* cameraFrom = nullptr;
const PlaygroundRoom* labelsFrom = nullptr;
Vec3 applyTo(const std::array<double, 16>& m, const Vec3& p, bool point) {
    const double w = point ? 1.0 : 0.0;
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * w, m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * w,
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * w};
}
bool renderOnHost(const GpuScene& scene, NativeFrame& frame) {
    if (cameraFrom == nullptr) return false;
    const Camera camera = cameraFrom->camera(shotWidth, shotHeight, false);
    Rasterizer raster(shotWidth, shotHeight, scene.background, camera.focal(), false);
    for (const GpuBatch& batch : scene.batches) {
        if (batch.isUi) continue;
        for (std::size_t i = 0; i + 2 < batch.indices.size(); i += 3) {
            std::array<RenderVertex, 3> corners;
            for (int k = 0; k < 3; ++k) {
                const GpuVertex& v = batch.vertices[batch.indices[i + static_cast<std::size_t>(k)]];
                const Vec3 world = applyTo(batch.modelMatrix, {v.x, v.y, v.z}, true);
                const Vec3 normal = applyTo(batch.modelMatrix, {v.nx, v.ny, v.nz}, false);
                corners[static_cast<std::size_t>(k)] = cameraVertex(camera, world, normalized(normal), world, {v.u, v.v});
            }
            raster.triangle(corners, batch.material);
        }
    }
    frame = raster.finish();
    frame.wantsBgra = false;
    if (labelsFrom != nullptr) compositeUi(frame, labelsFrom->forceLabels_);
    return true;
}
void shoot(PlaygroundRoom& room, RoomFreecam& cam, const std::string& path) {
    ViewState view;
    view.width = shotWidth;
    view.height = shotHeight;
    view.gpu = true;
    cameraFrom = &cam;
    labelsFrom = &room;
    NativeFrame frame;
    if (!room.render(frame, view, cam) || !frame.save(path)) std::fprintf(stderr, "shot failed %s\n", path.c_str());
}
// Set a shape up in the state the seesaw sweep throws it into.
void seat(PlaygroundRoom& r, int trial) {
    constexpr double S = PlaygroundRoom::kSeesaw;
    auto& s = r.bodies_[S];
    s.angle = (double(trial) / 200.0) * 1.7 - 0.85;
    s.spin = (double((trial * 37) % 60) - 30.0) * 0.7;
    PlaygroundRoom::placeCorners(s);
    s.previous = s.position;
    s.previousCentre = s.centre;
    s.previousAngle = s.angle;
    auto& c = r.bodies_[0];
    auto& t = r.bodies_[1];
    // Above the plank, always: a shape put down inside one is a state the room never
    // makes, and the first thing it does with it is throw it out -- whichever way is
    // shortest, which is sometimes down. What is being asked here is what the room
    // draws in play, so the shapes are let go from clear air and fall onto it.
    double top = -1e30;
    for (std::size_t q = 0; q < s.count; ++q) top = std::max(top, s.position[q].y);
    const double dropX = -1.0 + 2.0 * double(trial % 7) / 6.0;
    c.centre = {s.centre.x + dropX, top + 0.30 + double(trial % 5) * 0.30, 0};
    c.angle = double(trial % 4) * 0.7;
    c.velocity = {};
    c.spin = 0.0;
    c.frozen = false;
    PlaygroundRoom::placeCorners(c);
    c.previous = c.position;
    c.previousCentre = c.centre;
    c.previousAngle = c.angle;
    t.centre = {s.centre.x - dropX, top + 0.90 + double(trial % 3) * 0.25, 0};
    t.angle = 0.0;
    t.velocity = {};
    t.spin = 0.0;
    t.frozen = false;
    PlaygroundRoom::placeCorners(t);
    t.previous = t.position;
    t.previousCentre = t.centre;
    t.previousAngle = t.angle;
    r.ball_.position = {s.centre.x + 2.0, 0.12 + double(trial % 3) * 0.5, 0};
    r.ball_.velocity = {-2.0 - double(trial % 9), double(trial % 3) - 1.0, 0};
    r.ball_.spin = 0.0;
    r.ball_.frozen = false;
    r.ball_.previous = r.ball_.position;
}
const char* bodyName(std::size_t i) { return i == 0 ? "cube" : i == 1 ? "triangle" : i == 2 ? "plank" : "?"; }
}  // namespace

int main(int argc, char** argv) {
    setGpuRenderer(&renderOnHost);
    // The hunt: every contact the room writes, in every arrangement the seesaw sweep
    // makes, asked whether it points down onto the body it acts on.
    double worstDown = 0.0;
    std::size_t worstOwner = 0;
    int worstTrial = -1, worstTick = -1;
    Vec3 worstAt{}, worstForce{};
    int flagged = 0;
    for (int trial = 0; trial < 200; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(kDt);
        seat(r, trial);
        for (int k = 0; k < 300; ++k) {
            r.advance(kDt);
            for (std::size_t i = 0; i < r.forceCount_; ++i) {
                const auto& sample = r.forces_[i];
                if (sample.kind != PlaygroundRoom::ForceKind::Contact || sample.couple) continue;
                if (sample.owner >= PlaygroundRoom::kBodyCount) continue;
                // The axle's own answer is written at the pin, and a pin can push any
                // way it must: it is not a surface, and it is not what was wrong.
                const auto& body = r.bodies_[sample.owner];
                if (body.pivoted && length(sample.at - body.pivotPoint) < 1e-6) continue;
                // Whichever surface the place of the contact is on: the floor, or
                // another shape's outline. The force has to lie on the outward side of
                // it. A surface may push a body any way it likes -- a plank coming down
                // on a shape pushes it down, and that is physics -- but it may never
                // pull it in.
                Vec3 normal{0.0, 1.0, 0.0};
                bool against = sample.at.y <= 1e-3;
                if (!against) {
                    for (std::size_t other = 0; other < PlaygroundRoom::kBodyCount; ++other) {
                        if (other == sample.owner || r.bodies_[other].count == 0) continue;
                        const auto gap = ball_shape::outlineGap(sample.at, r.bodies_[other].position.data(),
                                                               r.bodies_[other].position.data(),
                                                               r.bodies_[other].count, 1.0);
                        if (std::abs(gap.gap) > 2e-3) continue;
                        normal = gap.normal;
                        against = true;
                        break;
                    }
                }
                if (!against) continue;
                const double inward = dot(sample.force, normal);
                if (inward < -1.0) {
                    ++flagged;
                    if (-inward > worstDown) {
                        worstDown = -inward;
                        worstOwner = sample.owner;
                        worstTrial = trial;
                        worstTick = k;
                        worstAt = sample.at;
                        worstForce = sample.force;
                    }
                }
            }
        }
    }
    std::printf("contacts pushing a body into the surface they act against, anywhere in the sweep:\n");
    if (flagged == 0) {
        std::printf("  none. 200 arrangements, 300 ticks each, every contact checked.\n");
    } else {
        std::printf("  %d of them, worst %.3f N inward on the %s at (%+.3f,%+.3f) F (%+.3f,%+.3f)  [trial %d tick %d]\n",
                    flagged, worstDown, bodyName(worstOwner), worstAt.x, worstAt.y, worstForce.x, worstForce.y,
                    worstTrial, worstTick);
    }
    // The worst one, taken apart: every surface near the place it acts, and every
    // contact the body was given that tick.
    if (worstTrial >= 0) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 10; ++k) r.advance(kDt);
        seat(r, worstTrial);
        for (int k = 0; k <= worstTick; ++k) r.advance(kDt);
        std::printf("\n  [trial %d, tick %d] the place (%+.4f,%+.4f), owner %s\n", worstTrial, worstTick, worstAt.x,
                    worstAt.y, bodyName(worstOwner));
        std::printf("    height above the floor %.4f m\n", worstAt.y);
        for (std::size_t other = 0; other < PlaygroundRoom::kBodyCount; ++other) {
            if (other == worstOwner || r.bodies_[other].count == 0) continue;
            const auto gap = ball_shape::outlineGap(worstAt, r.bodies_[other].position.data(),
                                                    r.bodies_[other].position.data(), r.bodies_[other].count, 1.0);
            std::printf("    to the %-8s  gap %+.4f m, its normal (%+.3f,%+.3f)\n", bodyName(other), gap.gap,
                        gap.normal.x, gap.normal.y);
        }
        for (std::size_t i = 0; i < r.forceCount_; ++i) {
            const auto& sm = r.forces_[i];
            if (sm.owner != worstOwner) continue;
            std::printf("    on it: %-8s at (%+.3f,%+.3f) F (%+.2f,%+.2f) N%s\n",
                        sm.couple ? "couple" : sm.kind == PlaygroundRoom::ForceKind::Gravity ? "gravity"
                                  : sm.kind == PlaygroundRoom::ForceKind::Contact ? "contact"
                                  : sm.kind == PlaygroundRoom::ForceKind::Friction ? "friction"
                                  : sm.kind == PlaygroundRoom::ForceKind::Pair ? "pair" : "other",
                        sm.at.x, sm.at.y, sm.force.x, sm.force.y, sm.couple ? " (moment)" : "");
        }
    }
    // And the state, drawn: the arrangement the arrow was found in, at the tick it was
    // found at, with the room's own force arrows and labels.
    const int trial = argc > 1 ? std::atoi(argv[1]) : (worstTrial >= 0 ? worstTrial : 4);
    const int until = argc > 2 ? std::atoi(argv[2]) : (worstTrial >= 0 ? worstTick : 40);
    PlaygroundRoom room;
    room.reset();
    for (int k = 0; k < 10; ++k) room.advance(kDt);
    seat(room, trial);
    for (int k = 0; k < until; ++k) room.advance(kDt);
    RoomFreecam cam;
    cam.reset({-3.0, 1.45, 3.05}, 0.02, -0.14);
    shoot(room, cam, "scratch/force/frame.png");
    std::printf("\nthe %s, as the room draws it:\n", "state");
    for (std::size_t i = 0; i < room.forceCount_; ++i) {
        const auto& s = room.forces_[i];
        if (s.owner >= PlaygroundRoom::kBodyCount) continue;
        std::printf("  %-8s %-8s at (%+.3f,%+.3f)  F (%+.2f,%+.2f) N%s\n", bodyName(s.owner),
                    s.couple ? "couple" : s.kind == PlaygroundRoom::ForceKind::Gravity ? "gravity"
                              : s.kind == PlaygroundRoom::ForceKind::Contact ? "contact"
                              : s.kind == PlaygroundRoom::ForceKind::Friction ? "friction"
                              : s.kind == PlaygroundRoom::ForceKind::Pair ? "pair" : "other",
                    s.at.x, s.at.y, s.force.x, s.force.y, s.couple ? "  (moment)" : "");
    }
    std::printf("picture: scratch/force/frame.png\n");
    return 0;
}
