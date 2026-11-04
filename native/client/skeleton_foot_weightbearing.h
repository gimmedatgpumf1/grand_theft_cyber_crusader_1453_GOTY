#pragma once

#include "client/skeleton_joint.h"

namespace gtc::native {
// Surface-area-weighted PCA gives a mesh proxy for the bone axes used by the
// WBCT forefoot splay study. It is not a substitute for their clinical image
// pipeline. Integrating triangle moments avoids dependence on vertex density,
// random sampling, or the tessellation of a flat face.
template<class Part,class Map>
Vec3 weightBearingBoneAxis(const Part& part,Map map) {
    double total=0;Vec3 mean{};
    for (const auto& face:part.faces) {
        Vec3 a=map(part.points[face[0]],part),b=map(part.points[face[1]],part),c=map(part.points[face[2]],part);
        const double area=length(cross(b-a,c-a))*.5;
        mean+=(a+b+c)*(area/3);total+=area;
    }
    if (total<1e-16) return {};
    mean=mean/total;
    double covariance[3][3]{};
    for (const auto& face:part.faces) {
        Vec3 a=map(part.points[face[0]],part),b=map(part.points[face[1]],part),c=map(part.points[face[2]],part);
        const double area=length(cross(b-a,c-a))*.5;
        a=a-mean;b=b-mean;c=c-mean;const Vec3 sum=a+b+c;
        const double v[4][3]={{a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z},{sum.x,sum.y,sum.z}};
        for(int i=0;i<3;++i) for(int j=0;j<3;++j)
            for(const auto& row:v) covariance[i][j]+=area*row[i]*row[j]/12;
    }
    Vec3 direction{0,0,1};
    for(int step=0;step<24;++step) {
        Vec3 next{covariance[0][0]*direction.x+covariance[0][1]*direction.y+covariance[0][2]*direction.z,
                  covariance[1][0]*direction.x+covariance[1][1]*direction.y+covariance[1][2]*direction.z,
                  covariance[2][0]*direction.x+covariance[2][1]*direction.y+covariance[2][2]*direction.z};
        if(length(next)<1e-18) return {};
        direction=normalized(next);
    }
    return direction.z<0 ? direction*-1.0 : direction;
}
} // namespace gtc::native
