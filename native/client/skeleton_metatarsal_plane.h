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
    std::vector<int> owners;
};

namespace metatarsal_plane_detail {
inline double turn(Vec3 a,Vec3 b,Vec3 c) {
    return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
}
inline std::vector<Vec3> hull(std::vector<Vec3> points) {
    if (points.size()<3) return {};
    std::sort(points.begin(),points.end(),[](Vec3 a,Vec3 b) { return a.x==b.x ? a.z<b.z : a.x<b.x; });
    std::vector<Vec3> out; out.reserve(points.size()*2);
    for (Vec3 p:points) {
        while (out.size()>=2 && turn(out[out.size()-2],out.back(),p)<=0) out.pop_back();
        out.push_back(p);
    }
    const std::size_t lower=out.size();
    for (std::size_t i=points.size()-1;i-- > 0;) {
        const Vec3 p=points[i];
        while (out.size()>lower && turn(out[out.size()-2],out.back(),p)<=0) out.pop_back();
        out.push_back(p);
    }
    if (out.size()<4) return {};
    out.pop_back(); return out;
}
inline std::vector<Vec3> clip(const std::vector<Vec3>& polygon, Vec3 a,Vec3 b,bool inside) {
    std::vector<Vec3> out;
    for (std::size_t i=0;i<polygon.size();++i) {
        const Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
        const double dp=turn(a,b,p),dq=turn(a,b,q);
        const bool kp=inside ? dp>=0 : dp<=0, kq=inside ? dq>=0 : dq<=0;
        if (kp) out.push_back(p);
        if (kp!=kq) out.push_back(p+(q-p)*(dp/(dp-dq)));
    }
    return out;
}
inline std::vector<Vec3> intersect(std::vector<Vec3> polygon,const std::vector<Vec3>& boundary) {
    for (std::size_t i=0;i<boundary.size() && polygon.size()>=3;++i)
        polygon=clip(polygon,boundary[i],boundary[(i+1)%boundary.size()],true);
    return polygon.size()>=3 ? polygon : std::vector<Vec3>{};
}
// Subtract a convex region, retaining disjoint convex pieces of the plane.
inline std::vector<std::vector<Vec3>> subtract(const std::vector<Vec3>& polygon,const std::vector<Vec3>& cut) {
    std::vector<std::vector<Vec3>> out;
    auto remaining=polygon;
    for (std::size_t i=0;i<cut.size() && remaining.size()>=3;++i) {
        const Vec3 a=cut[i],b=cut[(i+1)%cut.size()];
        auto outside=clip(remaining,a,b,false);
        if (outside.size()>=3) out.push_back(std::move(outside));
        remaining=clip(remaining,a,b,true);
    }
    return out;
}
inline std::vector<Vec3> heightClip(const std::vector<Vec3>& polygon,double ax,double az,double level,bool keepAbove) {
    std::vector<Vec3> out;
    for (std::size_t i=0;i<polygon.size();++i) {
        const Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
        const double dp=p.y-ax*p.x-az*p.z-level,dq=q.y-ax*q.x-az*q.z-level;
        const bool kp=keepAbove ? dp>=0 : dp<=0, kq=keepAbove ? dq>=0 : dq<=0;
        if (kp) out.push_back(p);
        if (kp!=kq) out.push_back(p+(q-p)*(dp/(dp-dq)));
    }
    return out;
}
inline std::vector<Vec3> above(const std::vector<Vec3>& polygon,double ax,double az,double level) {
    return heightClip(polygon,ax,az,level,true);
}
} // namespace metatarsal_plane_detail

