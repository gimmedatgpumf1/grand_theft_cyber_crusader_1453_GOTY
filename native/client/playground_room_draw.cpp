#include "client/playground_room.h"
#include "client/playground_shapes.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cmath>
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
constexpr double kPointRadius = 0.07, kGhostRadius = 0.032, kTraceHalfWidth = 0.010;
constexpr double kCornerRadius = 0.045, kEdgeHalfWidth = 0.008;
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

    scene_ = raster.takeGpuScene();
    scene_.revision = 1;
    pointBatch_ = findBatch(scene_, kPointId);
    traceBatch_ = findBatch(scene_, kTraceId);
    for (std::size_t i = 0; i < ghostBatches_.size(); ++i) {
        ghostBatches_[i] = findBatch(scene_, kGhostId + static_cast<std::uint32_t>(i));
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
    const auto count = scene_.batches.size();
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
