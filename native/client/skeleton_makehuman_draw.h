#pragma once

// Experimental in-scene preview of the original scaled MakeHuman foot.
#include "client/skeleton_makehuman_raw.h"

namespace gtc::native {
template<class Comparison>
void drawMakeHumanFeet(const Comparison& comparison, bool male, Rasterizer& rasterizer,
                       Vec3 origin, std::uint32_t idBase,
                       const std::vector<std::array<double,16>>* pose,
                       const Material& flesh) {
    const auto transform=[](const std::array<double,16>& m,Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    for(int side=0;side<2;++side) {
        const std::string suffix=side==0 ? "_L" : "_R";
        auto foot=rawMakeHumanFoot(comparison,male,suffix,origin);
        if(foot.points.empty())continue;
        if(!rasterizer.gpu() && pose) for(std::size_t i=0;i<foot.points.size();++i) {
            const auto first=static_cast<std::size_t>(foot.owners[i]);
            const auto second=static_cast<std::size_t>(foot.secondaryOwners[i]);
            if(first>=pose->size() || second>=pose->size())continue;
            const Vec3 p=foot.points[i];const double w=foot.ownerWeights[i];
            foot.points[i]=transform((*pose)[first],p)*w+transform((*pose)[second],p)*(1-w);
        }
        for(int& owner:foot.owners)owner+=static_cast<int>(idBase);
        for(int& owner:foot.secondaryOwners)owner+=static_cast<int>(idBase);
        rasterizer.setObject(idBase+static_cast<std::uint32_t>(5*comparison.parts().size()+side),{},0);
        rasterizer.meshObjectBlended(foot.points,foot.faces,foot.owners,foot.secondaryOwners,foot.ownerWeights,flesh);
    }
}
} // namespace gtc::native
