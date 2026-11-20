#pragma once

#include "client/skull_midplane.h"
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace gtc::native {
// Vertebra planes are fitted independently to the source triangle surfaces,
// before the reference pose. This is a geometric, not clinical, estimate.
struct VertebraPlane { Vec3 origin,normal; };
inline std::array<std::string,24> vertebraNames() {
    std::array<std::string,24> names; std::size_t index=0;
    for (int i=1;i<=7;++i) names[index++]="C"+std::to_string(i);
    for (int i=1;i<=12;++i) names[index++]="T"+std::to_string(i);
    for (int i=1;i<=5;++i) names[index++]="L"+std::to_string(i);
    return names;
}
inline bool loadVertebraPlanes(const std::string& path,std::map<std::string,VertebraPlane>& planes) {
    std::ifstream file(path); std::string line; planes.clear();
    while (std::getline(file,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line); std::string name,extra; VertebraPlane plane;
        if (!(row>>name>>plane.origin.x>>plane.origin.y>>plane.origin.z
                  >>plane.normal.x>>plane.normal.y>>plane.normal.z) || row>>extra) return false;
        for (double v:{plane.origin.x,plane.origin.y,plane.origin.z,plane.normal.x,plane.normal.y,plane.normal.z})
            if (!std::isfinite(v)) return false;
        if (std::abs(length(plane.normal)-1)>1e-6 || !planes.emplace(name,plane).second) return false;
    }
    if (!file.eof() || planes.size()!=24) return false;
    for (const auto& name:vertebraNames()) if (!planes.count(name)) return false;
    return true;
}
inline SkullMidplane posedVertebraPlane(VertebraPlane plane,const std::array<double,16>& matrix) {
    const auto point=[&](Vec3 p) {
        return Vec3{matrix[0]*p.x+matrix[4]*p.y+matrix[8]*p.z+matrix[12],
                    matrix[1]*p.x+matrix[5]*p.y+matrix[9]*p.z+matrix[13],
                    matrix[2]*p.x+matrix[6]*p.y+matrix[10]*p.z+matrix[14]};
    };
    const Vec3 normal=normalized(plane.normal);
    const Vec3 helper=std::abs(normal.y)<0.9 ? Vec3{0,1,0} : Vec3{0,0,1};
    const Vec3 up=normalized(helper-normal*dot(helper,normal));
    const Vec3 origin=point(plane.origin);
    const Vec3 posedNormal=normalized(point(plane.origin+normal)-origin);
    const Vec3 posedUp=normalized(point(plane.origin+up)-origin);
    return {origin,posedNormal,posedUp,normalized(cross(posedNormal,posedUp))};
}
} // namespace gtc::native
