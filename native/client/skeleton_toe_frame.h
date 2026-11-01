#pragma once

#include "client/skeleton_finger_frame.h"

namespace gtc::native {
// Align each toe using its actual incoming and outgoing joint locations. Tiny
// distal toe bones are often too round for a stable principal-shaft estimate.
// The clinical flexion coordinate is separate and points toward the sole.
inline std::optional<FingerReferenceFrames::Axes> sourceToeFrame(
        const SkeletonModel& model, FingerReferenceFrames& frames, std::size_t parent, std::size_t child,
        int side, Vec3 pivot, Vec3 soleNormal) {
    const std::string tag=side==0 ? "_L" : "_R";
    const int big=model.sourceStructure("FootMeta1"+tag), little=model.sourceStructure("FootMeta5"+tag);
    const int middle=model.sourceStructure("FootMeta3"+tag), heel=model.sourceStructure("Cal"+tag);
    const int tibia=model.sourceStructure("Tibia"+tag), talus=model.sourceStructure("Tal"+tag);
    if (big<0 || little<0 || middle<0 || heel<0 || tibia<0 || talus<0) return std::nullopt;
    const auto points=[&](std::size_t i) {
        const auto& s=model.sourceStructures()[i];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](std::size_t i) {
        const auto [p,n]=points(i); Vec3 c; for (std::size_t j=0;j<n;++j) c+=p[j];
        return n ? c/static_cast<double>(n) : Vec3{};
    };
    const auto contact=[&](std::size_t a, std::size_t b) {
        const auto [p,n]=points(a); const auto [q,k]=points(b);
        const auto ps=std::max<std::size_t>(1,n/192), qs=std::max<std::size_t>(1,k/192);
        Vec3 result=(center(a)+center(b))*0.5; double best=1e9;
        for (std::size_t x=0;x<n;x+=ps) for (std::size_t y=0;y<k;y+=qs) {
            const double d=length(p[x]-q[y]);
            if (d<best) { best=d; result=(p[x]+q[y])*0.5; }
        }
        return result;
    };
    const Vec3 direction=center(middle)-center(heel);
    const Vec3 forward=normalized(direction-soleNormal*dot(direction,soleNormal));
    Vec3 dorsal=normalized(soleNormal);
    if (dot(dorsal,center(tibia)-center(talus))<0) dorsal=dorsal*-1.0;
    const auto& name=model.sourceStructures()[child].name;
    std::string next=name;
    if (name.rfind("FootProxP",0)==0) next.replace(0,9,name[9]=='1' ? "FootDistP" : "FootMidP");
    else if (name.rfind("FootMidP",0)==0) next.replace(0,8,"FootDistP");
    else next.clear();
    const int nextIndex=next.empty() ? -1 : model.sourceStructure(next);
    if (!next.empty() && nextIndex<0) return std::nullopt;
    const Vec3 distal=normalized((nextIndex<0 ? center(child) : contact(child,nextIndex))-pivot);
    if (length(distal)<1e-8) return std::nullopt;
    auto result=frames.attachTowardPalm(parent,child,forward,distal,dorsal*-1.0);
    result.yaw=normalized(frames.axisInFrame(child,dorsal));
    return result;
}
} // namespace gtc::native
