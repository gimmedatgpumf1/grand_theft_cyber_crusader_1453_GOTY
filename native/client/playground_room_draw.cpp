#include "client/playground_room.h"
#include "client/playground_shapes.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gtc::native {
using namespace playground_detail;

namespace {
// The one time step in this room. Only the drawing half needs it, to work out
// how far through a tick a frame has got.
constexpr double kTick = 1.0 / 20.0;

// Scene object ids and the colour behind the room. Only drawing uses them.
constexpr std::uint32_t kGroundId = 1, kSurfaceId = 2, kPointId = 3, kTraceId = 4, kGhostId = 100;
constexpr std::uint32_t kBodyFaceId = 200, kBodySpan = 100, kBodyEdgeOffset = 10, kBodyCornerOffset = 20;
constexpr const char* kSky = "#101c22";
constexpr double kGhostRadius = 0.032, kTraceHalfWidth = 0.010;
constexpr double kCornerRadius = 0.045, kEdgeHalfWidth = 0.008;
constexpr std::uint32_t kBallId = 400, kBallMarkId = 401;
// One shaft and one head per arrow. The ids are well clear of the bodies' own,
// which run in hundreds from kBodyFaceId.
constexpr std::uint32_t kArrowId = 2000, kArrowKindStride = 40, kArrowHeadOffset = 20;
// How long a newton is, in metres of arrow. A corner weighs m*g = 9.8 N, so its
// weight is drawn a hand's width long; every other force is the same scale, which
// is the whole point of the arrows -- they are comparable by eye.
constexpr double kArrowMetresPerNewton = 0.030, kArrowHalfWidth = 0.010, kArrowHeadRadius = 0.045;
constexpr int kForceLabelHeight = 18;

// One arrow per drawn force: where it acts, where its tip is, and how hard it is.
struct Arrow {
    std::size_t kind = 0, slot = 0;
    Vec3 from{}, to{};
    double newtons = 0.0;
};

const char* kindColour(std::size_t kind) {
    switch (PlaygroundRoom::ForceKind(kind)) {
        case PlaygroundRoom::ForceKind::Gravity: return "#5aa9ff";
        case PlaygroundRoom::ForceKind::Contact: return "#7ce38b";
        case PlaygroundRoom::ForceKind::Pair: return "#ffb454";
        case PlaygroundRoom::ForceKind::Ball: return "#e07bd8";
        case PlaygroundRoom::ForceKind::Drag: return "#b9ccc4";
        case PlaygroundRoom::ForceKind::KindCount: break;
    }
    return "#ffffff";
}

// The ledger, as arrows. A force is written at the point it acts and its tip is
// the point plus the force itself, scaled -- so the direction is the force's own
// direction and the length is the force's own amount, and two forces can be
// compared by looking at them. Nothing here is smoothed or capped: a force too
// small to see is drawn too small to see, which is what it is.
std::vector<Arrow> forceArrows(const std::array<PlaygroundRoom::ForceSample, PlaygroundRoom::kMaxForceSamples>& forces,
                               std::size_t count) {
    std::array<std::size_t, std::size_t(PlaygroundRoom::ForceKind::KindCount)> used{};
    std::vector<Arrow> arrows;
    for (std::size_t i = 0; i < count; ++i) {
        const PlaygroundRoom::ForceSample& sample = forces[i];
        const std::size_t kind = std::size_t(sample.kind);
        if (kind >= used.size() || used[kind] >= PlaygroundRoom::kArrowsPerKind) continue;
        const double newtons = length(sample.force);
        if (!(newtons > 1e-6)) continue;
        Arrow arrow;
        arrow.kind = kind;
        arrow.slot = used[kind]++;
        arrow.from = sample.at;
        arrow.to = sample.at + sample.force * (kArrowMetresPerNewton / newtons);
        arrow.newtons = newtons;
        arrows.push_back(arrow);
    }
    return arrows;
}
} // namespace

// Everything the room draws, and the interpolation that decides where a body is
// on screen between two ticks. It is here rather than in the room itself so that
// neither file grows past the size a file is allowed to be; the simulation knows
// nothing about it.

