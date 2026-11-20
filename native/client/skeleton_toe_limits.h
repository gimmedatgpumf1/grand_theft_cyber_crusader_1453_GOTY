#pragma once

#include "native.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace gtc::native {
// The user's ordered surface marks provide each shaft, not a joint normal.
// The foot's dorsal direction supplies the sagittal plane. Positive rotation
// bends the child toward the sole. This measures relative to the marked parent,
// not the unaltered CT pose, which is not necessarily anatomically neutral.
struct MarkedToeBend {
    Vec3 axis, neutral, current;
    double flexion;
};
inline std::optional<MarkedToeBend> markedToeBend(Vec3 parent,Vec3 child,Vec3 dorsal) {
    if(length(parent)<1e-8 || length(child)<1e-8 || length(dorsal)<1e-8)return std::nullopt;
    parent=normalized(parent); dorsal=normalized(dorsal);
    Vec3 axis=cross(parent,dorsal*-1.0);
    if(length(axis)<1e-8)return std::nullopt;
    axis=normalized(axis);
    child=child-axis*dot(child,axis);
    if(length(child)<1e-8)return std::nullopt;
    child=normalized(child);
    const double angle=std::atan2(dot(axis,cross(parent,child)),std::clamp(dot(parent,child),-1.0,1.0));
    return MarkedToeBend{axis,parent,child,angle};
}
} // namespace gtc::native
