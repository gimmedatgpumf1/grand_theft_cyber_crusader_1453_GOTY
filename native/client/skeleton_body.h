#pragma once

#include "client/raster.h"
#include "client/skeleton_kinematic_tree.h"

#include <array>
#include <string>
#include <vector>

namespace gtc::native {

// A coarse runtime mesh group. One group can contain many named source
// structures; it is not necessarily an independently articulated bone.
struct SkeletonBone {
    std::string name;
    int parent = -1;
    Vec3 pivot;                                  // joint centre, metres, rest pose
    std::vector<Vec3> points;
    std::vector<std::array<int, 3>> triangles;
    double pitch = 0, yaw = 0, roll = 0;         // pose angles, radians
    bool hasFittedBendAxis = false;               // axis is not a complete joint model
    Vec3 fittedBendAxis;
};

// Ranges inside a coarse runtime mesh group for one labelled source structure.
// Source structure ranges are not joints and do not imply an articulated rig.
struct SkeletonSourceStructure {
    std::string name;
    std::size_t group = 0;
    std::size_t firstPoint = 0, pointCount = 0;
    std::size_t firstTriangle = 0, triangleCount = 0;
};

// An explicitly listed anatomical relationship between source structures.
// Classification describes biology; it is not a runtime joint constraint.
struct SkeletonSourceLink {
    std::size_t first = 0, second = 0;
    std::string kind, mobility;
};

// Femoral anatomical line through the epicondyles. A static shape-derived
// estimate, not a knee motion model or individual range of motion.
struct SkeletonKneeAxis {
    Vec3 origin;
    Vec3 medialDirection;
    double width = 0;
};

// A loaded skeleton. Rest pose is metres, soles on y = 0, facing +z, and the
// source scale is never touched. Both models are registered OpenSkeleton
// CT-derived models. Their sources and the limits of their verification are
// documented in tools/skeleton-source.
class SkeletonModel {
public:
    bool load(const std::string& path, std::string& error);
    bool loaded() const { return !bones_.empty(); }
    bool loadSourceLinks(const std::string& path, std::string& error);
    // Set the two femoral pivots from the source-fitted head estimates. This
    // only changes pivots; it does not calibrate joint axes or enforce ROM.
    bool loadHipCenters(const std::string& path, std::string& error);
    bool loadKneeAxes(const std::string& path, std::string& error);
    const std::array<SkeletonKneeAxis, 2>& kneeAxes() const { return kneeAxes_; }
    bool hasKneeAxes() const { return hasKneeAxes_; }
    const std::vector<SkeletonSourceLink>& sourceLinks() const { return sourceLinks_; }
    const std::string& name() const { return name_; }
    double height() const { return height_; }
    const std::vector<SkeletonBone>& bones() const { return bones_; }
    std::vector<SkeletonBone>& bones() { return bones_; }
    const std::vector<SkeletonSourceStructure>& sourceStructures() const { return sourceStructures_; }
    int sourceStructure(const std::string& name) const;
    // Index of a bone by name, or -1.
    int bone(const std::string& name) const;

    // A slow idle: the pelvis drifts, the spine counter-bends, the head turns
    // and the arms hang with the sway. Nothing here changes a bone length.
    void poseIdle(double seconds, double phase);

    // Draws the posed skeleton with its feet at `origin`, turned by `yaw`.
    // `onlyBone` restricts it to one bone, which is how the benchmarks render a
    // single skull through each backend.
    void draw(Rasterizer& rasterizer, const Camera& camera, Vec3 origin, double yaw,
              int onlyBone = -1) const;

    // A slice of the bones, so one body can be emitted by several threads. Each
    // slice is independent: a bone is placed by its own chain and writes only
    // its own triangles.
    std::size_t boneCount() const { return bones_.size(); }
    // Where each bone ends up for a given placement: a 4x4 column-major matrix,
    // the form the backend takes as a batch transform. A bone is a rigid body,
    // so this is all that has to change between poses -- the vertices stay put.
    std::vector<std::array<double, 16>> boneMatrices(Vec3 origin, double yaw) const;
    // The same triangles, once, in rest position: one batch per bone, numbered
    // from firstObjectId. Paired with boneMatrices() a pose change costs 28
    // matrices instead of 195,000 vertices.
    void buildRestGeometry(Rasterizer& rasterizer, const Camera& camera, int firstObjectId) const;
    // Independent GPU batches per labelled source structure. They inherit the
    // coarse group's transform unless a complete calibrated source rig is supplied.
    void buildSourceRestGeometry(Rasterizer& rasterizer, int firstObjectId) const;
    std::vector<std::array<double, 16>> sourceMatrices(Vec3 origin, double yaw) const;
    // Accept only a complete, explicitly supplied source-to-body map, at bind
    // pose. No clinical calibration is inferred or manufactured here.
    bool setSourceRig(AnatomicalKinematicTree rig, const std::vector<std::size_t>& sourceBodies, std::string& error);
    bool setSourcePose(const std::vector<AnatomicalKinematicTree::PoseUpdate>& updates);
    std::uint64_t sourcePoseRevision() const { return sourcePoseRevision_; }
    void drawBones(Rasterizer& rasterizer, const Camera& camera, Vec3 origin, double yaw,
                   std::size_t begin, std::size_t end) const;

private:
    std::vector<SkeletonBone> bones_;
    std::vector<SkeletonSourceStructure> sourceStructures_;
    std::vector<SkeletonSourceLink> sourceLinks_;
    std::array<SkeletonKneeAxis, 2> kneeAxes_{};
    bool hasKneeAxes_ = false;
    mutable std::optional<AnatomicalKinematicTree> sourceRig_;
    std::vector<std::size_t> sourceBodies_;
    std::uint64_t sourcePoseRevision_ = 0;
    std::string name_;
    double height_ = 0;
};

} // namespace gtc::native
