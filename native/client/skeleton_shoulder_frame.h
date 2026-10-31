#pragma once

#include "client/skeleton_limb_frame.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gtc::native {
struct ShoulderReferenceFrame {
    Vec3 axis, clavicleAxis, scapularAxis;
    double angle, scapularAngle;
    JointRigidTransform correction, scapular;
};
// Reference registration, not a patient diagnosis. We approximate the SC-AC
// line using centroids of medial/lateral 5% mesh vertices, then target a
// 24-degree coronal slope and 14-degree transverse retraction in the upright
// thorax. Clinical reports use identified landmarks and different axes: these
// numbers are a pose convention, not measured clinical joint angles.
inline std::optional<ShoulderReferenceFrame> sourceShoulderFrame(const SkeletonModel& model, int side, double spineTurn) {
    const int index=model.sourceStructure(side==0 ? "Clavicula_L" : "Clavicula_R");
    if (index<0) return std::nullopt;
    const auto& part=model.sourceStructures()[static_cast<std::size_t>(index)];
    const auto& points=model.bones()[part.group].points;
    if (part.pointCount<20) return std::nullopt;
    const double sign=side==0 ? 1.0 : -1.0;
    std::vector<std::pair<double,Vec3>> ordered; ordered.reserve(part.pointCount);
    for (std::size_t i=part.firstPoint;i<part.firstPoint+part.pointCount;++i)
        ordered.push_back({sign*points[i].x,points[i]});
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    const std::size_t count=std::max<std::size_t>(1,ordered.size()/20);
    Vec3 medial,lateral;
    for (std::size_t i=0;i<count;++i) { medial+=ordered[i].second; lateral+=ordered[ordered.size()-1-i].second; }
    const Vec3 chord=(lateral-medial)/static_cast<double>(count);
    if (length(chord)<0.05) return std::nullopt;
    constexpr double radians=3.14159265358979323846/180.0;
    const Vec3 target=frameVector(joint_detail::rotate({0,0,1},-spineTurn),
        normalized(Vec3{sign,std::tan(24*radians),-std::tan(14*radians)}));
    const Vec3 source=normalized(chord); Vec3 axis=cross(source,target);
    const double angle=std::atan2(length(axis),std::clamp(dot(source,target),-1.0,1.0));
    if (angle>35*radians) return std::nullopt; // avoid guessing beyond a conservative registration envelope
    if (length(axis)<1e-8) axis={0,0,sign}; else axis=normalized(axis);
    const auto correction=joint_detail::rotate(axis,angle);
    // A conservative compound AC reference convention. The registered
    // source is not a measured rest frame. These are NOT clinical rotations;
    // values were checked against these two meshes for surface intersections.
    const auto scapular=joint_detail::compose(joint_detail::rotate({0,0,sign},4*radians),
                                               joint_detail::rotate({0,sign,0},5*radians));
    const auto& r=scapular.rotation;
    Vec3 scapularAxis{r[7]-r[5],r[2]-r[6],r[3]-r[1]};
    const double scapularAngle=std::atan2(length(scapularAxis)*0.5,std::clamp((r[0]+r[4]+r[8]-1)*0.5,-1.0,1.0));
    scapularAxis=normalized(scapularAxis);
    return ShoulderReferenceFrame{axis,source,scapularAxis,angle,scapularAngle,correction,scapular};
}
} // namespace gtc::native