void PlaygroundRoom::interpolate() {
    const double fraction = !sceneReady_ ? 1.0 : std::clamp(accumulator_ / kTick, 0.0, 1.0);
    const double pointAlpha = pointFrozen_ || dragTarget_ == DragTarget::Point ? 1.0 : fraction;
    shown_ = previous_ + (point_ - previous_) * pointAlpha;
    // Each body is interpolated on its own. A frozen body is drawn where it is,
    // but that must never stop a body that is still falling from being spread
    // over the frames: one shared switch here is exactly what made the point
    // snap from tick to tick again the moment the square settled.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        const double alpha = body.frozen || bodyHeld(index) ? 1.0 : fraction;
        interpolateBody(body, alpha);
    }
    // The ball is one point: where it is and how far it has turned, both spread
    // over the frames the same way as everything else.
    const double ballAlpha = ball_.frozen || dragTarget_ == DragTarget::Ball ? 1.0 : fraction;
    ball_.shown = ball_.previous + (ball_.position - ball_.previous) * ballAlpha;
    // What is drawn, not what is simulated. Writing this back into `angle` used
    // to divide every tick's turn by the number of frames in it, so at 300 FPS
    // the ball turned a fifteenth of the way it rolled and the seams looked
    // nailed on.
    ball_.shownAngle = ball_.previousAngle + (ball_.angle - ball_.previousAngle) * ballAlpha;
}

void PlaygroundRoom::interpolateBody(Body& body, double alpha) {
    // Interpolating corners straight from tick to tick pulls them along chords,
    // so a turning body looks squashed; where it is and how far round it has
    // turned is interpolated instead, and the corners rebuilt from those.
    if (alpha >= 1.0) {
        body.shown = body.position;
        return;
    }
    const Vec3 from = centroid(body.previous.data(), body.count);
    const Vec3 to = centroid(body.position.data(), body.count);
    const double fromAngle = edgeAngle(body.previous.data());
    double turn = edgeAngle(body.position.data()) - fromAngle;
    while (turn > 3.14159265358979323846) turn -= 2.0 * 3.14159265358979323846;
    while (turn < -3.14159265358979323846) turn += 2.0 * 3.14159265358979323846;
    const Vec3 centre = from + (to - from) * alpha;
    const double angle = fromAngle + turn * alpha;
    const double c = std::cos(angle), s = std::sin(angle);
    for (std::size_t i = 0; i < body.count; ++i) {
        body.shown[i] = {centre.x + body.local[i].x * c - body.local[i].y * s,
                         centre.y + body.local[i].x * s + body.local[i].y * c, 0};
    }
}

