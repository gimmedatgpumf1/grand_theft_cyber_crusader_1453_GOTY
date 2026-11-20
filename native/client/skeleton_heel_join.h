#pragma once

#include "client/skeleton_metatarsal_plane.h"

namespace gtc::native {

// Extend only the existing side plane over the small gap to a heel panel.
// The patches remain coplanar with the side, rather than adding a new plane.
template<class Comparison>
void joinHeelToSide(const Comparison& comparison,const std::string& suffix,Vec3 origin,double backward,
                    const std::array<MetatarsalPlane,2>& heel,MetatarsalPlane& side) {
    using namespace metatarsal_plane_detail;
    Vec3 anchor{},normal{};
    double largest=0;
    for (auto f:side.faces) {
        Vec3 a=side.points[f[0]],b=side.points[f[1]],c=side.points[f[2]];
        Vec3 n=cross(b-a,c-a);
        if (length(n)>largest) { largest=length(n); anchor=a; normal=n; }
    }
    if (largest<1e-20) return;
    normal=normal/largest;
    if (std::abs(normal.x)<1e-9) return;
    const double outward=normal.x<0 ? -1.0 : 1.0;
    const double slopeU=-outward*normal.z/normal.x, slopeV=-outward*normal.y/normal.x;
    const double level=outward*dot(normal,anchor)/normal.x;
    std::vector<std::pair<Vec3,Vec3>> edges;
    for (const auto& panel:heel) for (auto f:panel.faces) for (int i=0;i<3;++i) {
        const Vec3 p=panel.points[f[i]],q=panel.points[f[(i+1)%3]];
        if (std::abs(dot(normal,p-anchor))>1e-8 || std::abs(dot(normal,q-anchor))>1e-8 ||
            std::abs(p.y-q.y)<1e-9) continue;
        edges.emplace_back(p,q);
    }
    std::vector<std::vector<Vec3>> patches;
    for (auto edge:edges) {
        if (edge.first.y>edge.second.y) std::swap(edge.first,edge.second);
        std::vector<double> knots{edge.first.y,edge.second.y};
        for (Vec3 p:side.points) if (p.y>edge.first.y && p.y<edge.second.y) knots.push_back(p.y);
        std::sort(knots.begin(),knots.end());
        knots.erase(std::unique(knots.begin(),knots.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),knots.end());
        const auto backZ=[&](double y) {
            return edge.first.z+(edge.second.z-edge.first.z)*(y-edge.first.y)/(edge.second.y-edge.first.y);
        };
        for (std::size_t i=0;i+1<knots.size();++i) {
            double lo=knots[i],hi=knots[i+1];
            if (hi-lo<1e-10) continue;
            const double mid=(lo+hi)*.5;
            double best=-1e30;
            Vec3 e0{},e1{};
            for (auto f:side.faces) {
                Vec3 p[]{side.points[f[0]],side.points[f[1]],side.points[f[2]]};
                if (std::abs(turn({p[0].z,0,p[0].y},{p[1].z,0,p[1].y},{p[2].z,0,p[2].y}))<1e-15) continue;
                for (int j=0;j<3;++j) {
                    Vec3 a=p[j],b=p[(j+1)%3];
                    if (std::abs(a.y-b.y)<1e-12 || mid<std::min(a.y,b.y) || mid>std::max(a.y,b.y)) continue;
                    const double z=a.z+(b.z-a.z)*(mid-a.y)/(b.y-a.y);
                    if (backward*z>best) { best=backward*z; e0=a; e1=b; }
                }
            }
            if (best<-1e20) continue;
            const auto sideZ=[&](double y){return e0.z+(e1.z-e0.z)*(y-e0.y)/(e1.y-e0.y);};
            double d0=backward*(backZ(lo)-sideZ(lo)),d1=backward*(backZ(hi)-sideZ(hi));
            if (d0<=0 && d1<=0) continue;
            if ((d0<0)!=(d1<0)) {
                const double y=lo+(hi-lo)*d0/(d0-d1);
                if (d0<0) lo=y; else hi=y;
            }
            std::vector<Vec3> patch{{sideZ(lo),0,lo},{sideZ(hi),0,hi},
                                    {backZ(hi),0,hi},{backZ(lo),0,lo}};
            double area=0;
            for (std::size_t j=0;j<patch.size();++j)
                area+=patch[j].x*patch[(j+1)%patch.size()].z-patch[j].z*patch[(j+1)%patch.size()].x;
            if (std::abs(area)<1e-16) continue;
            if (area<0) std::reverse(patch.begin(),patch.end());
            patches.push_back(std::move(patch));
        }
    }
    if (patches.empty()) return;
    const double clearance=.0001*std::sqrt(1+slopeU*slopeU+slopeV*slopeV);
    for (const auto& part:comparison.parts()) {
        if (part.name!="Tibia"+suffix && part.name!="Fibula"+suffix) continue;
        std::vector<Vec3> mapped,overlap;
        mapped.reserve(part.points.size());
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin;
            mapped.push_back({p.z,outward*p.x,p.y});
        }
        for (auto f:part.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            auto slab=heightClip({mapped[f[0]],mapped[f[1]],mapped[f[2]]},slopeU,slopeV,level-clearance,true);
            slab=heightClip(slab,slopeU,slopeV,level+clearance,false);
            overlap.insert(overlap.end(),slab.begin(),slab.end());
        }
        auto cut=hull(std::move(overlap));
        if (cut.size()<3) continue;
        std::vector<std::vector<Vec3>> next;
        for (const auto& patch:patches) {
            auto remaining=subtract(patch,cut);
            next.insert(next.end(),remaining.begin(),remaining.end());
        }
        patches=std::move(next);
    }
    for (const auto& patch:patches) for (std::size_t i=1;i+1<patch.size();++i) {
        if (std::abs(turn(patch[0],patch[i],patch[i+1]))<1e-14) continue;
        const int base=static_cast<int>(side.points.size());
        for (Vec3 p:{patch[0],patch[i],patch[i+1]})
            side.points.push_back({outward*(slopeU*p.x+slopeV*p.z+level),p.z,p.x});
        side.faces.push_back(outward<0 ? std::array<int,3>{base,base+1,base+2}
                                       : std::array<int,3>{base,base+2,base+1});
    }
}
} // namespace gtc::native
