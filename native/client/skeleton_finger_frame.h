#pragma once

#include "client/skeleton_joint.h"
#include "client/skeleton_limb_frame.h"
#include <vector>

namespace gtc::native {
// Reference frames are built symmetrically in each palm. Source-shape
// alignment is separate from the clinically bounded flexion coordinate.
class FingerReferenceFrames {
public:
    struct Axes { Vec3 flexion, alignment; double alignmentAngle; };
    explicit FingerReferenceFrames(std::size_t count) : reference_(count) {}
    void inherit(std::size_t parent, std::size_t child) { reference_[child]=reference_[parent]; }
    Axes attach(std::size_t parent, std::size_t child, Vec3 proximal, Vec3 distal, Vec3 palm) {
        const auto& p=reference_[parent];
        const Vec3 crossToStraight=cross(distal,proximal);
        const double magnitude=length(crossToStraight);
        const double alignmentAngle=std::atan2(magnitude,std::clamp(dot(distal,proximal),-1.0,1.0));
        const Vec3 extended=apply(p,proximal);
        Vec3 flexionInPalm=cross(extended,palm);
        if (length(flexionInPalm)<1e-8) {
            const Vec3 guide=std::abs(extended.y)<0.9 ? Vec3{0,1,0} : Vec3{1,0,0};
            flexionInPalm=cross(extended,guide);
        }
        const Vec3 flexion=normalized(apply(joint_detail::inverse(p),normalized(flexionInPalm)));
        const Vec3 alignment=magnitude>1e-8 ? crossToStraight/magnitude : flexion;
        reference_[child]=joint_detail::compose(p,joint_detail::rotate(alignment,alignmentAngle));
        return {flexion,alignment,alignmentAngle};
    }
    Axes attachTowardPalm(std::size_t parent, std::size_t child, Vec3 target, Vec3 distal, Vec3 palm) {
        return attach(parent,child,apply(joint_detail::inverse(reference_[parent]),target),distal,palm);
    }
    static Vec3 shaft(const Vec3* points, std::size_t count, Vec3 center, Vec3 hint) {
        Vec3 direction=normalized(hint);
        for (int step=0;step<16;++step) {
            Vec3 v;
            for (std::size_t i=0;i<count;++i) { const Vec3 d=points[i]-center; v+=d*dot(d,direction); }
            if (length(v)>1e-12) direction=normalized(v);
        }
        return dot(direction,hint)<0 ? direction*-1.0 : direction;
    }
private:
    static Vec3 apply(const JointRigidTransform& t, Vec3 v) {
        const auto& m=t.rotation;
        return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,
                m[6]*v.x+m[7]*v.y+m[8]*v.z};
    }
    std::vector<JointRigidTransform> reference_;
};
inline std::optional<FingerReferenceFrames::Axes> sourceFingerFrame(
        const SkeletonModel& model, FingerReferenceFrames& frames, std::size_t parent, std::size_t child,
        int side, Vec3 up, Vec3 down, bool metacarpal) {
    const auto wrist=sourceWristFrame(model,side,{1,0,0});
    if (!wrist) return std::nullopt;
    const auto points=[&](std::size_t i) {
        const auto& s=model.sourceStructures()[i];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](std::size_t i) {
        const auto [p,n]=points(i); Vec3 c; for (std::size_t j=0;j<n;++j) c+=p[j];
        return n ? c/static_cast<double>(n) : Vec3{};
    };
    const auto [pp,pn]=points(parent); const auto [cp,cn]=points(child);
    const Vec3 distal=FingerReferenceFrames::shaft(cp,cn,center(child),down);
    if (metacarpal) return frames.attachTowardPalm(parent,child,wrist->hand,distal,wrist->palm);
    const Vec3 proximal=FingerReferenceFrames::shaft(pp,pn,center(parent),up)*-1.0;
    return frames.attach(parent,child,proximal,distal,wrist->palm);
}

// The thumb starts in its registered CT position. Measure its metacarpal
// shaft and align it with the palm's longitudinal direction at T-pose; retain
// its independent CMC flexion coordinate for interactive movement.
inline std::optional<FingerReferenceFrames::Axes> sourceThumbFrame(
        const SkeletonModel& model, FingerReferenceFrames& frames, std::size_t parent, std::size_t child, int side) {
    const auto wrist=sourceWristFrame(model,side,{1,0,0});
    const int proximal=model.sourceStructure(side==0 ? "HandProxP1_L" : "HandProxP1_R");
    if (!wrist || proximal<0) return std::nullopt;
    const auto points=[&](std::size_t i) {
        const auto& s=model.sourceStructures()[i];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](std::size_t i) {
        const auto [p,n]=points(i); Vec3 c; for (std::size_t j=0;j<n;++j) c+=p[j];
        return n ? c/static_cast<double>(n) : Vec3{};
    };
    const auto [p,n]=points(child); const Vec3 c=center(child);
    const Vec3 shaft=FingerReferenceFrames::shaft(p,n,c,center(proximal)-c);
    constexpr double angle=35.0*3.14159265358979323846/180.0;
    const Vec3 target=normalized(wrist->hand*std::cos(angle)+wrist->radial*std::sin(angle));
    return frames.attachTowardPalm(parent,child,target,shaft,wrist->palm);
}
} // namespace gtc::native
