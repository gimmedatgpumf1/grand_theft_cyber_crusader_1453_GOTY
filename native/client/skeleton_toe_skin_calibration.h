#pragma once
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native {
// Local-space neutral matrices. Applied only when manual surface joints exist.
struct ToeSkinCalibration {
    std::vector<std::array<double,16>> before,after;
};
inline Vec3 toeSkinPoint(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
// Rebind the neutral skin, not the CT geometry. Carry each skin region by
// the displacement of its bone centre, without twisting it by the large
// calibration rotations. Blend at joints/webs; the GPU retains both owners.
template<class Comparison>
void rebindCalibratedToeSkin(const Comparison& c,Vec3 origin,
    std::vector<Vec3>& points,const std::vector<int>& owners,const std::vector<int>& secondary,
    std::vector<double>& weights) {
    const auto& ref=c.toeSkinCalibration();
    if(ref.before.size()!=c.parts().size() || ref.after.size()!=c.parts().size())return;
    std::vector<Vec3> movement(c.parts().size());
    for(std::size_t i=0;i<c.parts().size();++i) {
        const auto& part=c.parts()[i];
        if(part.points.empty() || !(part.name.starts_with("FootMeta") || part.name.starts_with("FootProxP") || part.name.starts_with("FootMidP") || part.name.starts_with("FootDistP")))continue;
        Vec3 center{};for(Vec3 p:part.points)center+=p;center=c.mapPart(center/double(part.points.size()),part);
        movement[i]=toeSkinPoint(ref.after[i],center)-toeSkinPoint(ref.before[i],center);
    }
    for(std::size_t i=0;i<points.size();++i) {
        const auto a=std::size_t(owners[i]),b=std::size_t(secondary[i]);
        if(a>=ref.before.size() || b>=ref.before.size())continue;
        double w=weights[i];const Vec3 p=points[i]-origin;
        const Vec3 old=toeSkinPoint(ref.before[a],p)*w+toeSkinPoint(ref.before[b],p)*(1-w);
        const Vec3 goal=old+movement[a]*w+movement[b]*(1-w);
        const auto& x=ref.after[a];const auto& y=ref.after[b];
        auto column=[&](int j){return Vec3{x[j]*w+y[j]*(1-w),x[j+1]*w+y[j+1]*(1-w),x[j+2]*w+y[j+2]*(1-w)};};
        Vec3 X=column(0),Y=column(4),Z=column(8);double det=dot(X,cross(Y,Z));
        // A 180-degree pair at equal weights collapses the linear blend. Use
        // its dominant owner instead of emitting enormous inverse vertices.
        if(std::abs(det)<.04) {w=w>=.5?1.:0.;weights[i]=w;X=column(0);Y=column(4);Z=column(8);det=dot(X,cross(Y,Z));}
        if(std::abs(det)<1e-8)continue;
        const Vec3 q=goal-column(12);
        points[i]=origin+Vec3{dot(q,cross(Y,Z))/det,dot(q,cross(Z,X))/det,dot(q,cross(X,Y))/det};
    }
}
} // namespace gtc::native
