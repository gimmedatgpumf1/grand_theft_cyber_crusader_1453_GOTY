#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gtc::native {
// Retarget only the little-toe skin from the old standing pose to the corrected
// bone frames. Inverting the same two-bone linear blend used by the GPU keeps
// the original surface and smooth joins instead of twisting it with the old,
// misaligned joint frames. The toe's tip is extended independently along z.
template<class Comparison,class Vertices>
void fitLittleToeSkin(const Comparison& c,const Vertices& vertices,double length,Vec3 origin,
    std::vector<Vec3>& points,const std::vector<int>& owners,const std::vector<int>& secondary,
    const std::vector<double>& weights) {
    const auto& before=c.littleToeSkinReference();const auto& after=c.correctedToeReference();
    if(before.size()!=c.parts().size() || after.size()!=c.parts().size() || points.size()<=591)return;
    const auto oldPose=c.toePose(before,origin,&before),newPose=c.toePose(after,origin,&after);
    const auto transform=[](const std::array<double,16>& m,Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    const auto oldPoint=[&](std::size_t i) {
        return transform(oldPose[std::size_t(owners[i])],points[i])*weights[i]+
               transform(oldPose[std::size_t(secondary[i])],points[i])*(1-weights[i]);
    };
    const double start=oldPoint(591).z+length*.038;
    for(std::size_t i=0;i<points.size();++i) {
        double influence=0;
        for(int j=0;j<4;++j)if(vertices[i].bones[std::size_t(j)]/10==5)influence+=vertices[i].weights[std::size_t(j)];
        if(influence<=1e-8)continue;
        Vec3 goal=oldPoint(i);
        double t=std::clamp((goal.z-start)/(length*.108),0.0,1.0);t=t*t*(3-2*t);
        goal.z+=length*.02455*influence*t;
        const auto& a=newPose[std::size_t(owners[i])];const auto& b=newPose[std::size_t(secondary[i])];
        const double w=weights[i];
        const Vec3 x{a[0]*w+b[0]*(1-w),a[1]*w+b[1]*(1-w),a[2]*w+b[2]*(1-w)};
        const Vec3 y{a[4]*w+b[4]*(1-w),a[5]*w+b[5]*(1-w),a[6]*w+b[6]*(1-w)};
        const Vec3 z{a[8]*w+b[8]*(1-w),a[9]*w+b[9]*(1-w),a[10]*w+b[10]*(1-w)};
        const Vec3 shift{a[12]*w+b[12]*(1-w),a[13]*w+b[13]*(1-w),a[14]*w+b[14]*(1-w)};
        const double determinant=dot(x,cross(y,z));
        if(std::abs(determinant)<1e-10)continue;
        const Vec3 value=goal-shift;
        points[i]={dot(value,cross(y,z))/determinant,dot(value,cross(z,x))/determinant,
                   dot(value,cross(x,y))/determinant};
    }
}
} // namespace gtc::native
