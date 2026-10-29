#pragma once

#include "client/raster.h"
#include "client/renderer.h"
#include "client/skeleton_body.h"
#include "client/skeleton_lines.h"

#include <cstdint>
#include <utility>

#include <string>

namespace gtc::native {

// The test room: an empty box with a tiled floor and the two anatomical
// skeletons in it, so animation and rigging work has somewhere to happen.
// Both OpenSkeleton bodies use a source-derived, static T-pose. Neither body is scaled.
class RoomFreecam;

class SkeletonRoom {
public:
    bool load(const std::string& root, std::string& error);
    bool ready() const { return ready_; }
    std::size_t triangles() const { return sceneTriangles_; }
    const SkeletonModel& male() const { return male_; }
    const SkeletonModel& female() const { return female_; }
    bool lineView() const { return lineView_; }
    void toggleLineView() { lineView_ = !lineView_; }

    void render(NativeFrame& frame, const ViewState& view, double seconds, const RoomFreecam* freecam = nullptr);

private:
    SkeletonModel male_;
    SkeletonModel female_;
    SkeletonLines maleLines_, femaleLines_;
    GpuScene lineScene_;
    bool ready_ = false;
    bool lineView_ = false;
    bool lineSceneReady_ = false;
    std::string error_;
    // The room's geometry is kept between frames when the GPU path draws it:
    // two skeletons are three hundred thousand triangles, and re-sending them
    // every frame is the cost this path exists to remove.
    GpuScene scene_;
    std::uint64_t revision_ = 0;
    // Triangles in the cached scene. Printed by the room benchmark: if this
    // changes with the thread count, the split is losing or duplicating work.
    std::size_t sceneTriangles_ = 0;
    // Rest geometry is emitted once; a pose then costs one matrix per bone. The
    // pair is the batch index each bone owns, so a rebuild writes 56 matrices
    // and uploads nothing.
    bool restReady_ = false;
    std::vector<std::pair<std::uint32_t, std::size_t>> boneBatches_;
    std::vector<std::array<double, 16>> cachedMaleMatrices_;
    std::vector<std::array<double, 16>> cachedFemaleMatrices_;
    bool sceneReady_ = false;
    double cachedPose_ = -1.0;
};

} // namespace gtc::native
