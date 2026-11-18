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
// The ball is drawn in front of every solid thing in the room, and its seams in
// front of the ball. Not for looks: the edge stroke is 16 mm wide and the corner
// dots are 90 mm discs, both of them decoration that reaches outside the outline
// they belong to, and a ball resting exactly against a face is a ball whose disc
// those two things cover. Measured on the King's picture: the light edge of the
// triangle ran across the ball's orange disc and the corner dots sat on top of it,
// and the picture therefore said the ball was inside a shape the physics had it
// exactly touching. Nothing in the room can be drawn over the ball now.
//
// Arrows keep their own depth in front of the ball: they are annotations about the
// solid, not solid things, and a force arrow hidden behind the ball it acts on is
// an arrow nobody can read.
constexpr double kBallZ = 0.015, kBallMarkZ = 0.016;
// The seesaw's axle, drawn as a post of this half-width and a pin of this radius.
constexpr double kPivotPostHalf = 0.055, kPivotPinRadius = 0.075;
// The bodies run 200, 300, 400 ..., so the ball has to start beyond the last of
// them -- three bodies now reach 500, and a ball sharing a body's id draws the ball
// where that body is and leaves the body itself at the last frame it was placed at.
// Two balls, so two ids each: the disc and the seams, side by side in the numbering.
constexpr std::uint32_t kBallId = 600, kBallSpan = 2;
constexpr std::uint32_t ballId(std::size_t index) { return kBallId + static_cast<std::uint32_t>(index) * kBallSpan; }
// The seesaw's axle: a post from the floor to the pin, and the pin itself.
constexpr std::uint32_t kPivotPostId = 700, kPivotPinId = 701;
// The rope and the plate it is tied to. There is no bob in this list: the thing on the
// end of the rope is a ball, drawn by the ball's own batch, at the ball's own depth.
// Depths: the rope and its anchor go behind the balls, because the ball is the one
// thing in the room that nothing may be drawn over.
constexpr std::uint32_t kRopeId = 800, kRopeSpan = 2;
constexpr std::uint32_t ropeId(std::size_t i) { return kRopeId + std::uint32_t(i) * kRopeSpan; }
constexpr std::uint32_t anchorId(std::size_t i) { return ropeId(i) + 1; }
constexpr double kRopeHalfWidth = 0.014, kAnchorRadius = 0.075;
constexpr double kRopeZ = 0.010, kAnchorZ = 0.009;
// One shaft and one head per arrow. The ids are well clear of the bodies' own,
// which run in hundreds from kBodyFaceId.
// The ids are laid out from the room's own count of arrows per kind, so a change
// to that number cannot silently walk one kind's heads into the next kind's
// shafts: each kind owns twice that many ids, the shafts first and then the heads.
constexpr std::uint32_t kArrowId = 2000, kArrowHeadOffset = PlaygroundRoom::kArrowsPerKind,
                        kArrowKindStride = 2 * PlaygroundRoom::kArrowsPerKind;
// How long a newton is, in metres of arrow. A corner weighs m*g = 9.8 N, so its
// weight is drawn a hand's width long; every other force is the same scale, which
// is the whole point of the arrows -- they are comparable by eye.
constexpr double kArrowMetresPerNewton = 0.030, kArrowHalfWidth = 0.010;
// The head is a share of the arrow's own length, not a fixed size: a long arrow
// gets a long head and a short one a short head, so a small force still reads as
// an arrow and not as a head with no shaft. Both are fractions of the drawn
// length, which is the force, so nothing here is a separate scale to keep in step.
constexpr double kArrowHeadShare = 0.34, kArrowHeadHalfShare = 0.42;
// In front of everything the room draws, the ball's seams and the bodies' corners
// included. A force that acts at a point inside a ball is still drawn where it
// acts, and it has to be visible there: an arrow buried inside the shape it is
// pushing is an arrow nobody can read.
constexpr double kArrowZ = 0.02;
// One depth per kind of solid thing, in the order they are meant to appear, so that
// nothing in the room can win a tie against the ball.
//
// The renderer keeps the *first* thing drawn at a depth, not the last (`depth >=` is
// a discard), so two batches at the same depth are decided by the order the scene was
// built in -- which is the order of the code, not of the picture. The corner dots were
// at exactly the ball's depth and the edge strokes a hair in front of it, and both are
// drawn before the ball: a ball resting in a corner therefore had a 90 mm dot and a
// 16 mm stroke painted over its own disc, and the King's screenshot shows exactly that
// -- the light edge of the triangle running across the ball as a chord, with a corner
// dot sitting on it. Nothing was wrong with where the ball was; something was wrong
// with what was painted on top of it.
//
// So every solid thing has a depth of its own, all of them behind the ball, and the
// ball has its own in front of all of them. Arrows stay in front of the ball because
// they are annotations about it and not solid things.
constexpr double kFloorZ = -0.010, kFaceZ = -0.008, kEdgeZ = -0.006, kDotZ = -0.004;
constexpr double kGhostZ = -0.003, kPinPostZ = -0.009, kPinZ = -0.002;
constexpr int kForceLabelHeight = 20;

