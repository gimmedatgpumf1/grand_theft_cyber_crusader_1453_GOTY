#pragma once

#include "client/skeleton_limb_frame.h"

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
// Rotate only about the neck's vertical axis: no head nod is introduced.
inline double forwardHeadYaw(Vec3 jawFromHead, double spineTurn) {
    const double radius=std::hypot(jawFromHead.x,jawFromHead.z);
    if (radius<1e-8) return 0;
    const double target=std::tan(spineTurn)*jawFromHead.y/radius;
    return std::asin(std::clamp(target,-1.0,1.0))-std::atan2(jawFromHead.x,jawFromHead.z);
}
struct NeckReferenceFrames {
    double neckTurn, atlasYaw, headAngle;
    Vec3 headAxis;
};
// Keep the source's front-to-back curvature but align the cervical column and
// head laterally; the head's additional twist keeps its face looking forward.
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
    const double yaw=forwardHeadYaw(center(jaw)-center(head),spineTurn+turn);
    const auto parent=joint_detail::compose(joint_detail::rotate({0,0,1},spineTurn+turn),joint_detail::rotate({0,1,0},yaw));
    const Vec3 headLine=frameVector(parent,center(head)-headPivot), atlas=frameVector(parent,center(c1)-headPivot);
    const double roll=uprightSpineAngle(atlas,{},headLine);
    const auto upright=joint_detail::rotate({0,0,1},roll);
    const Vec3 axis=normalized(frameVector(upright,headLine));
    const Vec3 face=frameVector(upright,frameVector(parent,center(jaw)-center(head)));
    const double a=face.x-axis.x*dot(axis,face), b=cross(axis,face).x, c=axis.x*dot(axis,face);
    const double radius=std::hypot(a,b);
    double twist=radius<1e-9 ? 0 : std::asin(std::clamp(-c/radius,-1.0,1.0))-std::atan2(a,b);
    while (twist>3.141592653589793) twist-=2*3.141592653589793;
    while (twist<-3.141592653589793) twist+=2*3.141592653589793;
    if (std::abs(twist)>1.5707963267948966) twist+=twist>0 ? -3.141592653589793 : 3.141592653589793;
    const auto combined=composeAlignments({0,0,1},roll,axis,twist);
    return NeckReferenceFrames{turn,yaw,combined.second,frameVector(joint_detail::inverse(parent),combined.first)};
}
} // namespace gtc::native
