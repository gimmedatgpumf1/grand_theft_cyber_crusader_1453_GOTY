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
struct ToeEnd {
    Vec3 back{},point{},direction{};
    double distance=0,prismLength=0;
    std::size_t part=0;
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

// The original fitted sheets stay untouched behind the transition. Each
// forward plane is fitted against the actual skin and bones in its toe band.
inline std::array<Plane,6> fitToePlanes(const ToeEnd& toe,const std::array<Plane,6>& original,
                                        const std::vector<Obstacle>& obstacles,double minX,double maxX) {
    auto fitted=original;
    if (toe.distance<1e-8) return fitted;
    double drop=1e30,rise=1e30; bool found=false;
    for (const auto& obstacle:obstacles) for (auto face:obstacle.faces) {
        if (face[0]<0 || face[1]<0 || face[2]<0 || std::size_t(face[0])>=obstacle.points.size() ||
            std::size_t(face[1])>=obstacle.points.size() || std::size_t(face[2])>=obstacle.points.size()) continue;
        std::vector<Vec3> triangle{obstacle.points[std::size_t(face[0])],
                                   obstacle.points[std::size_t(face[1])],obstacle.points[std::size_t(face[2])]};
        triangle=clipBy(triangle,[&](Vec3 p){return -dot(toe.direction,p-toe.back);});
        triangle=clipBy(triangle,[&](Vec3 p){return dot(toe.direction,p-toe.back)-toe.distance;});
        triangle=clipBy(triangle,[&](Vec3 p){return minX-p.x;});
        triangle=clipBy(triangle,[&](Vec3 p){return p.x-maxX;});
        for (Vec3 p:triangle) {
            const double fraction=dot(toe.direction,p-toe.back)/toe.distance;
            if (fraction<1e-5) continue;
            const double roof=original[0].world(original[0].local(p)).y;
            const double sole=original[5].world(original[5].local(p)).y;
            drop=std::min(drop,(roof-p.y-.0001)/fraction);
            rise=std::min(rise,(p.y-sole-.0001)/fraction);
            found=true;
        }
    }
    if (!found) return fitted;
    // A bone very close to the transition can otherwise demand an almost
    // vertical, unbounded continuation. Leave a local clearance cutout instead.
    const double maxRise=(toe.distance<toe.prismLength*.5 ? .6 : 2.0)*toe.distance;
    drop=std::clamp(drop,-maxRise,toe.distance);
    rise=std::clamp(rise,-toe.distance,toe.distance);
    for (auto [index,amount]:std::array<std::pair<int,double>,2>{{{0,drop},{5,rise}}}) {
        auto& plane=fitted[std::size_t(index)];
        const double factor=amount/toe.distance;
        plane.slopeU-=factor*toe.direction.x;
        plane.slopeV-=factor*toe.direction.z;
        plane.level+=factor*dot(toe.direction,toe.back);
        plane.normal=normalized(plane.w-plane.u*plane.slopeU-plane.v*plane.slopeV);
        plane.point=plane.world({toe.back.x,0,toe.back.z});
    }
    return fitted;
}

// Fan the outer side outward only where the toe prism projects beyond the
// original fitted side. It still meets the unchanged side at the transition.
template<class Comparison>
bool fitOuterSide(const Comparison& comparison,Vec3 origin,const ToeEnd& toe,
                  bool leftmost,Vec3 pivot,double fraction,bool allowNarrowing,
                  std::array<Plane,6>& planes) {
    int index=1;
    if ((planes[std::size_t(index)].normal.x<0)!=leftmost) index=2;
    Plane& side=planes[std::size_t(index)];
    auto prism=comparison.toePrism(toe.part,origin);
    comparison.resizeGreatToe(toe.part,origin,prism);
    if (prism.size()!=16) return false;
    double spread=allowNarrowing ? -1e30 : 0;
    {
        Vec3 center{}; double low=1e30,high=-1e30;
        for (int k=0;k<8;++k) {
            const Vec3 ring=prism[std::size_t(k)]*(1-fraction)+prism[std::size_t(k+8)]*fraction;
            center+=ring; low=std::min(low,ring.x); high=std::max(high,ring.x);
        }
        center=center/8;
        Vec3 p=center; p.x=leftmost ? low+(high-low)*.1 : high-(high-low)*.1;
        const double progress=dot(toe.direction,p-pivot);
        if (progress<1e-8) return false;
        for (int surface:{0,5}) {
            p.y=planes[std::size_t(surface)].world(planes[std::size_t(surface)].local(p)).y;
            spread=std::max(spread,(dot(side.normal,p-side.point)+.0001)/progress);
        }
    }
    if (std::abs(spread)<1e-12 || spread<=-1e29) return false;
    const Vec3 n=side.normal-toe.direction*spread;
    const double offset=dot(side.normal,side.point)-spread*dot(toe.direction,pivot);
    const double across=dot(n,side.w);
    if (across<1e-6) return false;
    side.slopeU=-dot(n,side.u)/across;
    side.slopeV=-dot(n,side.v)/across;
    side.level=offset/across;
    side.normal=normalized(n);
    side.point=side.world({dot(pivot,side.u),0,dot(pivot,side.v)});
    return true;
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
        const Vec3 unit=normalized(direction);
        // Begin the bend before the prism root so a continuation can rise
        // above a toe whose skin sits higher than the metatarsal plane.
        ends.push_back({back-unit*(length(direction)*.3),back+(front-back)*.4,
                        unit,length(direction)*.7,length(direction),i});
    }
    if (ends.size()!=5) { preserve(); return; }
    std::sort(ends.begin(),ends.end(),[](const ToeEnd& a,const ToeEnd& b){return a.point.x<b.point.x;});
    // Keep the original 0.1 mm metatarsal support point on the original
    // dorsal sheet, even when a nearby toe continuation has to rise.
    double nearest=1e30; Vec3 support{};
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)!=0) continue;
        for (Vec3 vertex:part.points) {
            const Vec3 p=comparison.mapPart(vertex,part)+origin;
            const double gap=dot(planes[0].normal,planes[0].point-p);
            if (gap>=0 && gap<nearest) { nearest=gap; support=p+planes[0].normal*gap; }
        }
    }
    if (nearest<1e20) for (std::size_t i=0;i<ends.size();++i) {
        const double minX=i>0 ? (ends[i-1].point.x+ends[i].point.x)*.5 : -1e30;
        const double maxX=i+1<ends.size() ? (ends[i].point.x+ends[i+1].point.x)*.5 : 1e30;
        if (support.x<minX || support.x>maxX) continue;
        ToeEnd& toe=ends[i];
        const double advance=dot(toe.direction,support-toe.back)+.0003;
        if (advance>0 && advance<toe.distance-.0003) { toe.back+=toe.direction*advance; toe.distance-=advance; }
    }
    std::vector<Vec3> bounds;
    for (const auto* sheet:sheets) bounds.insert(bounds.end(),sheet->points.begin(),sheet->points.end());
    const auto cuts=obstacles(comparison,suffix,origin);
    std::array<MetatarsalPlane,6> rebuilt;
    std::array<std::array<Plane,6>,5> fitted;
    for (std::size_t toeIndex=0;toeIndex<ends.size();++toeIndex) {
        const ToeEnd& toe=ends[toeIndex];
        const double minX=toeIndex>0 ? (ends[toeIndex-1].point.x+toe.point.x)*.5 : -1e30;
        const double maxX=toeIndex+1<ends.size() ? (toe.point.x+ends[toeIndex+1].point.x)*.5 : 1e30;
        auto first=fitToePlanes(toe,planes,cuts,minX,maxX);
        auto forward=first;
        const bool outer=toeIndex==0 || toeIndex+1==ends.size();
        const Vec3 join=toe.point-toe.direction*(toe.prismLength*.35);
        if (outer) {
            const bool widened=fitOuterSide(comparison,origin,toe,toeIndex==0,toe.back,.05,false,first);
            forward=first;
            fitOuterSide(comparison,origin,toe,toeIndex==0,join,.39,widened,forward);
        }
        fitted[toeIndex]=forward;
        for (int sectionIndex=0;sectionIndex<(outer ? 3 : 2);++sectionIndex) {
            const bool isForward=sectionIndex>0;
            const auto& sectionPlanes=!isForward ? planes : (outer && sectionIndex==1 ? first : forward);
            for (int index=0;index<6;++index) {
                const Plane& current=sectionPlanes[std::size_t(index)];
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
                    const Plane& limit=sectionPlanes[std::size_t(other)];
                    polygon=clipBy(polygon,[&](Vec3 p){return dot(limit.normal,current.world(p)-limit.point);});
                }
                if (toeIndex>0) {
                    const double x=(ends[toeIndex-1].point.x+toe.point.x)*.5;
                    polygon=clipBy(polygon,[&](Vec3 p){return x-current.world(p).x;});
                }
                if (toeIndex+1<ends.size()) {
                    const double x=(toe.point.x+ends[toeIndex+1].point.x)*.5;
                    polygon=clipBy(polygon,[&](Vec3 p){return current.world(p).x-x;});
                }
                polygon=clipBy(polygon,[&](Vec3 p){
                    const double distance=dot(toe.direction,current.world(p)-toe.back);
                    return isForward ? -distance : distance;
                });
                if (isForward) polygon=clipBy(polygon,[&](Vec3 p){return dot(toe.direction,current.world(p)-toe.point);});
                if (isForward && outer) polygon=clipBy(polygon,[&](Vec3 p){
                    const double d=dot(toe.direction,current.world(p)-join);
                    return sectionIndex==1 ? d : -d;
                });
                if (polygon.size()<3) continue;
                std::vector<std::vector<Vec3>> pieces{std::move(polygon)};
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
        }
    }
    // Join adjacent fitted toe bands. Without these narrow planar risers the
    // independently angled sheets leave visible slits into the blue bones.
    for (std::size_t band=0;band+1<ends.size();++band) {
        const double x=(ends[band].point.x+ends[band+1].point.x)*.5;
        const auto zAt=[&](const ToeEnd& toe,Vec3 anchor) {
            if (std::abs(toe.direction.z)<1e-8) return anchor.z;
            return anchor.z-toe.direction.x*(x-anchor.x)/toe.direction.z;
        };
        const double startA=zAt(ends[band],ends[band].back),startB=zAt(ends[band+1],ends[band+1].back);
        const double endA=zAt(ends[band],ends[band].point),endB=zAt(ends[band+1],ends[band+1].point);
        const double finish=std::min(endA,endB);
        std::vector<double> stops{std::min(startA,startB),std::max(startA,startB),finish};
        std::sort(stops.begin(),stops.end());
        for (int surface:{0,5}) for (std::size_t segment=0;segment+1<stops.size();++segment) {
            const double begin=stops[segment],end=std::min(stops[segment+1],finish);
            if (end-begin<1e-9 || begin>=finish) continue;
            const double mid=(begin+end)*.5;
            const Plane& a=mid>=startA ? fitted[band][std::size_t(surface)] : planes[std::size_t(surface)];
            const Plane& b=mid>=startB ? fitted[band+1][std::size_t(surface)] : planes[std::size_t(surface)];
            const auto height=[&](const Plane& p,double z) { return p.world(p.local({x,0,z})).y; };
            const double a0=height(a,begin),a1=height(a,end),b0=height(b,begin),b1=height(b,end);
            const double d0=a0-b0,d1=a1-b1;
            std::vector<double> split{begin,end};
            if (d0*d1<0) split.insert(split.begin()+1,begin+(end-begin)*d0/(d0-d1));
            for (std::size_t part=0;part+1<split.size();++part) {
                const double z0=split[part],z1=split[part+1];
                const double ay0=height(a,z0),ay1=height(a,z1),by0=height(b,z0),by1=height(b,z1);
                if (std::max(std::abs(ay0-by0),std::abs(ay1-by1))<1e-9) continue;
                Plane wall; wall.u={0,0,1}; wall.v={0,1,0}; wall.w={1,0,0}; wall.level=x;
                wall.normal={(ay0+ay1>by0+by1 ? 1.0 : -1.0)*(surface==0 ? 1.0 : -1.0),0,0};
                wall.point={x,0,0};
                std::vector<Vec3> polygon{{z0,0,ay0},{z1,0,ay1},{z1,0,by1},{z0,0,by0}};
                for (int limit=1;limit<=4 && polygon.size()>=3;++limit)
                    polygon=clipBy(polygon,[&](Vec3 p){return dot(planes[std::size_t(limit)].normal,wall.world(p)-planes[std::size_t(limit)].point);});
                if (polygon.size()<3) continue;
                // subtract() expects a counter-clockwise polygon in local coordinates.
                if (turn(polygon[0],polygon[1],polygon[2])<0) std::reverse(polygon.begin(),polygon.end());
                std::vector<std::vector<Vec3>> pieces{std::move(polygon)};
                cutObstacles(pieces,wall,cuts);
                auto& result=rebuilt[std::size_t(surface)];
                const bool positive=dot(cross(wall.u,wall.v),wall.normal)>0;
                for (const auto& piece:pieces) for (std::size_t i=1;i+1<piece.size();++i) {
                    if (std::abs(turn(piece[0],piece[i],piece[i+1]))<1e-14) continue;
                    const int base=static_cast<int>(result.points.size());
                    for (Vec3 p:{piece[0],piece[i],piece[i+1]}) result.points.push_back(wall.world(p));
                    result.faces.push_back(positive ? std::array<int,3>{base,base+1,base+2}
                                                    : std::array<int,3>{base,base+2,base+1});
                }
            }
        }
    }
    // Keep the original fitted sheets if a pathological profile produced no
    // bounded face, rather than replacing visible geometry with an empty mesh.
    for (const auto& mesh:rebuilt) if (mesh.faces.empty()) { preserve(); return; }
    combine(rebuilt);
}
} // namespace gtc::native
