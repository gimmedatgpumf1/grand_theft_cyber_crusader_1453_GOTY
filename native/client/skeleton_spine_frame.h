#pragma once

#include "client/skeleton_limb_frame.h"
#include "client/skeleton_jaw_frame.h"

namespace gtc::native {
// Rotate the source trunk at the sacrum/L5 joint so C7 is above the sacrum.
// This removes a global sideways lean without flattening the sagittal spine or
// changing any source vertices. The ordinary intervertebral bends stay separate.
inline double uprightSpineAngle(Vec3 sacrum, Vec3 pivot, Vec3 neck) {
    const Vec3 arm=neck-pivot;
    const double length=std::hypot(arm.x,arm.y);
    if (length<1e-8) return 0;
    return std::atan2(arm.x,arm.y)-std::asin(std::clamp((sacrum.x-pivot.x)/length,-1.0,1.0));
}
struct NeckReferenceFrames {
    double neckTurn, atlasYaw, headAngle;
    Vec3 headAxis;
};
// Keep the source's front-to-back curvature but align the cervical column and
// head laterally; align the estimated condylar axis so the face looks forward.
inline std::optional<NeckReferenceFrames> sourceNeckFrames(const SkeletonModel& model,double spineTurn) {
    const int c7=model.sourceStructure("C7"), c6=model.sourceStructure("C6"), c2=model.sourceStructure("C2");
    const int c1=model.sourceStructure("C1"), head=model.sourceStructure("Head"), jaw=model.sourceStructure("Jaw");
    if (c7<0 || c6<0 || c2<0 || c1<0 || head<0 || jaw<0) return std::nullopt;
    const auto points=[&](int i) {
        const auto& s=model.sourceStructures()[i];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](int i) {
        const auto [p,n]=points(i); Vec3 v; for (std::size_t j=0;j<n;++j) v+=p[j];
        return n ? v/static_cast<double>(n) : Vec3{};
    };
    const auto contact=[&](int a,int b) {
        const auto [p,n]=points(a); const auto [q,k]=points(b);
        const auto ps=std::max<std::size_t>(1,n/192), qs=std::max<std::size_t>(1,k/192);
        double best=1e9; Vec3 result=(center(a)+center(b))*0.5;
        for (std::size_t x=0;x<n;x+=ps) for (std::size_t y=0;y<k;y+=qs) {
            const double d=length(p[x]-q[y]); if (d<best) { best=d; result=(p[x]+q[y])*0.5; }
        }
        return result;
    };
    const Vec3 neckPivot=contact(c7,c6), headPivot=contact(c1,head);
    const auto trunk=joint_detail::rotate({0,0,1},spineTurn);
    const double turn=uprightSpineAngle(frameVector(trunk,center(c7)-neckPivot),{},frameVector(trunk,center(c2)-neckPivot));
    const auto hinge=sourceJawHinge(model); if (!hinge) return std::nullopt;
    // The bilateral condyles define a transverse facial axis. Align it rather
    // than using the jaw-to-skull centroid, which introduces a false yaw.
    const double yaw=std::atan2(hinge->axis.z,hinge->axis.x);
    const auto parent=joint_detail::compose(joint_detail::rotate({0,0,1},spineTurn+turn),joint_detail::rotate({0,1,0},yaw));
    const Vec3 headLine=frameVector(parent,center(head)-headPivot), atlas=frameVector(parent,center(c1)-headPivot);
    const double roll=uprightSpineAngle(atlas,{},headLine);
    const Vec3 rollAxis={0,0,roll<0 ? -1.0 : 1.0};
    return NeckReferenceFrames{turn,yaw,std::abs(roll),frameVector(joint_detail::inverse(parent),rollAxis)};
}
} // namespace gtc::native
