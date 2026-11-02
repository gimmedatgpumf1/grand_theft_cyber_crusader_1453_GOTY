#pragma once

#include "client/raster.h"
#include <array>
#include <memory>
#include <vector>

namespace gtc::native {
class SkeletonModel;
class SkeletonComparison;

// One world is shared by both figures so their bones can contact one another.
class SkeletonRagdoll {
public:
    SkeletonRagdoll();
    ~SkeletonRagdoll();
    SkeletonRagdoll(const SkeletonRagdoll&) = delete;
    SkeletonRagdoll& operator=(const SkeletonRagdoll&) = delete;
    bool create(SkeletonModel& male, Vec3 maleOrigin, SkeletonModel& female, Vec3 femaleOrigin,
                const SkeletonComparison* blueMale=nullptr, const SkeletonComparison* blueFemale=nullptr);
    const std::vector<std::array<double,16>>& bluePose(bool male) const;
    void advance(double seconds);
    struct Contacts { int floor=0, sameFigure=0, otherFigure=0; };
    Contacts contacts() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gtc::native
