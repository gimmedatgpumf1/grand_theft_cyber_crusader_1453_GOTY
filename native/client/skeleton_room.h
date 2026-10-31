#pragma once

#include "client/raster.h"
#include "client/renderer.h"
#include "client/skeleton_body.h"
#include "client/skeleton_comparison.h"
#include "client/skeleton_lines.h"
#include "client/skeleton_ragdoll.h"

#include <cstdint>
#include <utility>

#include <string>

namespace gtc::native {

// The test room: an empty box with a tiled floor and the two anatomical
// skeletons in it, so animation and rigging work has somewhere to happen.
// The original OpenSkeleton bodies use source-bone rigid transforms and are not scaled.
// The two static blue comparison meshes are separately deformed visual proxies.
class RoomFreecam;

class SkeletonRoom {
public:
    bool load(const std::string& root, std::string& error);
    bool ready() const { return ready_; }
    std::size_t triangles() const { return sceneTriangles_; }
    const SkeletonModel& male() const { return male_; }
    const SkeletonModel& female() const { return female_; }
    SkeletonModel& male() { return male_; }
    SkeletonModel& female() { return female_; }
    const SkeletonComparison& comparison(bool male) const { return male ? maleComparison_ : femaleComparison_; }
    bool setComparison(bool male, const SkeletonComparison::Profile& profile, std::string& error) {
        SkeletonComparison& copy=male ? maleComparison_ : femaleComparison_;
        const auto revision=copy.revision();
        if (!copy.setProfile(profile,error)) return false;
        if (revision!=copy.revision()) lineSceneReady_=false;
        return true;
    }
    void resetComparison(bool male) {
        std::string error; (male ? maleComparison_ : femaleComparison_).reset(error);
        lineSceneReady_=false;
    }
    bool lineView() const { return lineView_; }
    void toggleLineView() { lineView_ = !lineView_; }
    // Test-room controls act directly on named anatomical groups in both bodies.
    // fingerJoint: 0 = whole digit, 1 = MCP/MTP, 2 = PIP/IP, 3 = DIP.
    bool adjustReferenceGroup(int key, double degrees, int fingerJoint = 0);
    bool restoreReferencePose(); // straight-arm, straight-leg reference pose
    bool restoreOriginalPose(); // original registered CT pose
    bool toggleRagdoll();
    void advanceRagdoll(double seconds);
    bool ragdollActive() const { return bool(ragdoll_); }
    const SkeletonRagdoll* ragdoll() const { return ragdoll_.get(); }

    void render(NativeFrame& frame, const ViewState& view, double seconds, const RoomFreecam* freecam = nullptr);

private:
    std::unique_ptr<SkeletonRagdoll> ragdoll_;
    SkeletonModel male_;
    SkeletonModel female_;
    SkeletonComparison maleComparison_, femaleComparison_;
    SkeletonLines maleLines_, femaleLines_;
    GpuScene lineScene_;
    bool ready_ = false;
    bool lineView_ = false;
    double maleFloor_ = 0, femaleFloor_ = 0;
    bool lineSceneReady_ = false;
    std::uint64_t lineMalePoseRevision_ = 0, lineFemalePoseRevision_ = 0;
    std::string error_;
    // The room's geometry is kept between frames when the GPU path draws it:
    // two skeletons are three hundred thousand triangles, and re-sending them
    // every frame is the cost this path exists to remove.
    GpuScene scene_;
    std::uint64_t revision_ = 0;
    std::uint64_t blueMaleRevision_ = 0, blueFemaleRevision_ = 0, blueBatchRevision_ = 0;
    // Triangles in the cached scene. Printed by the room benchmark: if this
    // changes with the thread count, the split is losing or duplicating work.
    std::size_t sceneTriangles_ = 0;
    // Rest geometry is emitted once; a pose then costs one matrix per bone. The
    // pair is the batch index each source structure owns, so a pose update
    // changes transforms without re-uploading its mesh vertices.
    bool restReady_ = false;
    std::vector<std::pair<std::uint32_t, std::size_t>> boneBatches_;
    std::vector<std::array<double, 16>> cachedMaleMatrices_;
    std::vector<std::array<double, 16>> cachedFemaleMatrices_;
    bool sceneReady_ = false;
    double cachedPose_ = -1.0;
    std::uint64_t cachedMalePoseRevision_ = 0, cachedFemalePoseRevision_ = 0;
};

} // namespace gtc::native
