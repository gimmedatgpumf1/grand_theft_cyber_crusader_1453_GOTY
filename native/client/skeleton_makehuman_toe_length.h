#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace gtc::native {
// Adjust each original toe along the heel-to-toe axis, while retaining the
// source vertices, triangles and skin owners. MakeHuman's source weights
// localise each adjustment; a smooth ramp anchors it at the toe's root.
template<class Comparison,class Vertices>
void fitMakeHumanToeLengths(const Comparison& c,const Vertices& vertices,
    const std::array<int,64>& ids,Vec3 origin,const std::array<double,5>& ratios,
    std::vector<Vec3>& points) {
    if(std::all_of(ratios.begin(),ratios.end(),[](double value){return value==1.0;}))return;
    std::array<double,5> root{},tip{};
    root.fill(1e30);tip.fill(-1e30);
    for(int toe=0;toe<5;++toe) {
        const int id=ids[(toe+1)*10+1];
        if(id<0)continue;
        const auto& part=c.parts()[std::size_t(id)];
        for(Vec3 p:part.points)root[toe]=std::min(root[toe],(c.mapPart(p,part)+origin).z);
    }
    for(std::size_t i=0;i<points.size();++i) {
        std::array<double,5> weights{};
        for(int j=0;j<4;++j) {
            const int digit=int(vertices[i].bones[std::size_t(j)])/10;
            if(digit>=1 && digit<=5)weights[digit-1]+=vertices[i].weights[std::size_t(j)];
        }
        for(int toe=0;toe<5;++toe)if(weights[toe]>.6)tip[toe]=std::max(tip[toe],points[i].z);
    }
    for(std::size_t i=0;i<points.size();++i) {
        const double z=points[i].z;double offset=0;
        for(int j=0;j<4;++j) {
            const int digit=int(vertices[i].bones[std::size_t(j)])/10;
            if(digit<1 || digit>5)continue;
            const int toe=digit-1;const double span=tip[toe]-root[toe];
            if(span<=1e-9 || ratios[toe]==1.0)continue;
            const double t=std::clamp((z-root[toe])/span,0.0,1.0);
            const double ramp=t*t*(3.0-2.0*t);
            offset+=vertices[i].weights[std::size_t(j)]*(ratios[toe]-1.0)*span*ramp;
        }
        points[i].z+=offset;
    }
}
} // namespace gtc::native
