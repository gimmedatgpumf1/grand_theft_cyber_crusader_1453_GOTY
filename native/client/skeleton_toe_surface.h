#pragma once

#include "client/raster.h"
#include <array>
#include <limits>
#include <optional>
#include <vector>

namespace gtc::native {
// All manual attachments are points on actual triangle faces, not bounding
// boxes, volume centres or nearest vertices. A screen ray chooses the visible
// face of the selected bone; orbiting exposes its other faces.
inline std::optional<Vec3> toeSurfaceRay(Vec3 origin,Vec3 direction,const std::vector<Vec3>& points,
                                         const std::vector<std::array<int,3>>& faces) {
    double best=std::numeric_limits<double>::infinity();std::optional<Vec3> hit;
    for(const auto& f:faces) {
        if(f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=points.size() || std::size_t(f[1])>=points.size() || std::size_t(f[2])>=points.size())continue;
        const Vec3 a=points[f[0]],edge1=points[f[1]]-a,edge2=points[f[2]]-a,p=cross(direction,edge2);
        const double determinant=dot(edge1,p);if(std::abs(determinant)<1e-12)continue;
        const double inv=1/determinant;const Vec3 t=origin-a;const double u=dot(t,p)*inv;
        if(u<0 || u>1)continue;
        const Vec3 q=cross(t,edge1);const double v=dot(direction,q)*inv;
        if(v<0 || u+v>1)continue;
        const double distance=dot(edge2,q)*inv;
        if(distance>0 && distance<best){best=distance;hit=origin+direction*distance;}
    }
    return hit;
}
inline Vec3 toeClosestTrianglePoint(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const Vec3 ab=b-a,ac=c-a,ap=p-a;const double d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0 && d2<=0)return a;
    const Vec3 bp=p-b;const double d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0 && d4<=d3)return b;
    const double vc=d1*d4-d3*d2;if(vc<=0 && d1>=0 && d3<=0)return a+ab*(d1/(d1-d3));
    const Vec3 cp=p-c;const double d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0 && d5<=d6)return c;
    const double vb=d5*d2-d1*d6;if(vb<=0 && d2>=0 && d6<=0)return a+ac*(d2/(d2-d6));
    const double va=d3*d6-d5*d4;if(va<=0 && d4-d3>=0 && d5-d6>=0)return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const double sum=va+vb+vc;if(std::abs(sum)<1e-20)return a;
    return a+ab*(vb/sum)+ac*(vc/sum);
}
inline std::optional<Vec3> toeClosestSurface(Vec3 p,const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces) {
    double best=std::numeric_limits<double>::infinity();std::optional<Vec3> result;
    for(const auto& f:faces) {
        if(f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=points.size() || std::size_t(f[1])>=points.size() || std::size_t(f[2])>=points.size())continue;
        const Vec3 q=toeClosestTrianglePoint(p,points[f[0]],points[f[1]],points[f[2]]),d=q-p;const double distance=dot(d,d);
        if(distance<best){best=distance;result=q;}
    }
    return result;
}
} // namespace gtc::native
