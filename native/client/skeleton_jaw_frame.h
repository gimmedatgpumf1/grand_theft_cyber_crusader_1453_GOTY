#pragma once

#include "client/skeleton_body.h"
#include <algorithm>
#include <optional>
#include <vector>

namespace gtc::native {
struct JawHingeFrame { Vec3 pivot, axis; };

// Approximate the two mandibular condyles from the superior/posterior portions
// of the actual jaw mesh. The jaw hinges at these ends, not at its teeth.
// This is a geometric estimate, not an expert-labelled clinical landmark.
inline std::optional<JawHingeFrame> sourceJawHinge(const SkeletonModel& model) {
    const int id=model.sourceStructure("Jaw"); if (id<0) return std::nullopt;
    const auto& jaw=model.sourceStructures()[id];
    const auto* points=model.bones()[jaw.group].points.data()+jaw.firstPoint;
    if (jaw.pointCount<20) return std::nullopt;
    Vec3 center; for (std::size_t i=0;i<jaw.pointCount;++i) center+=points[i];
    center=center/static_cast<double>(jaw.pointCount);
    Vec3 condyles[2];
    for (int side=0;side<2;++side) {
        std::vector<Vec3> half;
        for (std::size_t i=0;i<jaw.pointCount;++i)
            if ((points[i].x-center.x)*(side==0 ? -1.0 : 1.0)>0) half.push_back(points[i]);
        if (half.size()<10) return std::nullopt;
        std::sort(half.begin(),half.end(),[](Vec3 a,Vec3 b){return a.z<b.z;});
        half.resize(std::max<std::size_t>(1,half.size()/5));
        std::sort(half.begin(),half.end(),[](Vec3 a,Vec3 b){return a.y>b.y;});
        half.resize(std::max<std::size_t>(1,half.size()*15/100));
        for (Vec3 p:half) condyles[side]+=p;
        condyles[side]=condyles[side]/static_cast<double>(half.size());
    }
    const Vec3 axis=normalized(condyles[1]-condyles[0]);
    if (length(axis)<1e-8) return std::nullopt;
    return JawHingeFrame{(condyles[0]+condyles[1])*0.5,axis};
}
} // namespace gtc::native