// A flat, foot-attached plane extending from the metatarsals to the ankle.
// Its inclination follows the upper surfaces, tilts towards the little toe,
// and clears the adjacent fixed tarsals. The supporting metatarsal is 0.1 mm
// away along the plane normal. Reference-pose toe and ankle overlaps are cut out.
template<class Comparison>
MetatarsalPlane makeMetatarsalPlane(const Comparison& comparison, const std::string& suffix, Vec3 origin) {
    using namespace metatarsal_plane_detail;
    MetatarsalPlane result;
    std::vector<Vec3> vertices;
    std::array<double,2> endX{};
    std::array<int,2> endCount{};
    int found=0;
    const auto& parts=comparison.parts();
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& part=parts[i];
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)!=0 ||
            part.name.size()!=9+suffix.size() || part.name[8]<'1' || part.name[8]>'5') continue;
        ++found;
        if (part.name[8]=='3') result.owner=static_cast<int>(i);
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin;
            vertices.push_back(p);
            if (part.name[8]=='1' || part.name[8]=='5') {
                const int end=part.name[8]=='1' ? 0 : 1;
                endX[end]+=p.x; ++endCount[end];
            }
        }
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
    if (!endCount[0] || !endCount[1]) return {};
    const double lateral=endX[1]/endCount[1]>endX[0]/endCount[0] ? 1.0 : -1.0;
    // Lower the lateral edge towards the little toe, mirrored on the two feet.
    const double slopeX=(xy*zz-zy*xz)/determinant-lateral*.20;
    double slopeZ=(zy*xx-xy*xz)/determinant;
    // Extend the metatarsal outline to the upper talus, where it meets the leg.
    std::vector<Vec3> footprint=vertices;
    for (const auto& part:parts) if (part.name=="Tal"+suffix && !part.points.empty()) {
        std::vector<Vec3> talus;
        double bottom=1e30,top=-1e30;
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin;
            talus.push_back(p); bottom=std::min(bottom,p.y); top=std::max(top,p.y);
        }
        for (Vec3 p:talus) if (p.y>=bottom+(top-bottom)*.75) footprint.push_back(p);
    }
    auto boundary=hull(std::move(footprint));
    if (boundary.size()<3) return {};
    std::vector<Vec3> fixed;
    std::vector<std::vector<Vec3>> fixedTriangles;
    std::array<std::vector<std::vector<Vec3>>,5> toes;
    std::array<std::vector<std::vector<Vec3>>,2> ankle;
    double heelZ=0; int heelCount=0;
    for (const auto& part:parts) {
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)==0) continue;
        const bool toe=part.name.rfind("FootProxP",0)==0 || part.name.rfind("FootMidP",0)==0 || part.name.rfind("FootDistP",0)==0;
        const bool tibia=part.name.rfind("Tibia_",0)==0;
        const bool fibula=part.name.rfind("Fibula_",0)==0;
        const bool foot=toe || tibia || fibula || part.name.rfind("FootSesa",0)==0 || part.name.rfind("Tal_",0)==0 ||
            part.name.rfind("Cal_",0)==0 || part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 ||
            part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!foot) continue;
        std::vector<Vec3> mapped; mapped.reserve(part.points.size());
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin; mapped.push_back(p);
            if (part.name.rfind("Cal_",0)==0) { heelZ+=p.z; ++heelCount; }
        }
        int digit=-1;
        if (toe) for (char c:part.name) if (c>='1' && c<='5') { digit=c-'1'; break; }
        for (const auto& face:part.faces) {
            if (face[0]<0 || face[1]<0 || face[2]<0 || std::size_t(face[0])>=mapped.size() ||
                std::size_t(face[1])>=mapped.size() || std::size_t(face[2])>=mapped.size()) continue;
            auto polygon=intersect({mapped[face[0]],mapped[face[1]],mapped[face[2]]},boundary);
            if (polygon.empty()) continue;
            if (digit>=0) toes[std::size_t(digit)].push_back(std::move(polygon));
            else if (tibia || fibula) ankle[tibia ? 0 : 1].push_back(std::move(polygon));
            else {
                fixed.insert(fixed.end(),polygon.begin(),polygon.end());
                fixedTriangles.push_back(std::move(polygon));
            }
        }
    }
    const auto support=[&](const std::vector<Vec3>& points,double zSlope) {
        double level=-1e30;
        for (Vec3 p:points) level=std::max(level,p.y-slopeX*p.x-zSlope*p.z);
        return level;
    };
    // Raise the back by rotating about the fitted metatarsals, not by lifting
    // the whole plane away from them. The scan finds the closest valid angle.
    bool fixedClear=fixed.empty();
    if (!fixed.empty()) {
        const double sign=heelCount && mean.z<heelZ/double(heelCount) ? -1.0 : 1.0;
        const double initial=slopeZ;
        for (int step=0;step<=4000;++step) {
            const double candidate=initial-sign*double(step)*.001;
            if (support(fixed,candidate)<=support(vertices,candidate)) { slopeZ=candidate; fixedClear=true; break; }
        }
    }
    const double clearance=.0001*std::sqrt(1+slopeX*slopeX+slopeZ*slopeZ);
    const double level=support(vertices,slopeZ);
    const double intercept=level+clearance;
    // Stop the proximal edge at the first intersection with the ankle bones,
    // rather than leaving pointed flaps behind the leg's projected outline.
    const double towardToe=heelCount && mean.z<heelZ/double(heelCount) ? -1.0 : 1.0;
    double ankleEnd=-1e30;
    for (const auto& triangles:ankle) for (const auto& polygon:triangles) {
        for (std::size_t i=0;i<polygon.size();++i) {
            const Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
            const double dp=p.y-slopeX*p.x-slopeZ*p.z-intercept;
            const double dq=q.y-slopeX*q.x-slopeZ*q.z-intercept;
            if ((dp>=0)!=(dq>=0)) {
                const Vec3 point=p+(q-p)*(dp/(dp-dq));
                ankleEnd=std::max(ankleEnd,towardToe*point.z);
            }
        }
    }
    if (ankleEnd>-1e20) {
        const double z=towardToe*(ankleEnd+.0001);
        boundary=clip(boundary,{0,0,z},{towardToe,0,z},true);
    }
    // A toe can project into the end of the metatarsal outline. Cut only the
    // overlapping toe outlines out of the plane; don't raise the whole plane.
    std::vector<std::vector<Vec3>> pieces{boundary};
    const auto exclude=[&](const std::vector<std::vector<Vec3>>& triangles,bool ankleBone=false) {
        std::vector<Vec3> projected;
        for (const auto& triangle:triangles) {
            auto overlap=above(triangle,slopeX,slopeZ,level);
            // The shin extends far above the sheet. Only its intersection with
            // the plane's clearance slab defines the ankle cutout.
            if (ankleBone) overlap=heightClip(overlap,slopeX,slopeZ,intercept+clearance,false);
            projected.insert(projected.end(),overlap.begin(),overlap.end());
        }
        const auto cut=hull(std::move(projected));
        if (cut.size()<3) return;
        std::vector<std::vector<Vec3>> next;
        for (const auto& piece:pieces) {
            auto remaining=subtract(piece,cut);
            next.insert(next.end(),remaining.begin(),remaining.end());
        }
        pieces=std::move(next);
    };
    for (const auto& triangles:toes) exclude(triangles);
    for (const auto& triangles:ankle) exclude(triangles,true);
    // If no suitable tilt exists, trim fixed-bone overlap instead of silently
    // leaving a sheet that intersects it.
    if (!fixedClear) exclude(fixedTriangles);
    for (const auto& piece:pieces) {
        for (std::size_t i=1;i+1<piece.size();++i) {
            if (std::abs(turn(piece[0],piece[i],piece[i+1]))<1e-12*(maxX-minX)*(maxZ-minZ)) continue;
            const int base=static_cast<int>(result.points.size());
            for (Vec3 p:{piece[0],piece[i+1],piece[i]})
                result.points.push_back({p.x,slopeX*p.x+slopeZ*p.z+intercept,p.z});
            result.faces.push_back({base,base+1,base+2});
        }
    }
    return result;
}
} // namespace gtc::native
