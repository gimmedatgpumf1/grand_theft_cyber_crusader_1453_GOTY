#pragma once

#include "client/skeleton_metatarsal_plane.h"

namespace gtc::native {
namespace sole_detail {
using namespace metatarsal_plane_detail;

inline bool plane(const MetatarsalPlane& mesh,Vec3& point,Vec3& normal) {
    double largest=0;
    for (auto f:mesh.faces) {
        Vec3 a=mesh.points[f[0]],b=mesh.points[f[1]],c=mesh.points[f[2]];
        Vec3 n=cross(b-a,c-a);
        if (length(n)>largest) { largest=length(n); point=a; normal=n; }
    }
    if (largest<1e-20) return false;
    normal=normal/largest;
    return true;
}

inline std::vector<Vec3> trim(const std::vector<Vec3>& polygon,const Vec3 point,const Vec3 normal,double y) {
    std::vector<Vec3> out;
    for (std::size_t i=0;i<polygon.size();++i) {
        Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
        double a=dot(normal,Vec3{p.x,y,p.z}-point), b=dot(normal,Vec3{q.x,y,q.z}-point);
        if (a<=0) out.push_back(p);
        if ((a<=0)!=(b<=0)) out.push_back(p+(q-p)*(a/(a-b)));
    }
    return out;
}

// Extend the existing side only where a sole edge meets its plane. The added
// triangles are coplanar with that side and owned by the same rigid foot.
template<class Comparison>
void joinSide(const Comparison& comparison,const std::string& suffix,Vec3 origin,
              const MetatarsalPlane& sole,MetatarsalPlane& side,double bottom) {
    Vec3 anchor{},normal{};
    if (!plane(side,anchor,normal) || std::abs(normal.x)<1e-10) return;
    const double outward=normal.x<0 ? -1.0 : 1.0;
    const double su=-outward*normal.z/normal.x, sv=-outward*normal.y/normal.x;
    const double level=outward*dot(normal,anchor)/normal.x;
    const double clearance=.0001*std::sqrt(1+su*su+sv*sv);
    std::vector<std::pair<double,double>> ranges;
    for (auto f:sole.faces) for (int i=0;i<3;++i) {
        Vec3 p=sole.points[f[i]],q=sole.points[f[(i+1)%3]];
        if (std::abs(dot(normal,p-anchor))>1e-8 || std::abs(dot(normal,q-anchor))>1e-8 ||
            std::abs(p.z-q.z)<1e-9) continue;
        ranges.emplace_back(std::min(p.z,q.z),std::max(p.z,q.z));
    }
    std::vector<std::vector<Vec3>> patches;
    for (auto range:ranges) {
        std::vector<double> knots{range.first,range.second};
        for (Vec3 p:side.points) if (p.z>range.first && p.z<range.second) knots.push_back(p.z);
        std::sort(knots.begin(),knots.end());
        knots.erase(std::unique(knots.begin(),knots.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),knots.end());
        for (std::size_t i=0;i+1<knots.size();++i) {
            double lo=knots[i],hi=knots[i+1];
            if (hi-lo<1e-10) continue;
            const double mid=(lo+hi)*.5;
            double best=1e30;
            Vec3 e0{},e1{};
            for (auto f:side.faces) {
                Vec3 p[]{side.points[f[0]],side.points[f[1]],side.points[f[2]]};
                if (std::abs(turn({p[0].z,0,p[0].y},{p[1].z,0,p[1].y},{p[2].z,0,p[2].y}))<1e-15) continue;
                for (int j=0;j<3;++j) {
                    Vec3 a=p[j],b=p[(j+1)%3];
                    if (std::abs(a.z-b.z)<1e-12 || mid<std::min(a.z,b.z) || mid>std::max(a.z,b.z)) continue;
                    double y=a.y+(b.y-a.y)*(mid-a.z)/(b.z-a.z);
                    if (y<best) { best=y; e0=a; e1=b; }
                }
            }
            if (best>1e20) continue;
            const auto edgeY=[&](double z){return e0.y+(e1.y-e0.y)*(z-e0.z)/(e1.z-e0.z);};
            double d0=edgeY(lo)-bottom,d1=edgeY(hi)-bottom;
            if (d0<=0 && d1<=0) continue;
            if ((d0<0)!=(d1<0)) {
                const double z=lo+(hi-lo)*d0/(d0-d1);
                if (d0<0) lo=z; else hi=z;
            }
            std::vector<Vec3> patch{{lo,0,bottom},{hi,0,bottom},{hi,0,edgeY(hi)},{lo,0,edgeY(lo)}};
            double area=0;
            for (std::size_t j=0;j<patch.size();++j)
                area+=patch[j].x*patch[(j+1)%patch.size()].z-patch[j].z*patch[(j+1)%patch.size()].x;
            if (std::abs(area)<1e-16) continue;
            if (area<0) std::reverse(patch.begin(),patch.end());
            patches.push_back(std::move(patch));
        }
    }
    if (patches.empty()) return;
    // A separately articulated toe may intersect an extension at the front.
    // Remove its reference-pose cross-section, never changing the toe itself.
    for (std::size_t i=0;i<comparison.parts().size();++i) {
        const auto& part=comparison.parts()[i];
        if (!part.name.ends_with(suffix) || part.name.rfind("FootDistP",0)!=0) continue;
        const auto skin=comparison.toeSurface(i,origin);
        std::vector<Vec3> overlap;
        for (auto f:skin.faces) {
            std::vector<Vec3> tri;
            for (int index:f) {
                Vec3 p=skin.points[std::size_t(index)]; tri.push_back({p.z,outward*p.x,p.y});
            }
            auto slab=heightClip(tri,su,sv,level-clearance,true);
            slab=heightClip(slab,su,sv,level+clearance,false);
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
        int base=static_cast<int>(side.points.size());
        for (Vec3 p:{patch[0],patch[i],patch[i+1]})
            side.points.push_back({outward*(su*p.x+sv*p.z+level),p.z,p.x});
        side.faces.push_back(outward<0 ? std::array<int,3>{base,base+1,base+2}
                                       : std::array<int,3>{base,base+2,base+1});
    }
}
} // namespace sole_detail

template<class Comparison>
MetatarsalPlane makeFootSole(const Comparison& comparison,const std::string& suffix,Vec3 origin,
                             MetatarsalPlane& medial,MetatarsalPlane& lateral,
                             const std::array<MetatarsalPlane,2>& heel) {
    using namespace metatarsal_plane_detail;
    MetatarsalPlane sole;
    if (medial.faces.empty() || lateral.faces.empty() || heel[0].faces.empty() || heel[1].faces.empty()) return sole;
    sole.owner=medial.owner;
    double bottom=1e30,loX=1e30,hiX=-1e30,loZ=1e30,hiZ=-1e30,frontLo=1e30,frontHi=-1e30;
    double heelZ=0,metaZ=0; int heelCount=0,metaCount=0;
    std::vector<std::vector<Vec3>> metatarsals;
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix)) continue;
        const bool cal=part.name=="Cal"+suffix, meta=part.name.rfind("FootMeta",0)==0;
        const bool fixed=cal || meta || part.name.rfind("FootSesa",0)==0 || part.name.rfind("Tal_",0)==0 ||
            part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 ||
            part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!fixed) continue;
        if (meta) metatarsals.emplace_back();
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin;
            bottom=std::min(bottom,p.y); loX=std::min(loX,p.x); hiX=std::max(hiX,p.x);
            loZ=std::min(loZ,p.z); hiZ=std::max(hiZ,p.z);
            if (cal) { heelZ+=p.z; ++heelCount; }
            if (meta) { metaZ+=p.z; ++metaCount; frontLo=std::min(frontLo,p.z); frontHi=std::max(frontHi,p.z); metatarsals.back().push_back(p); }
        }
    }
    if (!heelCount || !metaCount || bottom>1e20) return {};
    bottom-=.0001;
    const bool positive=metaZ/double(metaCount)>heelZ/double(heelCount);
    const double front=positive ? frontHi : frontLo;
    const double pad=std::max(hiX-loX,hiZ-loZ);
    double back=positive ? loZ-pad : hiZ+pad;
    double z0=std::min(back,front),z1=std::max(back,front);
    std::vector<Vec3> boundary{{loX-pad,0,z0},{hiX+pad,0,z0},{hiX+pad,0,z1},{loX-pad,0,z1}};
    // Follow the ends of the side sheets and the individual metatarsals,
    // instead of projecting a wide square underneath the separate toes.
    std::array<Vec3,2> frontEdge{};
    int sideIndex=0;
    for (const auto* side:std::array<const MetatarsalPlane*,2>{&medial,&lateral}) {
        Vec3 point{},normal{};
        if (!sole_detail::plane(*side,point,normal) || std::abs(normal.x)<1e-10) return {};
        double z=positive ? -1e30 : 1e30;
        for (Vec3 p:side->points) z=positive ? std::max(z,p.z) : std::min(z,p.z);
        frontEdge[std::size_t(sideIndex++)]={(dot(normal,point)-normal.y*bottom-normal.z*z)/normal.x,0,z};
    }
    std::vector<Vec3> footprint{frontEdge[0],frontEdge[1],{loX-pad,0,back},{hiX+pad,0,back}};
    for (const auto& points:metatarsals) {
        if (points.empty()) continue;
        double low=points.front().z,high=low;
        for (Vec3 p:points) { low=std::min(low,p.z); high=std::max(high,p.z); }
        for (Vec3 p:points)
            if (positive ? p.z>=high-(high-low)*.15 : p.z<=low+(high-low)*.15) footprint.push_back({p.x,0,p.z});
    }
    boundary=intersect(std::move(boundary),hull(std::move(footprint)));
    for (const auto* mesh:std::array<const MetatarsalPlane*,4>{&medial,&lateral,&heel[0],&heel[1]}) {
        Vec3 point{},normal{};
        if (!sole_detail::plane(*mesh,point,normal)) return {};
        boundary=sole_detail::trim(boundary,point,normal,bottom);
        if (boundary.size()<3) return {};
    }
    std::vector<std::vector<Vec3>> pieces{std::move(boundary)};
    // Preserve the individual toes. Cut away only their reference-pose skin
    // where it reaches the sole instead of binding toes to the foot sheet.
    for (std::size_t i=0;i<comparison.parts().size();++i) {
        const auto& part=comparison.parts()[i];
        if (!part.name.ends_with(suffix) || part.name.rfind("FootDistP",0)!=0) continue;
        const auto skin=comparison.toeSurface(i,origin);
        std::vector<Vec3> overlap;
        for (auto f:skin.faces) {
            auto low=heightClip({skin.points[std::size_t(f[0])],skin.points[std::size_t(f[1])],
                                 skin.points[std::size_t(f[2])]},0,0,bottom+.0001,false);
            overlap.insert(overlap.end(),low.begin(),low.end());
        }
        auto cut=hull(std::move(overlap));
        if (cut.size()<3) continue;
        std::vector<std::vector<Vec3>> next;
        for (const auto& piece:pieces) {
            auto remaining=subtract(piece,cut);
            next.insert(next.end(),remaining.begin(),remaining.end());
        }
        pieces=std::move(next);
    }
    for (const auto& piece:pieces) for (std::size_t i=1;i+1<piece.size();++i) {
        if (std::abs(turn(piece[0],piece[i],piece[i+1]))<1e-14) continue;
        int base=static_cast<int>(sole.points.size());
        for (Vec3 p:{piece[0],piece[i],piece[i+1]}) sole.points.push_back({p.x,bottom,p.z});
        sole.faces.push_back({base,base+1,base+2});
    }
    sole_detail::joinSide(comparison,suffix,origin,sole,medial,bottom);
    sole_detail::joinSide(comparison,suffix,origin,sole,lateral,bottom);
    return sole;
}
} // namespace gtc::native
