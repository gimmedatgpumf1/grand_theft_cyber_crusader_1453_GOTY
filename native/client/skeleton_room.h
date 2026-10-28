#pragma once

#include "client/raster.h"
#include "client/renderer.h"
#include "client/skeleton_body.h"

#include <cstdint>

#include <string>

namespace gtc::native {

// The test room: an empty box with a tiled floor and the two anatomical
// skeletons standing in it, so animation and rigging work has somewhere to
// happen without a server, a citizen, or a toilet.
//
// The skeletons are drawn at their source heights -- 1.696 m and 1.713 m -- and
// nothing here scales them. A body that is the wrong size for the room gets a
// taller room, not a shorter body.
class SkeletonRoom {
public:
    bool load(const std::string& root, std::string& error);
    bool ready() const { return male_.loaded() && female_.loaded(); }
    const SkeletonModel& male() const { return male_; }
    const SkeletonModel& female() const { return female_; }

    void render(NativeFrame& frame, const ViewState& view, double seconds);

private:
    SkeletonModel male_;
    SkeletonModel female_;
    std::string error_;
    // The room's geometry is kept between frames when the GPU path draws it:
    // two skeletons are three hundred thousand triangles, and re-sending them
    // every frame is the cost this path exists to remove.
    GpuScene scene_;
    std::uint64_t revision_ = 0;
    bool sceneReady_ = false;
    double cachedPose_ = -1.0;
    int cachedWidth_ = 0;
    int cachedHeight_ = 0;
};

} // namespace gtc::native
