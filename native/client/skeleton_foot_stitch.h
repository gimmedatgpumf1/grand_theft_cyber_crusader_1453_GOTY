#pragma once

#include "client/skeleton_sole.h"
#include <map>
#include <tuple>

namespace gtc::native {
namespace foot_stitch_detail {
using namespace metatarsal_plane_detail;

struct Plane {
    Vec3 point{},normal{},u{},v{},w{};
    double slopeU=0,slopeV=0,level=0;
    Vec3 local(Vec3 p) const { return {dot(p,u),dot(p,w),dot(p,v)}; }
    Vec3 world(Vec3 p) const { return u*p.x+v*p.z+w*(slopeU*p.x+slopeV*p.z+level); }
};

inline bool setup(const MetatarsalPlane& mesh,int index,Plane& plane) {
    if (!sole_detail::plane(mesh,plane.point,plane.normal)) return false;
    if (index==0 || index==5) {
        plane.u={1,0,0}; plane.v={0,0,1}; plane.w={0,plane.normal.y<0?-1.0:1.0,0};
    } else if (index==1 || index==2) {
        plane.u={0,0,1}; plane.v={0,1,0}; plane.w={plane.normal.x<0?-1.0:1.0,0,0};
    } else {
        plane.u={1,0,0}; plane.v={0,1,0}; plane.w={0,0,plane.normal.z<0?-1.0:1.0};
    }
    const double denominator=dot(plane.normal,plane.w);
    if (denominator<1e-8) return false;
    plane.slopeU=-dot(plane.normal,plane.u)/denominator;
    plane.slopeV=-dot(plane.normal,plane.v)/denominator;
    plane.level=dot(plane.normal,plane.point)/denominator;
    return true;
}

template<class Distance>
std::vector<Vec3> clipBy(const std::vector<Vec3>& polygon,const Distance& distance) {
    std::vector<Vec3> out;
    for (std::size_t i=0;i<polygon.size();++i) {
        const Vec3 a=polygon[i],b=polygon[(i+1)%polygon.size()];
        const double da=distance(a),db=distance(b);
        if (da<=0) out.push_back(a);
        if ((da<=0)!=(db<=0)) out.push_back(a+(b-a)*(da/(da-db)));
    }
    return out;
}

struct Obstacle {
    std::vector<Vec3> points;
    std::vector<std::array<int,3>> faces;
};

template<class Comparison>
std::vector<Obstacle> obstacles(const Comparison& comparison,const std::string& suffix,Vec3 origin) {
    std::vector<Obstacle> result;
    for (std::size_t i=0;i<comparison.parts().size();++i) {
        const auto& part=comparison.parts()[i];
        if (!part.name.ends_with(suffix)) continue;
        const bool bone=part.name.rfind("FootMeta",0)==0 || part.name.rfind("FootSesa",0)==0 ||
            part.name.rfind("FootProxP",0)==0 || part.name.rfind("FootMidP",0)==0 || part.name.rfind("FootDistP",0)==0 ||
            part.name.rfind("Tal_",0)==0 || part.name.rfind("Cal_",0)==0 || part.name.rfind("Nav_",0)==0 ||
            part.name.rfind("Cub_",0)==0 || part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 ||
            part.name.rfind("LatCun_",0)==0 || part.name.rfind("Tibia_",0)==0 || part.name.rfind("Fibula_",0)==0;
        if (bone) {
            Obstacle item;
            for (Vec3 p:part.points) item.points.push_back(comparison.mapPart(p,part)+origin);
            item.faces=part.faces; result.push_back(std::move(item));
        }
        if (part.name.rfind("FootDistP",0)==0) {
            auto skin=comparison.toeSurface(i,origin);
            result.push_back({std::move(skin.points),std::move(skin.faces)});
        }
    }
    return result;
}

inline void cutObstacles(std::vector<std::vector<Vec3>>& pieces,const Plane& plane,
                         const std::vector<Obstacle>& obstacles) {
    // Remove reference-pose cross sections, preserving the separately moving
    // bones and toes rather than letting an extended foot sheet cut through them.
    const double clearance=.0001*std::sqrt(1+plane.slopeU*plane.slopeU+plane.slopeV*plane.slopeV);
    for (const auto& obstacle:obstacles) {
        if (pieces.empty()) return;
        std::vector<Vec3> mapped,overlap;
        mapped.reserve(obstacle.points.size());
        for (Vec3 p:obstacle.points) mapped.push_back(plane.local(p));
        for (auto f:obstacle.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            auto slab=heightClip({mapped[f[0]],mapped[f[1]],mapped[f[2]]},plane.slopeU,plane.slopeV,plane.level-clearance,true);
            slab=heightClip(slab,plane.slopeU,plane.slopeV,plane.level+clearance,false);
            overlap.insert(overlap.end(),slab.begin(),slab.end());
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
}
// Weld coincident points into a single indexed mesh and split T-junctions so
// neighbouring triangles actually share the intersection edges. Face normals
// remain flat in the renderer, so welding does not round the fitted planes.
inline MetatarsalPlane weld(const std::array<MetatarsalPlane,6>& sheets) {
    MetatarsalPlane result; result.owner=sheets[0].owner;
    using Key=std::tuple<long long,long long,long long>;
    std::map<Key,std::vector<int>> bins;
    constexpr double tolerance=1e-8;
    const auto vertex=[&](Vec3 p) {
        const auto x=static_cast<long long>(std::llround(p.x/tolerance));
        const auto y=static_cast<long long>(std::llround(p.y/tolerance));
        const auto z=static_cast<long long>(std::llround(p.z/tolerance));
        for (int dx=-1;dx<=1;++dx) for (int dy=-1;dy<=1;++dy) for (int dz=-1;dz<=1;++dz) {
            const auto found=bins.find({x+dx,y+dy,z+dz});
            if (found==bins.end()) continue;
            for (int index:found->second) if (length(result.points[std::size_t(index)]-p)<=tolerance) return index;
        }
        int index=static_cast<int>(result.points.size());
        result.points.push_back(p); bins[{x,y,z}].push_back(index); return index;
    };
    for (const auto& sheet:sheets) {
        std::vector<int> remap; remap.reserve(sheet.points.size());
        for (Vec3 p:sheet.points) remap.push_back(vertex(p));
        for (auto face:sheet.faces) {
            const int a=remap[std::size_t(face[0])],b=remap[std::size_t(face[1])],c=remap[std::size_t(face[2])];
            if (a!=b && b!=c && c!=a) result.faces.push_back({a,b,c});
        }
    }
    const std::size_t pointCount=result.points.size();
    std::vector<std::array<int,3>> faces;
    for (auto face:result.faces) {
        std::vector<int> boundary;
        for (int edge=0;edge<3;++edge) {
            const int a=face[edge],b=face[(edge+1)%3];
            const Vec3 start=result.points[std::size_t(a)],line=result.points[std::size_t(b)]-start;
            const double squared=dot(line,line);
            boundary.push_back(a);
            if (squared<tolerance*tolerance) continue;
            std::vector<std::pair<double,int>> between;
            for (std::size_t i=0;i<pointCount;++i) {
                if (int(i)==a || int(i)==b) continue;
                Vec3 delta=result.points[i]-start;
                const double t=dot(delta,line)/squared;
                if (t<=1e-8 || t>=1-1e-8) continue;
                if (length(delta-line*t)<=tolerance) between.emplace_back(t,int(i));
            }
            std::sort(between.begin(),between.end());
            for (auto item:between) if (item.second!=boundary.back()) boundary.push_back(item.second);
        }
        if (boundary.size()==3) { faces.push_back(face); continue; }
        const Vec3 center=(result.points[std::size_t(face[0])]+result.points[std::size_t(face[1])]+result.points[std::size_t(face[2])])/3;
        const int middle=static_cast<int>(result.points.size()); result.points.push_back(center);
        for (std::size_t i=0;i<boundary.size();++i) {
            int a=boundary[i],b=boundary[(i+1)%boundary.size()];
            if (a!=b && length(cross(result.points[std::size_t(a)]-center,result.points[std::size_t(b)]-center))>1e-16)
                faces.push_back({middle,a,b});
        }
    }
    result.faces=std::move(faces);
    return result;
}
} // namespace foot_stitch_detail

// Rebuild the six existing fitted planes to their mutual intersections. Their
// equations and foot owner stay unchanged; no extra sculpt or toe bodies are
// introduced. The front footprint extends partway along the proximal toes.
template<class Comparison>
void stitchFootPlanes(const Comparison& comparison,const std::string& suffix,Vec3 origin,
                      MetatarsalPlane& top,MetatarsalPlane& medial,MetatarsalPlane& lateral,
                      std::array<MetatarsalPlane,2>& heel,MetatarsalPlane& sole) {
    using namespace foot_stitch_detail;
    std::array<MetatarsalPlane*,6> sheets{&top,&medial,&lateral,&heel[0],&heel[1],&sole};
    const auto combine=[&](const std::array<MetatarsalPlane,6>& geometry) {
        MetatarsalPlane joined=weld(geometry);
        for (auto* sheet:sheets) *sheet={};
        top=std::move(joined);
    };
    const auto preserve=[&] {
        std::array<MetatarsalPlane,6> geometry;
        for (std::size_t i=0;i<sheets.size();++i) geometry[i]=std::move(*sheets[i]);
        combine(geometry);
    };
    std::array<Plane,6> planes;
    for (int i=0;i<6;++i) if (!setup(*sheets[std::size_t(i)],i,planes[std::size_t(i)])) { preserve(); return; }
    struct ToeEnd { Vec3 back{},point{}; double left=0,right=0; };
    std::vector<ToeEnd> ends;
    // Each toe has its own hard end border, measured from the back to the
    // front of its proximal prism (after the existing great-toe scaling).
    for (std::size_t i=0;i<comparison.parts().size();++i) {
        const auto& part=comparison.parts()[i];
        if (!part.name.ends_with(suffix) || part.name.rfind("FootProxP",0)!=0) continue;
        auto prism=comparison.toePrism(i,origin);
        if (prism.size()!=16) continue;
        comparison.resizeGreatToe(i,origin,prism);
        Vec3 back{},front{};
        double left=1e30,right=-1e30;
        for (int k=0;k<8;++k) {
            back+=prism[std::size_t(k)]; front+=prism[std::size_t(k+8)];
            const double at40=prism[std::size_t(k)].x*.6+prism[std::size_t(k+8)].x*.4;
            left=std::min({left,prism[std::size_t(k)].x,at40});
            right=std::max({right,prism[std::size_t(k)].x,at40});
        }
        back=back/8; front=front/8;
        if (length(Vec3{front.x-back.x,0,front.z-back.z})<1e-8) continue;
        ends.push_back({back,back+(front-back)*.4,left,right});
    }
    if (ends.size()!=5) { preserve(); return; }
    std::sort(ends.begin(),ends.end(),[](const ToeEnd& a,const ToeEnd& b){return a.point.x<b.point.x;});
    // In plan view the front is one connected, angular line through the five
    // 40% points, returning to the side sheets at the outer toes' back rings.
    // The slanted sides meet the roof and sole at different X coordinates;
    // include both, so the end does not retreat behind the toe root.
    const auto sideExtent=[&](const ToeEnd& end,double outward) {
        const Plane& side=planes[planes[1].normal.x*outward>0 ? 1 : 2];
        double extent=outward<0 ? end.left : end.right;
        for (int surface:{0,5}) {
            const Plane& face=planes[std::size_t(surface)];
            const double a=dot(face.normal,face.point)-face.normal.z*end.back.z;
            const double b=dot(side.normal,side.point)-side.normal.z*end.back.z;
            const double det=face.normal.x*side.normal.y-side.normal.x*face.normal.y;
            if (std::abs(det)<1e-8) continue;
            const double x=(a*side.normal.y-b*face.normal.y)/det;
            extent=outward<0 ? std::min(extent,x) : std::max(extent,x);
        }
        return extent;
    };
    std::vector<Vec3> border;
    border.push_back({std::min(sideExtent(ends.front(),-1),ends.front().point.x-1e-8),0,ends.front().back.z});
    for (const ToeEnd& end:ends) border.push_back(end.point);
    border.push_back({std::max(sideExtent(ends.back(),1),ends.back().point.x+1e-8),0,ends.back().back.z});
    for (std::size_t i=1;i<border.size();++i)
        if (border[i].x-border[i-1].x<1e-8) { preserve(); return; }
    std::vector<Vec3> bounds;
    for (const auto* sheet:sheets) bounds.insert(bounds.end(),sheet->points.begin(),sheet->points.end());
    const auto cuts=obstacles(comparison,suffix,origin);
    std::array<MetatarsalPlane,6> rebuilt;
    for (int index=0;index<6;++index) {
        const Plane& current=planes[std::size_t(index)];
        double loU=1e30,hiU=-1e30,loV=1e30,hiV=-1e30;
        for (Vec3 p:bounds) {
            p=current.local(p);
            loU=std::min(loU,p.x); hiU=std::max(hiU,p.x);
            loV=std::min(loV,p.z); hiV=std::max(hiV,p.z);
        }
        const double padding=2*std::max(hiU-loU,hiV-loV)+.01;
        std::vector<Vec3> polygon{{loU-padding,0,loV-padding},{hiU+padding,0,loV-padding},
                                  {hiU+padding,0,hiV+padding},{loU-padding,0,hiV+padding}};
        for (int other=0;other<6 && polygon.size()>=3;++other) if (other!=index) {
            const Plane& limit=planes[std::size_t(other)];
            polygon=clipBy(polygon,[&](Vec3 p){return dot(limit.normal,current.world(p)-limit.point);});
        }
        // Clip each segment separately, then weld their common endpoints.
        // The first and last segments continue to the side planes, so the
        // line reaches the outer edge even where the side is slanted.
        std::vector<std::vector<Vec3>> pieces;
        for (std::size_t i=0;i+1<border.size();++i) {
            const Vec3 a=border[i],b=border[i+1];
            auto section=polygon;
            if (i>0) section=clipBy(section,[&](Vec3 p){return a.x-current.world(p).x;});
            if (i+2<border.size()) section=clipBy(section,[&](Vec3 p){return current.world(p).x-b.x;});
            const double slope=(b.z-a.z)/(b.x-a.x);
            section=clipBy(section,[&](Vec3 p){
                const Vec3 world=current.world(p);
                return world.z-(a.z+(world.x-a.x)*slope);
            });
            if (section.size()>=3) pieces.push_back(std::move(section));
        }
        cutObstacles(pieces,current,cuts);
        auto& result=rebuilt[std::size_t(index)]; result.owner=sheets[std::size_t(index)]->owner;
        const bool positive=dot(cross(current.u,current.v),current.normal)>0;
        for (const auto& piece:pieces) for (std::size_t i=1;i+1<piece.size();++i) {
            if (std::abs(turn(piece[0],piece[i],piece[i+1]))<1e-14) continue;
            const int base=static_cast<int>(result.points.size());
            for (Vec3 p:{piece[0],piece[i],piece[i+1]}) result.points.push_back(current.world(p));
            result.faces.push_back(positive ? std::array<int,3>{base,base+1,base+2}
                                            : std::array<int,3>{base,base+2,base+1});
        }
    }
    // Keep the original fitted sheets if a pathological profile produced no
    // bounded face, rather than replacing visible geometry with an empty mesh.
    for (const auto& mesh:rebuilt) if (mesh.faces.empty()) { preserve(); return; }
    combine(rebuilt);
}
} // namespace gtc::native
