#pragma once

#include "client/raster.h"
#include <memory>

namespace gtc::native {
class SkeletonModel;

// One world is shared by both figures so their bones can contact one another.
class SkeletonRagdoll {
public:
    SkeletonRagdoll();
    ~SkeletonRagdoll();
    SkeletonRagdoll(const SkeletonRagdoll&) = delete;
    SkeletonRagdoll& operator=(const SkeletonRagdoll&) = delete;
    bool create(SkeletonModel& male, Vec3 maleOrigin, SkeletonModel& female, Vec3 femaleOrigin);
    void advance(double seconds);
    struct Contacts { int floor=0, sameFigure=0, otherFigure=0; };
    Contacts contacts() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gtc::native