// One arrow per drawn force: where it acts, where its tip is, and how hard it is.
struct Arrow {
    std::size_t kind = 0, slot = 0;
    Vec3 from{}, to{};   // where it acts, and the tip of the head
    Vec3 neck{};         // where the shaft ends and the head begins
    double newtons = 0.0;
};

const char* kindColour(std::size_t kind) {
    switch (PlaygroundRoom::ForceKind(kind)) {
        case PlaygroundRoom::ForceKind::Gravity: return "#5aa9ff";
        case PlaygroundRoom::ForceKind::Contact: return "#7ce38b";
        // Friction, in its own colour: the force along the surface, so a shape being
        // held on a slope and a shape sliding down one both show what is doing it.
        case PlaygroundRoom::ForceKind::Friction: return "#ffe066";
        // A contact between two shapes is a normal force, and it is drawn in the
        // colour of one. It had a colour of its own, and the King asked where the
        // normal forces had gone: they were there, in a different orange, at the one
        // place he was looking for them. A normal force is a normal force whatever it
        // is pressed against, and the colour says what kind of force it is and nothing
        // else. That the room's contacts come in pairs is not a colour: it is the two
        // arrows, equal and opposite, one on each shape, which is what is on screen.
        case PlaygroundRoom::ForceKind::Pair: return "#7ce38b";
        case PlaygroundRoom::ForceKind::Ball: return "#e07bd8";
        case PlaygroundRoom::ForceKind::Drag: return "#b9ccc4";
        // Rolling resistance, apart from friction and in its own colour, because
        // it is not friction: it is the ground pushing a little ahead of the
        // bottom of the ball, and it is what stops a ball rolling for ever.
        case PlaygroundRoom::ForceKind::Rolling: return "#d98c5f";
        // The rope's pull, in the colour of rope. It is the one force here that is
        // neither a surface nor the air, and the only one that can be nothing at all:
        // a slack rope writes no arrow, and a taut one writes two, at the bob and at
        // the anchor, along the rope between them.
        case PlaygroundRoom::ForceKind::Rope: return "#f0e2c0";
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
        // The tip is the tail plus the force itself, scaled: the arrow's direction
        // is the force's direction and its length is the force's amount times one
        // number for the whole room, so 9.8 N is always the same arrow however many
        // forces are on screen and two of them can be compared by eye.
        const Vec3 direction = sample.force * (1.0 / newtons);
        arrow.to = sample.at + sample.force * kArrowMetresPerNewton;
        arrow.neck = arrow.to - direction * (length(arrow.to - arrow.from) * kArrowHeadShare);
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

// Ticks happen twenty times a second and frames happen as often as the screen
// allows, so a tick's worth of motion is spread over every frame that falls in
// it. The screen shows the point between the position it had at the previous
// tick and the position the last tick left it in, at the fraction of a tick the
// frame has reached. Nothing is moved by the renderer; this is only what is
// drawn, exactly as Minecraft spreads a 20 Hz tick over rendered frames.
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
    // Every ball, one at a time. Both of them are balls -- the one hanging from the
    // rope is not another kind of thing, it is this kind of thing -- so there is one
    // interpolator and it is asked for each in turn.
    for (std::size_t index = 0; index < kBallCount; ++index) {
        Ball& ball = balls_[index];
        const double alpha =
            ball.frozen || ballHeld(ball) ? 1.0 : fraction;
        interpolateBall(ball, alpha);
    }
}

// Where a ball's own tick had it at a fraction of the tick. One function, because the
// picture has to be able to ask for a fraction *earlier* than the frame's own when the
// frame's own is under something -- see below.
Vec3 PlaygroundRoom::ballPlaceAt(const Ball& ball, double t) {
    if (t >= 1.0) return ball.position;
    if (!ball.bent) return ball.previous + (ball.position - ball.previous) * t;
    return t <= ball.bendAt ? ball.previous + ball.bendIn * (kTick * t)
                            : ball.bendPoint + ball.bendOut * (kTick * (t - ball.bendAt));
}

// One ball, on the frame: where it is, how far it has turned, and -- the one question
// the picture has to ask that the physics does not -- whether drawing it there would
// show a place the room never allowed.
//
// Two straight pieces when it bent, one when it did not. This is not the drawing
// inventing a path: it is the path the tick took. The ball went to the meeting
// at one speed and left it at another, and the only place the two pieces of that
// motion join is the place it touched. Spreading the tick as a single line put
// the join halfway up the rebound instead, which is why a ball landing fast
// looked like it bounced on air a hand's width above the cube.
void PlaygroundRoom::interpolateBall(Ball& ball, double fraction) {
    // One function for the place at a fraction of the tick (ballPlaceAt above), because
    // the picture has to be able to ask for a fraction *earlier* than the frame's own
    // when the frame's own is under a surface -- see below.
    // The picture may not show a configuration the physics never had.
    //
    // A tick's travel is a straight line for the ball and a straight line for each
    // corner of a shape, and two straight lines do not keep a circle and a face the
    // same distance apart: at the end of every tick the ball is on or outside every
    // outline -- the walk and the separation are what make that true -- but at a
    // fraction of a tick between the two, which is what the screen draws sixty to
    // three hundred times a second, the two chords can cross and the ball is drawn
    // inside. Measured in the King's corner: 1.2e-4 m. A tenth of a millimetre is
    // invisible, and it is still a lie, and the King is looking at pixels.
    //
    // The cure is to keep the drawn ball on the path its own tick took. That path is
    // already known exactly -- it is the two straight pieces the interpolation is
    // built from -- so the drawn place is the *latest* point of it that is not deeper
    // inside anything than the place the room itself left, found by halving the
    // fraction until that point is pinned down to a part in 2^40 of a tick. Nothing
    // here is a step size or a tolerance: it is a root, found the way the room finds
    // where a walk stops being clear.
    //
    // Why not push the ball sideways off a surface instead, which is what this used
    // to do? Because a push moves the drawn ball off its own path, and two surfaces
    // closing on it push it out of one and into the other until the push gives up and
    // the frame is drawn at the newest tick -- a jump forward of up to a tick's
    // travel, then a jump back on the next frame. That is what made the ball look
    // laggy in a corner, and it is measurable: in the King's corner the drawn ball
    // was thrown off its path by 5e-2 m on some frames, and then teleported. Walking
    // back along the path cannot do that: every place it can choose is a place the
    // tick really passed through.
    // The picture may not show a configuration the physics never had.
    //
    // A tick's travel is a straight line for the ball and a straight line for each
    // corner of a shape, and two straight lines do not keep a circle and a face the
    // same distance apart: at the end of every tick the ball is on or outside every
    // outline -- the walk and the separation are what make that true -- but at a
    // fraction of a tick between the two, which is what the screen draws sixty to
    // three hundred times a second, the two chords can cross and the ball is drawn
    // inside. Measured in the King's corner: 1.2e-4 m. A tenth of a millimetre is
    // invisible, and it is still a lie, and the King is looking at pixels.
    //
    // The cure is to keep the drawn ball on the path its own tick took. That path is
    // already known exactly -- it is the two straight pieces the interpolation is
    // built from -- so the drawn place is the *latest* point of it that is not deeper
    // inside anything than the place the room itself left, found by halving the
    // fraction until that point is pinned down to a part in 2^40 of a tick. Nothing
    // here is a step size or a tolerance: it is a root, found the way the room finds
    // where a walk stops being clear.
    //
    // Why not push the ball sideways off a surface instead, which is what this used
    // to do? Because a push moves the drawn ball off its own path, and two surfaces
    // closing on it push it out of one and into the other until the push gives up and
    // the frame is drawn at the newest tick -- a jump forward of up to a tick's
    // travel, then a jump back on the next frame. That is what made the ball look
    // laggy in a corner, and it is measurable: in the King's corner the drawn ball
    // was thrown off its path by 5e-2 m on some frames, and then teleported. Walking
    // back along the path cannot do that: every place it can choose is a place the
    // tick really passed through.
    const auto depthInside = [&](const Vec3& where, double atFraction) {
        double inside = ball.radius - where.y;
        for (std::size_t index = 0; index < kBodyCount; ++index) {
            const Body& body = bodies_[index];
            if (body.count == 0) continue;
            const ball_shape::OutlineGap gap =
                ball_shape::outlineGap(where, body.shown.data(), body.shown.data(), body.count, 1.0);
            inside = std::max(inside, ball.radius - gap.gap);
        }
        // And the other ball, which is a solid too: two balls drawn through each other
        // are the same lie as a ball drawn through a cube. Its place is read off its own
        // path at the same fraction of the same tick, so the pair is drawn as the pair
        // the tick had.
        for (const Ball& other : balls_) {
            if (&other == &ball) continue;
            const Vec3 there = ballPlaceAt(other, atFraction);
            const double apart = length(Vec3{where.x - there.x, where.y - there.y, 0.0});
            inside = std::max(inside, ball.radius - (apart - other.radius));
        }
        return inside;
    };
    // Two different questions, and they were being asked as one.
    //
    // "Is this place inside something?" is the room's own question, and the room has
    // always answered it with a band: a ball at exactly one radius from a face is
    // *touching* it, and the arithmetic that puts it there reads a fraction of a
    // micrometre either side of zero from tick to tick. The physics calls a place
    // inside only when it is deeper than that touch (`ball_shape::kTouch`), never when
    // it is 1.11e-16 m inside. Asking it the drawing's old way -- deeper than zero --
    // meant that every place of a sliding ball's path counted as inside, the draw-walk
    // ran back to where the tick began, and the ball was drawn a whole tick, 50 ms,
    // behind the room on every frame of a slide down the triangle. That is the lag.
    //
    // "How deep may it be drawn?" is a separate question, and the answer is not the
    // touch band: it is exactly as deep as the place the room itself left the ball, in
    // that same frame, and no deeper.
    // The place the frame is entitled to, before anything is asked about it.
    ball.shown = ballPlaceAt(ball, fraction);
    // `body.shown` is the frame's interpolated pose, not the end-of-tick pose.
    // Comparing the ball's end position against that pose can grant a penetration
    // allowance that the physics never granted in the actual end-of-tick shapes.
    // The presentation must remain clear of the shapes it draws, so its allowance
    // is zero (within the geometry's existing contact tolerance).
    // Leave a ten-nanometre outward margin so the final interpolation/root
    // rounding cannot turn a mathematically touching pose into a visible overlap.
    const double allowed = -1.0e-8;
    double drawnAt = fraction;
    if (sceneReady_ && depthInside(ballPlaceAt(ball, drawnAt), drawnAt) > ball_shape::kTouch) {
        if (depthInside(ballPlaceAt(ball, 0.0), 0.0) > allowed) {
            // Not one place of this tick's path is as clear as the place the room ends
            // on: something has been drawn over the ground the ball already covered.
            // The room's own place is then the best there is, and it is legal by the
            // room's own measure.
            ball.shown = ballPlaceAt(ball, 1.0);
            drawnAt = 1.0;
        } else {
            // The latest place on this tick's path that is as clear as the room's own,
            // found by halving until it is pinned down to a part in 2^40 of a tick. Not
            // a step size and not a tolerance: the root of "where does this path stop
            // being deeper inside than the room left the ball", found the way the room
            // finds where a walk stops being clear.
            // Contact paths can enter, leave, and re-enter a moving outline in one
            // tick. A single bisection from tick start assumes one crossing and can
            // converge on an earlier crossing while the requested frame lies inside
            // a later one. Walk backward from the requested frame to bracket its
            // nearest clear point, then refine just that crossing.
            double inside = drawnAt;
            double clear = -1.0;
            constexpr int kBrackets = 256;
            for (int sample = 1; sample <= kBrackets; ++sample) {
                const double candidate = drawnAt * (1.0 - double(sample) / kBrackets);
                if (depthInside(ballPlaceAt(ball, candidate), candidate) <= allowed) {
                    clear = candidate;
                    break;
                }
                inside = candidate;
            }
            if (clear < 0.0) {
                drawnAt = 1.0;
            } else {
                for (int pass = 0; pass < 40; ++pass) {
                    const double middle = 0.5 * (clear + inside);
                    if (depthInside(ballPlaceAt(ball, middle), middle) > allowed) inside = middle;
                    else clear = middle;
                }
                drawnAt = clear;
            }
            ball.shown = ballPlaceAt(ball, drawnAt);
        }
    }
    // What is drawn, not what is simulated. Writing this back into `angle` used
    // to divide every tick's turn by the number of frames in it, so at 300 FPS
    // the ball turned a fifteenth of the way it rolled and the seams looked
    // nailed on.
    // The turn goes with the place; a ball drawn a little earlier in its tick must
    // not have turned all the way through it.
    ball.shownAngle = ball.previousAngle + (ball.angle - ball.previousAngle) * drawnAt;
}

void PlaygroundRoom::interpolateBody(Body& body, double alpha) {
    // What is drawn is a body between the place and the turn it had at the last
    // tick and the place and turn the last tick left it in. Corners are read off
    // that, never interpolated themselves: two corners of a turning body do not
    // travel in straight lines, and drawing them as if they did squashes it.
    if (alpha >= 1.0) {
        body.shown = body.position;
        return;
    }
    const Vec3 centre = body.previousCentre + (body.centre - body.previousCentre) * alpha;
    const double angle = body.previousAngle + (body.angle - body.previousAngle) * alpha;
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
    quad(points, faces, -6, -0.06, 6, 0.0, kFloorZ);
    raster.setObject(kSurfaceId, {0, 0}, 0);
    raster.mesh(points, faces, flat("#b9ccc4"));

    // The seesaw's axle: a post from the floor up to the pin, and the pin itself.
    // It is scenery and not a body -- it never moves, so it is drawn once and left --
    // but it is the thing the plank is held by, and a plank floating on nothing would
    // be a lie about what is holding it up. The pin's own reaction to whatever stands
    // on the plank goes into the ground, which is where a real axle's frame takes it.
    // In dot-only mode the seesaw is not in the room, so neither is its axle.
    if (!dotOnly_ && PlaygroundRoom::kBodyCount > PlaygroundRoom::kSeesaw) {
        const auto& plank = kShapes[PlaygroundRoom::kSeesaw];
        const double px = plank.middle.x, py = plank.middle.y;
        points.clear();
        faces.clear();
        quad(points, faces, px - kPivotPostHalf, 0.0, px + kPivotPostHalf, py, kPinPostZ);
        raster.setObject(kPivotPostId, {0, 0}, 0);
        raster.mesh(points, faces, flat("#4d6e6a"));
        // The pin itself is drawn after the bodies (see below): it sits in the middle
        // of the plank, and a pin drawn under the plank is a pin nobody sees.
    }

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
        if (body.count == 0) continue;  // not in the room: no geometry at all
        const std::uint32_t base = kBodyFaceId + static_cast<std::uint32_t>(index) * kBodySpan;
        face(points, faces, body.local.data(), body.count, kFaceZ);
        raster.setObject(base, {0, 0}, 0);
        raster.mesh(points, faces, flat("#2c5f74"));

        points.clear();
        faces.clear();
        quad(points, faces, 0, -1, 1, 1, 0.0);
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

    // The pin in the middle of the plank, last of the solid things so that it is
    // drawn over the plank it goes through and not under it.
    if (!dotOnly_ && PlaygroundRoom::kBodyCount > PlaygroundRoom::kSeesaw) {
        const auto& plank = kShapes[PlaygroundRoom::kSeesaw];
        // A small square of the axle's own metal, in the middle of the plank and in
        // front of it: the one thing on screen that says the plank is held there.
        points.clear();
        faces.clear();
        quad(points, faces, plank.middle.x - kPivotPinRadius, plank.middle.y - kPivotPinRadius,
             plank.middle.x + kPivotPinRadius, plank.middle.y + kPivotPinRadius, kPinZ);
        raster.setObject(kPivotPinId, {0, 0}, 0);
        raster.mesh(points, faces, flat("#e8e2c9"));
    }

    // The rope and the plate it is tied to. The rope is one quad along its own length,
    // so its ends follow the anchor and the ball on the end of it every frame without
    // any geometry being rebuilt: it is as long as the distance between them, which is
    // what a rope is. Nothing else is drawn for the hanging end, because the hanging
    // end is a ball and the balls are drawn next.
    points.clear();
    faces.clear();
    quad(points, faces, 0, -1, 1, 1, 0.0);
    for (std::size_t i = 0; i < kRopeCount; ++i) {
        raster.setObject(ropeId(i), {0, 0}, 0);
        raster.mesh(points, faces, flat("#c9b48a"));
        points.clear(); faces.clear(); disc(points, faces, kAnchorRadius);
        raster.setObject(anchorId(i), {0, 0}, 0);
        raster.mesh(points, faces, flat("#8d9aa0"));
        points.clear(); faces.clear(); quad(points, faces, 0, -1, 1, 1, 0.0);
    }

    // Every ball: a disc of its own radius, and a cross of two seams so the turning can
    // be seen. The seams are thin quads turned together by the ball's angle, so the
    // spin needs no geometry of its own either. The same two batches per ball, in the
    // same order, for both of them: one is on a rope and one is not, and that is a fact
    // about the rope.
    for (std::size_t index = 0; index < kBallCount; ++index) {
        if (!balls_[index].active) continue;  // not in the room: no geometry at all
        const std::uint32_t base = ballId(index);
        points.clear();
        faces.clear();
        disc(points, faces, 1.0);
        raster.setObject(base, {0, 0}, 0);
        raster.mesh(points, faces, flat("#c8763a"));
        points.clear();
        faces.clear();
        quad(points, faces, -0.80, -0.09, 0.80, 0.09, 0.01);
        quad(points, faces, -0.09, -0.80, 0.09, 0.80, 0.01);
        raster.setObject(base + 1, {0, 0}, 0);
        raster.mesh(points, faces, flat("#3a2016"));
    }

    // The forces: a unit bar along +x for the shaft and a unit triangle for the head,
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
            quad(points, faces, 0, -1, 1, 1, kArrowZ);
            raster.setObject(base, {0, 0}, 0);
            raster.mesh(points, faces, flat(kindColour(kind)));
            // A point on the end of it, not a disc: the tip of the arrow is where
            // the force ends, and a round end says nothing about which way it is
            // going or where it stops.
            triangle(points, faces, kArrowZ);
            raster.setObject(base + kArrowHeadOffset, {0, 0}, 0);
            raster.mesh(points, faces, flat(kindColour(kind)));
        }
    }

    scene_ = raster.takeGpuScene();
    scene_.revision = 1;
    pointBatch_ = findBatch(scene_, kPointId);
    traceBatch_ = findBatch(scene_, kTraceId);
    for (std::size_t i = 0; i < kRopeCount; ++i) {
        ropeForBall(i + 1).ropeBatch = findBatch(scene_, ropeId(i));
        ropeForBall(i + 1).anchorBatch = findBatch(scene_, anchorId(i));
    }
    for (std::size_t index = 0; index < kBallCount; ++index) {
        balls_[index].faceBatch = findBatch(scene_, ballId(index));
        balls_[index].markBatch = findBatch(scene_, ballId(index) + 1);
    }
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
            i < ghostCount_ ? translation(Vec3{ghosts_[i].x, ghosts_[i].y, kGhostZ})
                            : translation({0, -1000, 0});
    }
    for (std::size_t i = 0; i < kRopeCount; ++i) {
        const Rope& rope = ropeForBall(i + 1);
        const Ball& hanging = balls_[rope.ball < kBallCount ? rope.ball : kThrowBall];
        const Vec3 at = hanging.shown, anchor = rope.anchor;
        scene_.batches[rope.ropeBatch].modelMatrix =
            rope.attached ? segment({anchor.x, anchor.y, kRopeZ}, {at.x, at.y, kRopeZ}, kRopeHalfWidth)
                          : translation({0, -1000, 0});
        scene_.batches[rope.anchorBatch].modelMatrix =
            rope.attached ? translation({anchor.x, anchor.y, kAnchorZ}) : translation({0, -1000, 0});
    }
    // Every ball, at the radius it has and the turn it was drawn with. A unit disc
    // scaled to the ball's radius, so a ball of a different size needs no new geometry
    // -- and there is nothing here that knows which ball is on a rope.
    for (const Ball& ball : balls_) {
        if (!ball.active) continue;  // not in the room: nothing was built for it
        const double radius = ball.radius;
        scene_.batches[ball.faceBatch].modelMatrix =
            {radius, 0, 0, 0, 0, radius, 0, 0, 0, 0, 1, 0, ball.shown.x, ball.shown.y, kBallZ, 1};
        const double c = std::cos(ball.shownAngle), s = std::sin(ball.shownAngle);
        scene_.batches[ball.markBatch].modelMatrix =
            {radius * c, radius * s, 0, 0, -radius * s, radius * c, 0, 0, 0, 0, 1, 0,
             ball.shown.x, ball.shown.y, kBallMarkZ, 1};
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
            const double drawn = length(arrow.to - arrow.from);
            const double head = drawn * kArrowHeadShare;
            scene_.batches[arrowShaft_[arrow.kind][arrow.slot]].modelMatrix =
                segment(arrow.from, arrow.neck, kArrowHalfWidth);
            scene_.batches[arrowHead_[arrow.kind][arrow.slot]].modelMatrix =
                arrowHead(arrow.neck, arrow.to, head * kArrowHeadHalfShare);
        }
    }
    for (std::size_t index = 0; index < kBodyCount; ++index) {
        const Body& body = bodies_[index];
        if (body.count == 0) continue;  // not in the room: nothing was built for it
        // The face is the rest shape placed where the drawn corners already are,
        // so what is filled in is exactly the outline the edges draw.
        const Vec3 middle = centroid(body.shown.data(), body.count);
        scene_.batches[body.faceBatch].modelMatrix = pose(middle, edgeAngle(body.shown.data()));
        for (std::size_t i = 0; i < body.count; ++i) {
            // The stroke is sixteen millimetres wide and it used to be centred on the
            // outline, so eight of them were outside the shape -- exactly the eight a
            // ball lying against the face has to put its own surface in. Drawn inside,
            // with its outer edge on the outline, the stroke stops where the shape
            // stops. It is the same outline either way; one of the two ways leaves the
            // room's own picture claiming an overlap the room does not have.
            const Vec3 a = body.shown[i];
            const Vec3 b = body.shown[(i + 1) % body.count];
            const Vec3 side = b - a;
            const double span = length(side);
            Vec3 inward{0, 1, 0};
            if (span > 1e-9) {
                const Vec3 outward{side.y / span, -side.x / span, 0};
                inward = dot(middle - a, outward) > 0.0 ? outward : outward * -1.0;
            }
            const Vec3 shift = inward * kEdgeHalfWidth;
            scene_.batches[body.edgeBatches[i]].modelMatrix =
                segment(Vec3{a.x + shift.x, a.y + shift.y, kEdgeZ}, Vec3{b.x + shift.x, b.y + shift.y, kEdgeZ},
                        kEdgeHalfWidth);
            scene_.batches[body.cornerBatches[i]].modelMatrix =
                translation(Vec3{body.shown[i].x, body.shown[i].y, kDotZ});
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
