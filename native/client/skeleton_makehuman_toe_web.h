#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gtc::native {
// Pull the four original MakeHuman interdigital saddles toward the MTPs.
// The Gaussian lateral falloff and smooth longitudinal falloff move the web
// and its nearby surfaces together, leaving the midfoot and tips nearly fixed.
inline void fitFemaleMakeHumanToeWebs(std::vector<Vec3>& points,double footLength) {
    // Saddle vertices in originalFemaleVertices, not new or generated faces.
    constexpr std::array<std::size_t,4> saddles{307,303,567,591};
    constexpr std::array<double,4> depth{.0621,.0648,.06345,.0368};
    constexpr std::array<double,4> width{.030,.027,.025,.021};
    constexpr std::array<double,4> forward{.10,.10,.09,.045};
    if(points.size()<=saddles.back() || footLength<=1e-9)return;
    std::array<Vec3,4> centers{};
    for(std::size_t i=0;i<centers.size();++i)centers[i]=points[saddles[i]];
    const double lateral=centers[1].x>centers[0].x?1.0:-1.0;
    const auto smooth=[](double t) {
        t=std::clamp(t,0.0,1.0);return t*t*(3.0-2.0*t);
    };
    for(Vec3& p:points) {
        double pull=0,side=0;
        for(std::size_t i=0;i<centers.size();++i) {
            const double dx=(p.x-centers[i].x)/(width[i]*footLength);
            const double dz=p.z-centers[i].z;
            const double falloff=dz<0?smooth((dz+.17*footLength)/(.17*footLength)):
                                       smooth((forward[i]*footLength-dz)/(forward[i]*footLength));
            const double weight=std::exp(-.5*dx*dx)*falloff;
            pull+=depth[i]*footLength*weight;
            // Center the second web between its two adjacent toe bones.
            if(i==1)side+=lateral*.00393*footLength*weight;
        }
        p.x+=side;p.z-=pull;
        // Ease the inside of the little toe around its straightened joint.
        // The small local falloff leaves the web, tip and adjoining toes intact.
        const double x=(p.x-centers[3].x-lateral*.0073*footLength)/(.0147*footLength);
        const double y=(p.y-centers[3].y-.014*footLength)/(.0245*footLength);
        const double z=(p.z-centers[3].z-.042*footLength)/(.0245*footLength);
        p.x-=lateral*.00196*footLength*std::exp(-.5*(x*x+y*y+z*z));
    }
}
} // namespace gtc::native
