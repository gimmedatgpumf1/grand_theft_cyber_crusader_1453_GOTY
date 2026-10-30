#pragma once

#include "client/skeleton_joint.h"

namespace gtc::native {
struct MirroredElbowFrame {
    Vec3 flexion, alignment;
    double alignmentAngle;
};

// Build the same left/right elbow movement in each body's own T-pose frame.
// The source limbs remain distinct: only the anatomical movement is mirrored.
inline MirroredElbowFrame mirroredElbowFrame(Vec3 shoulder, Vec3 elbow, Vec3 forearm, bool left) {
    const Vec3 arm=normalized(elbow-shoulder), fore=normalized(forearm-elbow);
    const Vec3 target=left ? Vec3{1,0,0} : Vec3{-1,0,0};
    Vec3 shoulderAxis=cross(arm,target);
    const double shoulderAngle=std::atan2(length(shoulderAxis),std::clamp(dot(arm,target),-1.0,1.0));
    if (length(shoulderAxis)<1e-8) shoulderAxis={0,0,1};
    shoulderAxis=normalized(shoulderAxis);
    const auto inverseShoulder=joint_detail::inverse(joint_detail::rotate(shoulderAxis,shoulderAngle));
    const Vec3 worldAxis{0,left ? -1.0 : 1.0,0};
    const auto& m=inverseShoulder.rotation;
    const Vec3 flexion=normalized(Vec3{m[0]*worldAxis.x+m[1]*worldAxis.y+m[2]*worldAxis.z,
        m[3]*worldAxis.x+m[4]*worldAxis.y+m[5]*worldAxis.z,
        m[6]*worldAxis.x+m[7]*worldAxis.y+m[8]*worldAxis.z});
    Vec3 alignment=cross(fore,arm);
    const double angle=std::atan2(length(alignment),std::clamp(dot(fore,arm),-1.0,1.0));
    if (length(alignment)<1e-8) alignment=flexion;
    return {flexion,normalized(alignment),angle};
}
} // namespace gtc::native
