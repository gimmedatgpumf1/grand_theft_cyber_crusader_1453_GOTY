#pragma once

#include "client/skeleton_body.h"
#include "client/skeleton_limb_frame.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gtc::native {
// Reversibly balance the two geometric clavicle slopes around their mean.
// This is a T-pose symmetry convention, not a measured clinical SC angle or
// an inferred normal posture. Each whole clavicle/scapula/arm chain moves
// about its existing SC pivot; source/rest geometry is never altered.
inline std::optional<double> sourceClavicleSlope(const SkeletonModel& model, int side, double spineTurn) {
    const std::string name=side==0 ? "Clavicula_L" : "Clavicula_R";
    const int index=model.sourceStructure(name); if (index<0) return std::nullopt;
    const auto& part=model.sourceStructures()[static_cast<std::size_t>(index)];
    const auto& vertices=model.bones()[part.group].points;
    if (part.pointCount<20) return std::nullopt;
    const double sign=side==0 ? 1.0 : -1.0;
    std::vector<std::pair<double,Vec3>> ordered; ordered.reserve(part.pointCount);
    for (std::size_t i=part.firstPoint;i<part.firstPoint+part.pointCount;++i)
        ordered.push_back({sign*vertices[i].x,vertices[i]});
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    const std::size_t count=std::max<std::size_t>(1,ordered.size()/20);
    Vec3 medial, lateral;
    for (std::size_t i=0;i<count;++i) { medial+=ordered[i].second; lateral+=ordered[ordered.size()-1-i].second; }
    const Vec3 chord=frameVector(joint_detail::rotate({0,0,1},spineTurn),(lateral-medial)/static_cast<double>(count));
    if (sign*chord.x<0.04 || !std::isfinite(chord.y)) return std::nullopt;
    return std::atan2(chord.y,sign*chord.x);
}
inline std::optional<double> referenceGirdleTurn(const SkeletonModel& model, int side, double spineTurn) {
    const auto left=sourceClavicleSlope(model,0,spineTurn), right=sourceClavicleSlope(model,1,spineTurn);
    if (!left || !right) return std::nullopt;
    constexpr double pi=3.14159265358979323846;
    const double slope=side==0 ? *left : *right;
    const double difference=std::clamp((*left+*right)*0.5-slope,-5*pi/180,5*pi/180);
    return (side==0 ? 1.0 : -1.0)*difference;
}
} // namespace gtc::native
