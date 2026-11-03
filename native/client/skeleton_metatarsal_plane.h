#pragma once

#include "client/raster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native {
struct MetatarsalPlane {
    std::vector<Vec3> points;
    std::vector<std::array<int,3>> faces;
    int owner=-1;
};

// One flat plane over the projection of the five mapped metatarsals. Fit the
// inclination to their upper surfaces, then lift it to clear the bones under its
// footprint by 0.1 mm vertically. A single flat plane cannot stay exactly 0.1 mm
// from every point on the differently shaped bones.
template<class Comparison>
MetatarsalPlane makeMetatarsalPlane(const Comparison& comparison, const std::string& suffix, Vec3 origin) {
    MetatarsalPlane result;
    std::vector<Vec3> vertices;
    int found=0;
    const auto& parts=comparison.parts();
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& part=parts[i];
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)!=0 ||
            part.name.size()!=9+suffix.size() || part.name[8]<'1' || part.name[8]>'5') continue;
        ++found;
        if (part.name[8]=='3') result.owner=static_cast<int>(i);
        for (Vec3 p:part.points) vertices.push_back(comparison.mapPart(p,part)+origin);
    }
    if (found!=5 || result.owner<0 || vertices.size()<3) return {};
    double minX=vertices[0].x,maxX=minX,minZ=vertices[0].z,maxZ=minZ;
    for (Vec3 p:vertices) {
        minX=std::min(minX,p.x); maxX=std::max(maxX,p.x);
        minZ=std::min(minZ,p.z); maxZ=std::max(maxZ,p.z);
    }
    if (maxX-minX<1e-8 || maxZ-minZ<1e-8) return {};
    constexpr int bins=12;
    std::array<Vec3,bins*bins> upper{};
    std::array<bool,bins*bins> used{};
    for (Vec3 p:vertices) {
        const int x=std::clamp(int((p.x-minX)/(maxX-minX)*bins),0,bins-1);
        const int z=std::clamp(int((p.z-minZ)/(maxZ-minZ)*bins),0,bins-1);
        const int index=z*bins+x;
        if (!used[index] || p.y>upper[index].y) { upper[index]=p; used[index]=true; }
    }
    Vec3 mean{}; int count=0;
    for (std::size_t i=0;i<upper.size();++i) if (used[i]) { mean+=upper[i]; ++count; }
    if (count<3) return {};
    mean=mean/double(count);
    double xx=0,xz=0,zz=0,xy=0,zy=0;
    for (std::size_t i=0;i<upper.size();++i) if (used[i]) {
        const Vec3 d=upper[i]-mean;
        xx+=d.x*d.x; xz+=d.x*d.z; zz+=d.z*d.z; xy+=d.x*d.y; zy+=d.z*d.y;
    }
    const double determinant=xx*zz-xz*xz;
    if (determinant<1e-20) return {};
    const double slopeX=(xy*zz-zy*xz)/determinant;
    const double slopeZ=(zy*xx-xy*xz)/determinant;
    // A convex outline keeps the flat plane bounded by the metatarsals rather
    // than extending a rectangle over the ankle or the individual toes.
    std::sort(vertices.begin(),vertices.end(),[](Vec3 a,Vec3 b) {
        return a.x==b.x ? a.z<b.z : a.x<b.x;
    });
    const auto turn=[](Vec3 a,Vec3 b,Vec3 c) { return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x); };
    std::vector<Vec3> hull; hull.reserve(vertices.size()*2);
    for (Vec3 p:vertices) {
        while (hull.size()>=2 && turn(hull[hull.size()-2],hull.back(),p)<=0) hull.pop_back();
        hull.push_back(p);
    }
    const std::size_t lower=hull.size();
    for (std::size_t i=vertices.size()-1;i-- > 0;) {
        const Vec3 p=vertices[i];
        while (hull.size()>lower && turn(hull[hull.size()-2],hull.back(),p)<=0) hull.pop_back();
        hull.push_back(p);
    }
    if (hull.size()<4) return {};
    hull.pop_back();
    double intercept=-1e30;
    const auto include=[&](Vec3 p) { intercept=std::max(intercept,p.y-slopeX*p.x-slopeZ*p.z); };
    for (Vec3 p:vertices) include(p);
    // Adjacent foot bones can overlap the projected metatarsal outline. Clip
    // their triangles to that outline before finding the highest point, so
    // neither those bones nor the metatarsals pierce the sheet.
    for (const auto& part:parts) {
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)==0) continue;
        const bool foot=part.name.rfind("Foot",0)==0 || part.name.rfind("Tal_",0)==0 ||
            part.name.rfind("Cal_",0)==0 || part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 ||
            part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!foot) continue;
        std::vector<Vec3> mapped; mapped.reserve(part.points.size());
        for (Vec3 p:part.points) mapped.push_back(comparison.mapPart(p,part)+origin);
        for (const auto& face:part.faces) {
            if (face[0]<0 || face[1]<0 || face[2]<0 || std::size_t(face[0])>=mapped.size() ||
                std::size_t(face[1])>=mapped.size() || std::size_t(face[2])>=mapped.size()) continue;
            std::vector<Vec3> polygon{mapped[face[0]],mapped[face[1]],mapped[face[2]]};
            for (std::size_t edge=0;edge<hull.size() && !polygon.empty();++edge) {
                const Vec3 a=hull[edge], b=hull[(edge+1)%hull.size()];
                std::vector<Vec3> clipped;
                for (std::size_t i=0;i<polygon.size();++i) {
                    const Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
                    const double dp=turn(a,b,p),dq=turn(a,b,q);
                    if (dp>=0) clipped.push_back(p);
                    if ((dp>=0)!=(dq>=0)) clipped.push_back(p+(q-p)*(dp/(dp-dq)));
                }
                polygon=std::move(clipped);
            }
            for (Vec3 p:polygon) include(p);
        }
    }
    intercept+=0.0001;
    for (Vec3 p:hull) result.points.push_back({p.x,slopeX*p.x+slopeZ*p.z+intercept,p.z});
    for (std::size_t i=1;i+1<result.points.size();++i)
        result.faces.push_back({0,static_cast<int>(i+1),static_cast<int>(i)});
    return result;
}
} // namespace gtc::native
