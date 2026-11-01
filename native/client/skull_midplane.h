#pragma once

#include "client/skeleton_body.h"

namespace gtc::native {
// Plane fitted to outward-visible source Head triangles alone, before any
// body pose. This geometric selection is conservative, not an anatomical
// segmentation; see 45-fit-skull-midplane.py for details and limitations.
struct SkullMidplane {
    Vec3 origin, normal, up, forward;
    Vec3 local(Vec3 p) const {
        const Vec3 d=p-origin;
        return {dot(d,normal),dot(d,up),dot(d,forward)};
    }
    Vec3 world(Vec3 p) const { return origin+normal*p.x+up*p.y+forward*p.z; }
};
inline SkullMidplane skullMidplane(bool male,const std::array<double,16>& m) {
    const Vec3 origin=male ? Vec3{0.009724651574,1.713671923386,0.038890553648}
                           : Vec3{-0.054193083472,1.660251465821,0.025364096919};
    const Vec3 normal=male ? Vec3{0.997126286943,-0.023187023971,-0.072121632024}
                           : Vec3{0.985046191514,0.152846486767,-0.079510704099};
    const auto point=[&](Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
                    m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    // Construct the whole frame in scan coordinates, then carry all three
    // axes through the head's actual pose. Recomputing an 'up' vector from
    // the world would discard the rotation around the plane normal.
    const Vec3 scanNormal=normalized(normal);
    const Vec3 scanUp=normalized(Vec3{0,1,0}-scanNormal*scanNormal.y);
    const Vec3 posedOrigin=point(origin);
    const Vec3 posedNormal=normalized(point(origin+scanNormal)-posedOrigin);
    const Vec3 posedUp=normalized(point(origin+scanUp)-posedOrigin);
    return {posedOrigin,posedNormal,posedUp,normalized(cross(posedNormal,posedUp))};
}
} // namespace gtc::native
