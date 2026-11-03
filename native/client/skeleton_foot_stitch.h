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
        std::vector<Vec3> mapped;
        std::vector<std::vector<Vec3>> sections;
        mapped.reserve(obstacle.points.size());
        for (Vec3 p:obstacle.points) mapped.push_back(plane.local(p));
        for (auto f:obstacle.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            auto slab=heightClip({mapped[f[0]],mapped[f[1]],mapped[f[2]]},plane.slopeU,plane.slopeV,plane.level-clearance,true);
            slab=heightClip(slab,plane.slopeU,plane.slopeV,plane.level+clearance,false);
            if (slab.size()>=3) sections.push_back(std::move(slab));
        }
        // A skin can meet a sheet in two separated places. Taking one hull of
        // both removes the perfectly clear area between them (notably under
        // the little toe), so join only touching cross-section fragments.
        std::vector<std::size_t> group(sections.size());
        for (std::size_t i=0;i<group.size();++i) group[i]=i;
        const auto root=[&](std::size_t i) { while (group[i]!=i) i=group[i]; return i; };
        for (std::size_t i=0;i<sections.size();++i) for (std::size_t j=0;j<i;++j) {
            bool touching=false;
            for (Vec3 a:sections[i]) for (Vec3 b:sections[j])
                if (std::hypot(a.x-b.x,a.z-b.z)<1e-7) touching=true;
            if (touching) group[root(i)]=root(j);
        }
        for (std::size_t i=0;i<sections.size();++i) {
            if (root(i)!=i) continue;
            std::vector<Vec3> overlap;
            for (std::size_t j=0;j<sections.size();++j) if (root(j)==i)
                overlap.insert(overlap.end(),sections[j].begin(),sections[j].end());
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

// Rebuild the fitted sheets to their intersections, preserving the dorsal,
// heel and sole planes and the foot owner. A bounded medial continuation fits
// the great toe; no extra sculpt or toe bodies are introduced.
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
    struct ToeEnd { Vec3 back{},point{},direction{}; std::size_t part=0; };
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
        for (int k=0;k<8;++k) { back+=prism[std::size_t(k)]; front+=prism[std::size_t(k+8)]; }
        back=back/8; front=front/8;
        Vec3 direction=front-back; direction.y=0;
        if (length(direction)<1e-8) continue;
        ends.push_back({back,back+(front-back)*.4,normalized(direction),i});
    }
    if (ends.size()!=5) { preserve(); return; }
    std::sort(ends.begin(),ends.end(),[](const ToeEnd& a,const ToeEnd& b){return a.point.x<b.point.x;});
    std::size_t great=0;
    while (great<ends.size() && comparison.parts()[ends[great].part].name.rfind("FootProxP1_",0)!=0) ++great;
    if (great==ends.size()) { preserve(); return; }
    // Keep the dorsal and sole sheets on their original planes. A short
    // continuation of the medial side follows the outer width of the great
    // toe without forming a projecting end flap.
    const ToeEnd& big=ends[great];
    const double outward=great==0 ? -1.0 : 1.0;
    const int outer=(planes[1].normal.x*outward>0) ? 1 : 2;
    auto prism=comparison.toePrism(big.part,origin);
    comparison.resizeGreatToe(big.part,origin,prism);
    const double length40=length(Vec3{big.point.x-big.back.x,0,big.point.z-big.back.z});
    const Vec3 transition=big.back-big.direction*(length40*2.0);
    auto frontPlanes=planes;
    Plane& side=frontPlanes[std::size_t(outer)];
    double spread=0;
    for (double fraction:{.05,.39}) {
        Vec3 center{}; double low=1e30,high=-1e30;
        for (int k=0;k<8;++k) {
            Vec3 p=prism[std::size_t(k)]*(1-fraction)+prism[std::size_t(k+8)]*fraction;
            center+=p; low=std::min(low,p.x); high=std::max(high,p.x);
        }
        center=center/8;
        center.x=outward<0 ? low+(high-low)*.1 : high-(high-low)*.1;
        const double progress=dot(big.direction,center-transition);
        if (progress<1e-8) { preserve(); return; }
        for (int surface:{0,5}) {
            center.y=planes[std::size_t(surface)].world(planes[std::size_t(surface)].local(center)).y;
            spread=std::max(spread,(dot(side.normal,center-side.point)+.0001)/progress);
        }
    }
    if (spread>1e-12) {
        const Vec3 n=side.normal-big.direction*spread;
        const double offset=dot(side.normal,side.point)-spread*dot(big.direction,transition);
        const double across=dot(n,side.w);
        if (across<1e-6) { preserve(); return; }
        side.slopeU=-dot(n,side.u)/across; side.slopeV=-dot(n,side.v)/across;
        side.level=offset/across; side.normal=normalized(n);
        side.point=side.world({dot(transition,side.u),0,dot(transition,side.v)});
    }
    // Trim the lower corner back toward the toe outline. The extra vertical
    // facet meets the original sole rather than leaving a triangular wing.
    Plane toeSide=planes[std::size_t(outer)];
    const Plane& oldSide=planes[std::size_t(outer)];
    const Plane& solePlane=planes[5];
    Vec3 soleStart=transition;
    soleStart.y=solePlane.world(solePlane.local(soleStart)).y;
    soleStart=oldSide.world(oldSide.local(soleStart));
    double outside=-1e30;
    Vec3 target{};
    for (int k=0;k<8;++k) {
        const Vec3 p=prism[std::size_t(k)]*.61+prism[std::size_t(k+8)]*.39;
        target+=p; outside=std::max(outside,outward*p.x);
    }
    target=target/8; outside+=.0001;
    const double capDz=target.z-transition.z;
    if (std::abs(capDz)<1e-8) { preserve(); return; }
    toeSide.slopeU=(outside-outward*soleStart.x)/capDz;
    toeSide.slopeV=0;
    toeSide.level=outward*soleStart.x-toeSide.slopeU*transition.z;
    toeSide.normal=normalized(toeSide.w-toeSide.u*toeSide.slopeU);
    toeSide.point=toeSide.world({transition.z,0,transition.y});
    std::vector<Vec3> bounds;
    for (const auto* sheet:sheets) bounds.insert(bounds.end(),sheet->points.begin(),sheet->points.end());
    const auto cuts=obstacles(comparison,suffix,origin);
    std::array<MetatarsalPlane,6> rebuilt;
    const auto emit=[&](int index,const Plane& current,std::vector<std::vector<Vec3>> pieces) {
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
    };
    for (int index=0;index<6;++index) for (int phase=0;phase<2;++phase) {
        const auto& active=phase==0 ? planes : frontPlanes;
        const Plane& current=active[std::size_t(index)];
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
            const Plane& limit=active[std::size_t(other)];
            polygon=clipBy(polygon,[&](Vec3 p){return dot(limit.normal,current.world(p)-limit.point);});
        }
        std::vector<std::vector<Vec3>> pieces;
        for (std::size_t i=0;i<ends.size();++i) {
            if (phase>0 && i!=great) continue;
            auto section=polygon;
            if (i>0) {
                const double x=(ends[i-1].point.x+ends[i].point.x)*.5;
                section=clipBy(section,[&](Vec3 p){return x-current.world(p).x;});
            }
            if (i+1<ends.size()) {
                const double x=(ends[i].point.x+ends[i+1].point.x)*.5;
                section=clipBy(section,[&](Vec3 p){return current.world(p).x-x;});
            }
            const ToeEnd& end=ends[i];
            section=clipBy(section,[&](Vec3 p){return dot(end.direction,current.world(p)-end.point);});
            if (i==great) {
                if (phase>0) section=clipBy(section,[&](Vec3 p){
                    return dot(toeSide.normal,current.world(p)-toeSide.point);
                });
                section=clipBy(section,[&](Vec3 p){
                    const double d=dot(end.direction,current.world(p)-transition);
                    return phase==0 ? d : -d;
                });
            }
            if (section.size()>=3) pieces.push_back(std::move(section));
        }
        emit(index,current,std::move(pieces));
    }
    {
        const auto& active=frontPlanes;
        const Vec3 local=toeSide.local(big.back);
        double pad=.01;
        for (Vec3 p:bounds) {
            const Vec3 q=toeSide.local(p);
            pad=std::max({pad,std::abs(q.x-local.x)*2,std::abs(q.z-local.z)*2});
        }
        std::vector<Vec3> patch{{local.x-pad,0,local.z-pad},{local.x+pad,0,local.z-pad},
                                {local.x+pad,0,local.z+pad},{local.x-pad,0,local.z+pad}};
        for (const Plane& limit:active) {
            patch=clipBy(patch,[&](Vec3 p){return dot(limit.normal,toeSide.world(p)-limit.point);});
        }
        if (great>0) {
            const double x=(ends[great-1].point.x+big.point.x)*.5;
            patch=clipBy(patch,[&](Vec3 p){return x-toeSide.world(p).x;});
        }
        if (great+1<ends.size()) {
            const double x=(big.point.x+ends[great+1].point.x)*.5;
            patch=clipBy(patch,[&](Vec3 p){return toeSide.world(p).x-x;});
        }
        patch=clipBy(patch,[&](Vec3 p){return -dot(big.direction,toeSide.world(p)-transition);});
        patch=clipBy(patch,[&](Vec3 p){return dot(big.direction,toeSide.world(p)-big.point);});
        if (patch.size()>=3) emit(outer,toeSide,{std::move(patch)});
    }
    // Keep the original fitted sheets if a pathological profile produced no
    // bounded face, rather than replacing visible geometry with an empty mesh.
    for (const auto& mesh:rebuilt) if (mesh.faces.empty()) { preserve(); return; }
    combine(rebuilt);
}
} // namespace gtc::native
