#pragma once

#include "client/skeleton_joint.h"
#include <vector>

namespace gtc::native {
// Track the rotation from each source finger to its extended reference. This
// keeps each joint's positive flexion aimed towards the palm even after its
// parent phalanges have straightened; no runtime contact calculation is used.
class FingerReferenceFrames {
public:
    explicit FingerReferenceFrames(std::size_t count) : reference_(count) {}
    void inherit(std::size_t parent, std::size_t child) { reference_[child]=reference_[parent]; }
    void attach(std::size_t parent, std::size_t child, Vec3 distal, Vec3 palm, Vec3& axis, double& angle) {
        const auto& p=reference_[parent];
        const auto neutral=joint_detail::compose(p,joint_detail::rotate(axis,-angle));
        const Vec3 a=apply(p,axis), d=apply(neutral,distal);
        if (dot(cross(a,d),palm)<0) { axis=axis*-1.0; angle=-angle; }
        reference_[child]=neutral;
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
