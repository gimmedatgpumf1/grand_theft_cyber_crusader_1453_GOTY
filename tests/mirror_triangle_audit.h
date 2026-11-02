#pragma once
// Verify actual reflected triangles and winding without rounding-boundary
// false failures. Coordinate bins only accelerate lookup; matches must pass
// the explicit geometric tolerance, and each triangle is consumed once.
#include "client/skeleton_body.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

inline bool reflectedTriangles(const std::vector<gtc::native::Vec3>& points,
                               const std::vector<std::array<int,3>>& faces,double tolerance) {
    using gtc::native::Vec3; using gtc::native::length;
    using Key=std::array<long long,3>;
    const auto key=[](Vec3 p)->Key { return {std::llround(p.x*1e6),std::llround(p.y*1e6),std::llround(p.z*1e6)}; };
    std::map<Key,std::vector<std::size_t>> bins;
    for (std::size_t i=0;i<faces.size();++i) {
        const auto f=faces[i];
        for (int v:f) if (v<0 || static_cast<std::size_t>(v)>=points.size()) return false;
        bins[key((points[f[0]]+points[f[1]]+points[f[2]])/3)].push_back(i);
    }
    std::vector<bool> used(faces.size());
    for (const auto& f:faces) {
        Vec3 a=points[f[0]],b=points[f[2]],c=points[f[1]]; a.x=-a.x; b.x=-b.x; c.x=-c.x;
        const auto k=key((a+b+c)/3); bool found=false;
        for (int dx=-1;dx<=1 && !found;++dx) for (int dy=-1;dy<=1 && !found;++dy)
            for (int dz=-1;dz<=1 && !found;++dz) {
                const auto it=bins.find({k[0]+dx,k[1]+dy,k[2]+dz}); if (it==bins.end()) continue;
                for (auto index:it->second) {
                    if (used[index]) continue; const auto g=faces[index];
                    for (int shift=0;shift<3;++shift) {
                        if (length(a-points[g[shift]])<=tolerance &&
                            length(b-points[g[(shift+1)%3]])<=tolerance &&
                            length(c-points[g[(shift+2)%3]])<=tolerance) {
                            used[index]=true; found=true; break;
                        }
                    }
                    if (found) break;
                }
            }
        if (!found) return false;
    }
    return true;
}
