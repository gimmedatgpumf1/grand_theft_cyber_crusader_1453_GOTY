#pragma once

#include "client/raster.h"

#include <array>
#include <string>
#include <vector>

namespace gtc::native {

// One bone of the anatomical skeleton: a rigid piece of mesh, the joint it turns
// about, and the bone it hangs from. Every bone in the source OBJ is already a
// separate closed shell, so a bone is one rigid body and no vertex has to be
// shared between two of them -- which is exactly why the joints behave.
struct SkeletonBone {
    std::string name;
    int parent = -1;
    Vec3 pivot;                                  // joint centre, metres, rest pose
    std::vector<Vec3> points;
    std::vector<std::array<int, 3>> triangles;
    double pitch = 0, yaw = 0, roll = 0;         // pose angles, radians
};

// A loaded skeleton. Rest pose is metres, soles on y = 0, facing +z, and the
// source scale is never touched. Both models are registered OpenSkeleton
// CT-derived models. Their sources and the limits of their verification are
// documented in tools/skeleton-source.
class SkeletonModel {
public:
    bool load(const std::string& path, std::string& error);
    bool loaded() const { return !bones_.empty(); }
    const std::string& name() const { return name_; }
    double height() const { return height_; }
    const std::vector<SkeletonBone>& bones() const { return bones_; }
    std::vector<SkeletonBone>& bones() { return bones_; }
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
    void drawBones(Rasterizer& rasterizer, const Camera& camera, Vec3 origin, double yaw,
                   std::size_t begin, std::size_t end) const;

private:
    std::vector<SkeletonBone> bones_;
    std::string name_;
    double height_ = 0;
};

} // namespace gtc::native
