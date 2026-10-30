#pragma once

#include "client/skeleton_joint.h"

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
} // namespace gtc::native
