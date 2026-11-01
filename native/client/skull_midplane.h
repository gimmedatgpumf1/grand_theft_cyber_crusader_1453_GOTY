#pragma once

#include "client/skeleton_body.h"

namespace gtc::native {
// Plane fitted to each source Head mesh alone, before any body pose. See
// 45-fit-skull-midplane.py for the surface-reflection fit and its limitations.
struct SkullMidplane {
    Vec3 origin, normal, up, forward;
    Vec3 local(Vec3 p) const {
        const Vec3 d=p-origin;
        return {dot(d,normal),dot(d,up),dot(d,forward)};
    }
};
inline SkullMidplane skullMidplane(bool male,const std::array<double,16>& m) {
    const Vec3 origin=male ? Vec3{0.010350176239,1.712216810588,0.046862083866}
                           : Vec3{-0.052859128301,1.656864116111,0.032299743336};
    const Vec3 normal=male ? Vec3{0.996695817502,-0.026225488909,-0.076874385236}
                           : Vec3{0.985715246282,0.144752347015,-0.086094200039};
    const auto point=[&](Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
                    m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    const Vec3 posedOrigin=point(origin);
    const Vec3 posedNormal=normalized(point(origin+normal)-posedOrigin);
    const Vec3 up=normalized(Vec3{0,1,0}-posedNormal*posedNormal.y);
    return {posedOrigin,posedNormal,up,cross(posedNormal,up)};
}
} // namespace gtc::native
