#pragma once

#include "client/skeleton_joint.h"
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
} // namespace gtc::native
