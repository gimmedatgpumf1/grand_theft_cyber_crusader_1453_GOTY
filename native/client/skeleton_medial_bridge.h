#pragma once

#include "client/skeleton_metatarsal_plane.h"
#include <limits>

namespace gtc::native {

// Add only the missing portion of the dorsal plane between its existing
// outline and a foot side sheet. The bridge is coplanar with that plane.
template<class Comparison>
void bridgeFootSidePlane(const Comparison& comparison,const std::string& suffix,Vec3 origin,
                           const MetatarsalPlane& side,MetatarsalPlane& top) {
    using namespace metatarsal_plane_detail;
    const auto plane=[](const MetatarsalPlane& mesh,Vec3& point,Vec3& normal) {
        double largest=0;
        for (const auto& f:mesh.faces) {
            Vec3 p=mesh.points[f[0]],q=mesh.points[f[1]],r=mesh.points[f[2]];
            Vec3 n=cross(q-p,r-p);
            if (length(n)>largest) { largest=length(n); point=p; normal=n; }
        }
        if (largest<1e-20) return false;
        normal=normal/largest;
        return true;
    };
    Vec3 ta{},tn{},sa{},sn{};
    if (!plane(top,ta,tn) || !plane(side,sa,sn)) return;
    if (tn.y<0) tn=tn*-1;
    const double determinant=tn.x*sn.y-sn.x*tn.y;
    if (std::abs(determinant)<1e-10 || std::abs(sn.x)<1e-10 || std::abs(tn.y)<1e-10) return;
    const double outward=sn.x<0 ? -1.0 : 1.0;
    const double td=dot(tn,ta),sd=dot(sn,sa);
    const auto joinX=[&](double z) {
        const double t=td-tn.z*z,s=sd-sn.z*z;
        return (t*sn.y-s*tn.y)/determinant;
    };
    const auto roofY=[&](double x,double z) { return (td-tn.x*x-tn.z*z)/tn.y; };
    // Only bridge z ranges where the side mesh actually reaches the roof.
    std::vector<std::pair<double,double>> ranges;
    for (const auto& f:side.faces) for (int i=0;i<3;++i) {
        const Vec3 p=side.points[f[i]],q=side.points[f[(i+1)%3]];
        if (std::abs(dot(tn,p)-td)>1e-8 || std::abs(dot(tn,q)-td)>1e-8 || std::abs(p.z-q.z)<1e-10) continue;
        ranges.emplace_back(std::min(p.z,q.z),std::max(p.z,q.z));
    }
    if (ranges.empty()) return;
    std::sort(ranges.begin(),ranges.end());
    std::vector<std::pair<double,double>> merged;
    for (auto r:ranges) {
        if (!merged.empty() && r.first<=merged.back().second+1e-9) merged.back().second=std::max(merged.back().second,r.second);
        else merged.push_back(r);
    }
    std::vector<std::vector<Vec3>> patches;
    for (auto range:merged) {
        std::vector<double> knots{range.first,range.second};
        for (Vec3 p:top.points) if (p.z>range.first && p.z<range.second) knots.push_back(p.z);
        std::sort(knots.begin(),knots.end());
        knots.erase(std::unique(knots.begin(),knots.end(),[](double a,double b) { return std::abs(a-b)<1e-10; }),knots.end());
        for (std::size_t i=0;i+1<knots.size();++i) {
            double lo=knots[i],hi=knots[i+1];
            if (hi-lo<1e-10) continue;
            const double mid=(lo+hi)*.5;
            double best=-std::numeric_limits<double>::infinity();
            Vec3 e0{},e1{};
            for (const auto& f:top.faces) {
                Vec3 tri[]{top.points[f[0]],top.points[f[1]],top.points[f[2]]};
                if (std::abs(turn(tri[0],tri[1],tri[2]))<1e-15) continue;
                for (int j=0;j<3;++j) {
                    Vec3 a=tri[j],b=tri[(j+1)%3];
                    if (std::abs(b.z-a.z)<1e-12 || mid<std::min(a.z,b.z) || mid>std::max(a.z,b.z)) continue;
                    const double x=a.x+(b.x-a.x)*(mid-a.z)/(b.z-a.z);
                    if (outward*x>best) { best=outward*x; e0=a; e1=b; }
                }
            }
            if (!std::isfinite(best)) continue;
            const auto edgeX=[&](double z) { return e0.x+(e1.x-e0.x)*(z-e0.z)/(e1.z-e0.z); };
            double d0=outward*(joinX(lo)-edgeX(lo)),d1=outward*(joinX(hi)-edgeX(hi));
            if (d0<=0 && d1<=0) continue;
            if ((d0<0)!=(d1<0)) {
                const double z=lo+(hi-lo)*d0/(d0-d1);
                if (d0<0) lo=z; else hi=z;
            }
            std::vector<Vec3> patch{{edgeX(lo),0,lo},{edgeX(hi),0,hi},
                                    {joinX(hi),0,hi},{joinX(lo),0,lo}};
            double area=0;
            for (std::size_t j=0;j<patch.size();++j) area+=patch[j].x*patch[(j+1)%patch.size()].z-patch[j].z*patch[(j+1)%patch.size()].x;
            if (std::abs(area)<1e-16) continue;
            if (area<0) std::reverse(patch.begin(),patch.end());
            patches.push_back(std::move(patch));
        }
    }
    if (patches.empty()) return;
    const double ax=-tn.x/tn.y,az=-tn.z/tn.y,level=td/tn.y;
    const double clearance=.0001/tn.y;
    // Trim only reference-pose overlaps. A moving ankle or toe can still
    // intersect the rigid foot-attached sheet, as with the original roof.
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix)) continue;
        const bool toe=part.name.rfind("FootProxP",0)==0 || part.name.rfind("FootMidP",0)==0 || part.name.rfind("FootDistP",0)==0;
        const bool leg=part.name.rfind("Tibia_",0)==0 || part.name.rfind("Fibula_",0)==0;
        const bool foot=toe || leg || part.name.rfind("FootMeta",0)==0 || part.name.rfind("FootSesa",0)==0 ||
            part.name.rfind("Tal_",0)==0 || part.name.rfind("Cal_",0)==0 || part.name.rfind("Nav_",0)==0 ||
            part.name.rfind("Cub_",0)==0 || part.name.rfind("MedCun_",0)==0 ||
            part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!foot) continue;
        std::vector<Vec3> mapped,overlap;
        for (Vec3 p:part.points) mapped.push_back(comparison.mapPart(p,part)+origin);
        for (const auto& f:part.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            auto polygon=above({mapped[f[0]],mapped[f[1]],mapped[f[2]]},ax,az,level-clearance);
            if (leg) polygon=heightClip(polygon,ax,az,level+clearance,false);
            overlap.insert(overlap.end(),polygon.begin(),polygon.end());
        }
        auto cut=hull(std::move(overlap));
        if (cut.size()<3) continue;
        std::vector<std::vector<Vec3>> next;
        for (const auto& patch:patches) {
            auto remaining=subtract(patch,cut);
            next.insert(next.end(),remaining.begin(),remaining.end());
        }
        patches=std::move(next);
        if (patches.empty()) return;
    }
    for (const auto& patch:patches) for (std::size_t i=1;i+1<patch.size();++i) {
        if (std::abs(turn(patch[0],patch[i],patch[i+1]))<1e-14) continue;
        const int base=static_cast<int>(top.points.size());
        for (Vec3 p:{patch[0],patch[i+1],patch[i]}) top.points.push_back({p.x,roofY(p.x,p.z),p.z});
        top.faces.push_back({base,base+1,base+2});
    }
}
} // namespace gtc::native