void PlaygroundRoom::buildScene(const Camera& camera) {
    Rasterizer raster(camera.width, camera.height, colorHex(kSky), camera.focal(), true);
    raster.setCamera(camera);
    std::vector<Vec3> points;
    std::vector<std::array<int, 3>> faces;

    // The room behind the demo: ground below the plane, the plane itself on top.
    quad(points, faces, -6, -4, 6, 0, -0.02);
    raster.setObject(kGroundId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#22302f"));

    points.clear();
    faces.clear();
    quad(points, faces, -6, -0.06, 6, 0.0, -0.01);
    raster.setObject(kSurfaceId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#b9ccc4"));

    // One marker per past tick: the 20 Hz cadence made visible.
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        disc(points, faces, kGhostRadius);
        raster.setObject(kGhostId + static_cast<std::uint32_t>(i), {0, 0}, 0);
        raster.mesh(points, faces, flat("#5c716c"));
    }

    // This tick's hitscan segment, and the point itself in front of it.
    points.clear();
    faces.clear();
    quad(points, faces, 0, -1, 1, 1, 0.004);
    raster.setObject(kTraceId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#6fc6ff"));

    disc(points, faces, kPointRadius);
    raster.setObject(kPointId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#ffd24a"));

    // Each body: a face, one edge marker per side, and a point at every corner
    // it is made of. The geometry is the body's rest shape and never changes;
    // where it is comes from the batch matrix, every frame, with no upload.
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        const std::uint32_t base = kBodyFaceId + static_cast<std::uint32_t>(index) * kBodySpan;
        face(points, faces, body.local.data(), body.count, -0.002);
        raster.setObject(base, {0, 0}, 0);
        raster.mesh(points, faces, flat("#2c5f74"));

        points.clear();
        faces.clear();
        quad(points, faces, 0, -1, 1, 1, 0.005);
        for (std::size_t i = 0; i < body.count; ++i) {
            raster.setObject(base + kBodyEdgeOffset + static_cast<std::uint32_t>(i), {0, 0}, 0);
            raster.mesh(points, faces, flat("#9fd8e8"));
        }
        disc(points, faces, kCornerRadius);
        for (std::size_t i = 0; i < body.count; ++i) {
            raster.setObject(base + kBodyCornerOffset + static_cast<std::uint32_t>(i), {0, 0}, 0);
            raster.mesh(points, faces, flat("#ff8a4c"));
        }
    }

    // The ball: a disc of its own radius, and a cross of two seams so the turning
    // can be seen. The seams are thin quads turned together by the ball's angle,
    // so the spin needs no geometry of its own either.
    points.clear();
    faces.clear();
    disc(points, faces, 1.0);
    raster.setObject(kBallId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#c8763a"));
    points.clear();
    faces.clear();
    quad(points, faces, -0.80, -0.09, 0.80, 0.09, 0.01);
    quad(points, faces, -0.09, -0.80, 0.09, 0.80, 0.01);
    raster.setObject(kBallMarkId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#3a2016"));

    // The forces: a unit bar along +x for the shaft and a unit disc for the head,
    // both placed by a model matrix every frame, exactly as the bodies' edges and
    // corners are. Every arrow exists in the scene from the first frame and is
    // moved off screen when there is no force to draw, so turning F on costs no
    // upload at all.
    for (std::size_t kind = 0; kind < std::size_t(ForceKind::KindCount); ++kind) {
        for (std::size_t slot = 0; slot < kArrowsPerKind; ++slot) {
            const std::uint32_t base = kArrowId + static_cast<std::uint32_t>(kind) * kArrowKindStride +
                                       static_cast<std::uint32_t>(slot);
            points.clear();
            faces.clear();
            quad(points, faces, 0, -1, 1, 1, 0.008);
            raster.setObject(base, {0, 0}, 0);
            raster.mesh(points, faces, flat(kindColour(kind)));
            disc(points, faces, 1.0);
            raster.setObject(base + kArrowHeadOffset, {0, 0}, 0);
            raster.mesh(points, faces, flat(kindColour(kind)));
        }
    }

    scene_ = raster.takeGpuScene();
    scene_.revision = 1;
    pointBatch_ = findBatch(scene_, kPointId);
    traceBatch_ = findBatch(scene_, kTraceId);
    ball_.faceBatch = findBatch(scene_, kBallId);
    ball_.markBatch = findBatch(scene_, kBallMarkId);
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        ghostBatches_[i] = findBatch(scene_, kGhostId + static_cast<std::uint32_t>(i));
    }
    for (std::size_t kind = 0; kind < std::size_t(ForceKind::KindCount); ++kind) {
        for (std::size_t slot = 0; slot < kArrowsPerKind; ++slot) {
            const std::uint32_t base = kArrowId + static_cast<std::uint32_t>(kind) * kArrowKindStride +
                                       static_cast<std::uint32_t>(slot);
            arrowShaft_[kind][slot] = findBatch(scene_, base);
            arrowHead_[kind][slot] = findBatch(scene_, base + kArrowHeadOffset);
        }
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        Body& body = bodies_[index];
        const std::uint32_t base = kBodyFaceId + static_cast<std::uint32_t>(index) * kBodySpan;
        body.faceBatch = findBatch(scene_, base);
        for (std::size_t i = 0; i < body.count; ++i) {
            body.edgeBatches[i] = findBatch(scene_, base + kBodyEdgeOffset + static_cast<std::uint32_t>(i));
            body.cornerBatches[i] = findBatch(scene_, base + kBodyCornerOffset + static_cast<std::uint32_t>(i));
        }
    }
}

void PlaygroundRoom::placeMarkers() {
    scene_.batches[pointBatch_].modelMatrix = translation(shown_);
    // The trace belongs to the tick that is being drawn: while the point is
    // still gliding through the last tick, the line runs from the previous
    // position to where that tick's travel was allowed to reach, so the point on
    // screen always sits on the line that was walked.
    const Vec3 traceStart = dragTarget_ == DragTarget::Point || !sceneReady_ ? traceFrom_ : previous_;
    scene_.batches[traceBatch_].modelMatrix =
        haveTrace_ ? segment(traceStart, traceTo_, kTraceHalfWidth) : translation({0, -1000, 0});
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        scene_.batches[ghostBatches_[i]].modelMatrix =
            i < ghostCount_ ? translation(ghosts_[i]) : translation({0, -1000, 0});
    }
    {
        const double radius = ball_.radius;
        // A unit disc scaled to the ball's radius, so a ball of a different size
        // needs no new geometry.
        scene_.batches[ball_.faceBatch].modelMatrix =
            {radius, 0, 0, 0, 0, radius, 0, 0, 0, 0, 1, 0, ball_.shown.x, ball_.shown.y, ball_.shown.z, 1};
        const double c = std::cos(ball_.shownAngle), s = std::sin(ball_.shownAngle);
        scene_.batches[ball_.markBatch].modelMatrix =
            {radius * c, radius * s, 0, 0, -radius * s, radius * c, 0, 0, 0, 0, 1, 0,
             ball_.shown.x, ball_.shown.y, ball_.shown.z, 1};
    }
    // The arrows: hidden unless F is on, and otherwise every force the ledger
    // holds, drawn at the point it acts with its own length. An arrow that is not
    // drawn is put under the floor rather than left where it was, so a stale force
    // cannot be mistaken for a live one.
    for (std::size_t kind = 0; kind < std::size_t(ForceKind::KindCount); ++kind) {
        for (std::size_t slot = 0; slot < kArrowsPerKind; ++slot) {
            scene_.batches[arrowShaft_[kind][slot]].modelMatrix = translation({0, -1000, 0});
            scene_.batches[arrowHead_[kind][slot]].modelMatrix = translation({0, -1000, 0});
        }
    }
    if (showForces_) {
        for (const Arrow& arrow : forceArrows(forces_, forceCount_)) {
            scene_.batches[arrowShaft_[arrow.kind][arrow.slot]].modelMatrix =
                segment(arrow.from, arrow.to, kArrowHalfWidth);
            scene_.batches[arrowHead_[arrow.kind][arrow.slot]].modelMatrix =
                {kArrowHeadRadius, 0, 0, 0, 0, kArrowHeadRadius, 0, 0, 0, 0, 1, 0,
                 arrow.to.x, arrow.to.y, arrow.to.z, 1};
        }
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        // The face is the rest shape placed where the drawn corners already are,
        // so what is filled in is exactly the outline the edges draw.
        const Vec3 middle = centroid(body.shown.data(), body.count);
        scene_.batches[body.faceBatch].modelMatrix = pose(middle, edgeAngle(body.shown.data()));
        for (std::size_t i = 0; i < body.count; ++i) {
            scene_.batches[body.edgeBatches[i]].modelMatrix =
                segment(body.shown[i], body.shown[(i + 1) % body.count], kEdgeHalfWidth);
            scene_.batches[body.cornerBatches[i]].modelMatrix = translation(body.shown[i]);
        }
    }
}

// The numbers on N: every arrow's force in newtons, written beside its tip, and
// the room's energy budget above them. The energy is the check on everything
// else -- it may be moved between speed and height by a tick, and it may never
// grow -- so it is shown next to the forces that are supposed to account for it.
void PlaygroundRoom::appendForceLabels(const Camera& camera, NativeFrame& frame) {
    forceLabels_.clear(camera.width, camera.height);
    if (!showNewtons_) return;
    // The words go into a layer of their own, laid out by the same glyph code the
    // rest of the interface uses: the frame is borrowed for the moment it takes to
    // fill the layer and given back untouched.
    UiLayer* const borrowed = frame.ui;
    frame.ui = &forceLabels_;
    const double focal = camera.focal();
    const auto project = [&](Vec3 point, double& x, double& y) {
        const Vec3 inView = camera.view(point);
        const double depth = inView.z > 1e-3 ? inView.z : 1e-3;
        x = camera.width * 0.5 + focal * inView.x / depth;
        y = camera.height * 0.5 - focal * inView.y / depth;
        return inView.z > 1e-3;
    };
    char number[64];
    for (const Arrow& arrow : forceArrows(forces_, forceCount_)) {
        double x = 0.0, y = 0.0;
        if (!project(arrow.to, x, y)) continue;
        std::snprintf(number, sizeof(number), "%.1f N", arrow.newtons);
        drawTextPixels(frame, static_cast<int>(std::lround(x)) + 6, static_cast<int>(std::lround(y)) - 22,
                       number, colorHex(kindColour(arrow.kind)), kForceLabelHeight);
    }
    double kinetic = 0.0, potential = 0.0;
    energyBudget(kinetic, potential);
    std::snprintf(number, sizeof(number), "E %.2f J   KE %.2f   PE %.2f", kinetic + potential, kinetic, potential);
    drawTextPixels(frame, 14, 12, number, colorHex("#e0effb"), kForceLabelHeight + 2);
    frame.ui = borrowed;
}

bool PlaygroundRoom::render(NativeFrame& frame, const ViewState& view, const RoomFreecam& freecam) {
    const Camera camera = freecam.camera(view.width, view.height, view.gpu);
    if (!sceneReady_) {
        buildScene(camera);
        sceneReady_ = true;
    }
    interpolate();
    placeMarkers();
    scene_.width = camera.width;
    scene_.height = camera.height;
    scene_.focal = camera.focal();
    scene_.viewMatrix = camera.viewMatrix();
    scene_.covered = false;
    // The force labels are laid out first and batched with the world, so the
    // numbers sit on the arrows they belong to instead of in a corner of the
    // screen.
    appendForceLabels(camera, frame);
    const auto count = scene_.batches.size();
    if (!forceLabels_.empty()) appendUiBatches(forceLabels_, camera, scene_);
    frame.interfaceInFrame = false;
    if (frame.uiLayer && !frame.uiLayer->empty()) {
        appendUiBatches(*frame.uiLayer, camera, scene_);
        frame.interfaceInFrame = true;
    }
    frame.width = camera.width;
    frame.height = camera.height;
    frame.wantsBgra = true;
    frame.rgb.clear();
    const bool ok = renderGpuScene(scene_, frame);
    scene_.batches.resize(count);
    return ok;
}
} // namespace gtc::native
