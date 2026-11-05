#pragma once

#include "client/skeleton_sole.h"
#include "client/skeleton_toe_transition.h"
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
    bool leg=false;
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
            part.name.rfind("Tal_",0)==0 || part.name.rfind("Cal_",0)==0 || part.name.rfind("Nav_",0)==0 ||
            part.name.rfind("Cub_",0)==0 || part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 ||
            part.name.rfind("LatCun_",0)==0 || part.name.rfind("Tibia_",0)==0 || part.name.rfind("Fibula_",0)==0;
        if (bone) {
            Obstacle item;
            item.leg=part.name.rfind("Tibia_",0)==0 || part.name.rfind("Fibula_",0)==0;
            for (Vec3 p:part.points) item.points.push_back(comparison.mapPart(p,part)+origin);
            item.faces=part.faces; result.push_back(std::move(item));
        }
    }
    return result;
}

inline void cutObstacles(std::vector<std::vector<Vec3>>& pieces,const Plane& plane,
                         const std::vector<Obstacle>& obstacles) {
    // Preserve clearance around the fixed foot and ankle bones. The toe bones
    // and flesh are deliberately not obstacles: the sheets continue through them.
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
        // Keep disconnected cross-sections separate instead of cutting away
        // the clear area between them.
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
            // Widen the cut slightly in the plane, so the triangulated border
            // remains outside the ankle after clipping and vertex welding.
            if (obstacle.leg) {
                std::vector<Vec3> expanded;
                for (Vec3 p:cut) for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z)
                    expanded.push_back({p.x+x*.00002,0,p.z+z*.00002});
                cut=hull(std::move(expanded));
            }
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

// Rebuild the fitted planes to their mutual intersections. The five dorsal
// sections are foot-owned; the front footprint reaches 50% of each proximal toe.
template<class Comparison>
bool stitchFootPlanes(const Comparison& comparison,const std::string& suffix,Vec3 origin,
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
    const auto toeRoots=toe_transition::roots(comparison,suffix,origin);
    if(toeRoots.size()!=5) {preserve();return false;}
    std::array<Plane,6> planes;
    for (int i=0;i<6;++i) if (!setup(*sheets[std::size_t(i)],i,planes[std::size_t(i)])) { preserve(); return false; }
    struct ToeEnd { Vec3 point{},leftMid{},rightMid{}; double left=0,right=0,upper=0; };
    std::vector<ToeEnd> ends;
    // Each toe has its own hard end border, measured from the back to the
    // front of its proximal prism (after the existing great-toe scaling).
    for (std::size_t i=0;i<comparison.parts().size();++i) {
        const auto& part=comparison.parts()[i];
        if (!part.name.ends_with(suffix) || part.name.rfind("FootProxP",0)!=0) continue;
        auto prism=comparison.toePrism(i,origin);
        if (prism.size()!=16) continue;
        comparison.resizeGreatToe(i,origin,prism);
        Vec3 back{},front{},leftMid{},rightMid{};
        double left=1e30,right=-1e30,upper=-1e30;
        leftMid.x=1e30; rightMid.x=-1e30;
        for (int k=0;k<8;++k) {
            back+=prism[std::size_t(k)]; front+=prism[std::size_t(k+8)];
            const Vec3 midpoint=(prism[std::size_t(k)]+prism[std::size_t(k+8)])*.5;
            upper=std::max(upper,midpoint.y);
            if (midpoint.x<leftMid.x) leftMid=midpoint;
            if (midpoint.x>rightMid.x) rightMid=midpoint;
            left=std::min({left,prism[std::size_t(k)].x,midpoint.x});
            right=std::max({right,prism[std::size_t(k)].x,midpoint.x});
        }
        back=back/8; front=front/8;
        if (length(Vec3{front.x-back.x,0,front.z-back.z})<1e-8) continue;
        ends.push_back({(back+front)*.5,leftMid,rightMid,left,right,upper});
    }
    if (ends.size()!=5) { preserve(); return false; }
    std::sort(ends.begin(),ends.end(),[](const ToeEnd& a,const ToeEnd& b){return a.point.x<b.point.x;});
    // Five expanded dorsal planes, one aimed at each toe. Adjacent planes
    // share the line from the rear anchor to their common front boundary.
    double rearZ=1e30,frontZ=-1e30;
    for (const auto* sheet:sheets) for (Vec3 p:sheet->points) {
        rearZ=std::min(rearZ,p.z); frontZ=std::max(frontZ,p.z);
    }
    rearZ-=std::max(frontZ-rearZ,.02)*.5;
    Vec3 apex{(ends.front().point.x+ends.back().point.x)*.5,0,rearZ};
    apex.y=planes[0].slopeU*apex.x+planes[0].slopeV*apex.z+planes[0].level;
    std::array<Vec3,6> stations{};
    stations[0]=ends.front().leftMid; stations[5]=ends.back().rightMid;
    stations[0].y=ends.front().upper; stations[5].y=ends.back().upper;
    for (int i=1;i<5;++i) {
        stations[std::size_t(i)]=(ends[std::size_t(i-1)].point+ends[std::size_t(i)].point)*.5;
        stations[std::size_t(i)].y=(ends[std::size_t(i-1)].upper+ends[std::size_t(i)].upper)*.5;
    }
    const auto boundaryX=[&](int i,double z) {
        const Vec3 p=stations[std::size_t(i)];
        return apex.x+(p.x-apex.x)*(z-apex.z)/(p.z-apex.z);
    };
    for (int i=0;i<6;++i) if (stations[std::size_t(i)].z-apex.z<1e-6) {preserve();return false;}
    for (int i=1;i<6;++i)
        if (boundaryX(i,frontZ)-boundaryX(i-1,frontZ)<1e-8) {preserve();return false;}
    std::array<Plane,5> roofs;
    const auto fitSections=[&](double lift,double rearRise=0) {
        for (int i=0;i<5;++i) {
            Vec3 a=stations[std::size_t(i)]-apex,b=stations[std::size_t(i+1)]-apex;
            a.y+=lift-rearRise; b.y+=lift-rearRise;
            const double determinant=a.x*b.z-a.z*b.x;
            if (std::abs(determinant)<1e-12) return false;
            auto& roof=roofs[std::size_t(i)];
            roof.u={1,0,0};roof.v={0,0,1};roof.w={0,1,0};
            roof.slopeU=(a.y*b.z-a.z*b.y)/determinant;
            roof.slopeV=(a.x*b.y-a.y*b.x)/determinant;
            roof.level=apex.y+rearRise-roof.slopeU*apex.x-roof.slopeV*apex.z;
        }
        return true;
    };
    if (!fitSections(0)) {preserve();return false;}
    std::vector<std::pair<int,Vec3>> supports;
    std::size_t metaCount=0;
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)!=0) continue;
        std::vector<Vec3> mapped; mapped.reserve(part.points.size());
        for (Vec3 p:part.points) mapped.push_back(comparison.mapPart(p,part)+origin);
        for (auto face:part.faces) {
            if (face[0]<0 || face[1]<0 || face[2]<0 || std::size_t(face[0])>=mapped.size() ||
                std::size_t(face[1])>=mapped.size() || std::size_t(face[2])>=mapped.size()) continue;
            for (int i=0;i<5;++i) {
                auto poly=std::vector<Vec3>{mapped[std::size_t(face[0])],mapped[std::size_t(face[1])],mapped[std::size_t(face[2])]};
                if (i>0) poly=clipBy(poly,[&](Vec3 p){return boundaryX(i,p.z)-p.x;});
                if (i<4 && !poly.empty()) poly=clipBy(poly,[&](Vec3 p){return p.x-boundaryX(i+1,p.z);});
                for (Vec3 p:poly) supports.emplace_back(i,p);
            }
        }
    }
    metaCount=supports.size();
    if (!metaCount) {preserve();return false;}
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix)) continue;
        const bool tarsal=part.name.rfind("Tal_",0)==0 || part.name.rfind("Cal_",0)==0 ||
            part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 ||
            part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!tarsal) continue;
        for (Vec3 point:part.points) {
            const Vec3 p=comparison.mapPart(point,part)+origin;
            const double oldY=planes[0].slopeU*p.x+planes[0].slopeV*p.z+planes[0].level;
            if (p.y>oldY-.0001*std::sqrt(1+planes[0].slopeU*planes[0].slopeU+planes[0].slopeV*planes[0].slopeV)) continue;
            int i=0; while (i<4 && p.x>boundaryX(i+1,p.z)) ++i;
            supports.emplace_back(i,p);
        }
    }
    // Adjust the rear anchor and toe edge together: the nearest metatarsal
    // remains 0.1 mm away while the already-covered tarsals stay covered.
    const auto metaClearance=[&](double lift,double rearRise) {
        if (!fitSections(lift,rearRise)) return -1e30;
        std::array<double,5> normalLength{};
        for (int i=0;i<5;++i) {const auto& r=roofs[std::size_t(i)];
            normalLength[std::size_t(i)]=std::sqrt(1+r.slopeU*r.slopeU+r.slopeV*r.slopeV);}
        double result=1e30;
        for (std::size_t k=0;k<metaCount;++k) {
            const auto [i,p]=supports[k]; const auto& r=roofs[std::size_t(i)];
            result=std::min(result,(r.slopeU*p.x+r.slopeV*p.z+r.level-p.y)/normalLength[std::size_t(i)]);
        }
        return result;
    };
    const double range=std::max(frontZ-rearZ,.02);
    const auto fitClear=[&](double rearRise) {
        double low=-range,high=range;
        if (metaClearance(low,rearRise)>.0001 || metaClearance(high,rearRise)<.0001) return false;
        for (int i=0;i<28;++i) {const double mid=(low+high)*.5;
            if (metaClearance(mid,rearRise)<.0001) low=mid;else high=mid;}
        fitSections(high+1e-10,rearRise);
        for (std::size_t k=metaCount;k<supports.size();++k) {
            const auto [i,p]=supports[k]; const auto& r=roofs[std::size_t(i)];
            const double distance=(r.slopeU*p.x+r.slopeV*p.z+r.level-p.y)/
                std::sqrt(1+r.slopeU*r.slopeU+r.slopeV*r.slopeV);
            if (distance<.0001-1e-10) return false;
        }
        return true;
    };
    double low=0,high=range;
    if (!fitClear(0)) {
        if (!fitClear(high)) {preserve();return false;}
        for (int i=0;i<24;++i) {const double mid=(low+high)*.5; if (fitClear(mid)) high=mid; else low=mid;}
        fitClear(high);
    }
    for (auto& roof:roofs) {roof.point={0,roof.level,0};roof.normal=normalized(Vec3{-roof.slopeU,1,-roof.slopeV});}
    // Transverse folds run orthogonally across the foot at the tarsal and
    // metatarsal centers. The same fold angle is applied to every toe sector:
    // this makes both ends of each cross-cut coincide with its neighbours.
    double tarsalZ=0,metaZ=0; int tarsalBones=0,metaBones=0;
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix) || part.points.empty()) continue;
        const bool meta=part.name.rfind("FootMeta",0)==0;
        const bool tarsal=part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 ||
            part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!meta && !tarsal) continue;
        double lo=1e30,hi=-1e30;
        for (Vec3 p:part.points) {p=comparison.mapPart(p,part)+origin;lo=std::min(lo,p.z);hi=std::max(hi,p.z);}
        if (meta) {metaZ+=(lo+hi)*.5;++metaBones;} else {tarsalZ+=(lo+hi)*.5;++tarsalBones;}
    }
    if (!tarsalBones || !metaBones) {preserve();return false;}
    tarsalZ/=tarsalBones; metaZ/=metaBones;
    if (metaZ-tarsalZ<1e-6) {preserve();return false;}
    struct CrossSupport { int sector,band; Vec3 point; };
    std::vector<CrossSupport> metaSupports,tarsalSupports;
    // Clip metatarsal triangles at both the longitudinal and transverse
    // boundaries so an extremum on a new fold is included in the fit.
    for (const auto& part:comparison.parts()) {
        if (!part.name.ends_with(suffix) || part.name.rfind("FootMeta",0)!=0) continue;
        std::vector<Vec3> mapped; mapped.reserve(part.points.size());
        for (Vec3 p:part.points) mapped.push_back(comparison.mapPart(p,part)+origin);
        for (auto f:part.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            for (int section=0;section<5;++section) {
                std::vector<Vec3> poly{mapped[std::size_t(f[0])],mapped[std::size_t(f[1])],mapped[std::size_t(f[2])]};
                if (section>0) poly=clipBy(poly,[&](Vec3 p){return boundaryX(section,p.z)-p.x;});
                if (section<4 && !poly.empty()) poly=clipBy(poly,[&](Vec3 p){return p.x-boundaryX(section+1,p.z);});
                for (int band=0;band<3 && !poly.empty();++band) {
                    auto partPoly=poly;
                    if (band>0) partPoly=clipBy(partPoly,[&](Vec3 p){return (band==1?tarsalZ:metaZ)-p.z;});
                    if (band<2 && !partPoly.empty()) partPoly=clipBy(partPoly,[&](Vec3 p){return p.z-(band==0?tarsalZ:metaZ);});
                    for (Vec3 p:partPoly) metaSupports.push_back({section,band,p});
                }
            }
        }
    }
    for (std::size_t k=metaCount;k<supports.size();++k) {
        const auto [section,p]=supports[k];
        tarsalSupports.push_back({section,p.z<tarsalZ?0:(p.z<metaZ?1:2),p});
    }
    if (metaSupports.empty()) {preserve();return false;}
    std::array<std::array<Plane,5>,3> crossRoofs{};
    // A modest sagittal bias supplies room for the front fold to follow the
    // toes without needing a near-vertical rear section to clear the tarsals.
    constexpr double sagittalBias=.12;
    const auto crossFit=[&](double frontSlope,double rearSlope) {
        for (int band=0;band<3;++band) for (int section=0;section<5;++section) {
            auto& roof=crossRoofs[std::size_t(band)][std::size_t(section)];
            roof=roofs[std::size_t(section)];
            roof.slopeV-=sagittalBias; roof.level+=sagittalBias*tarsalZ;
            if (band==0) {roof.slopeV-=rearSlope;roof.level+=rearSlope*tarsalZ;}
            if (band==2) {roof.slopeV+=frontSlope;roof.level-=frontSlope*metaZ;}
        }
        std::array<std::array<double,5>,3> normalLengths{};
        for (int band=0;band<3;++band) for (int section=0;section<5;++section) {
            const auto& r=crossRoofs[std::size_t(band)][std::size_t(section)];
            normalLengths[std::size_t(band)][std::size_t(section)]=std::sqrt(1+r.slopeU*r.slopeU+r.slopeV*r.slopeV);
        }
        double lift=-1e30;
        for (const auto& support:metaSupports) {
            const auto& r=crossRoofs[std::size_t(support.band)][std::size_t(support.sector)];
            const Vec3 p=support.point;
            lift=std::max(lift,p.y+.0001*normalLengths[std::size_t(support.band)][std::size_t(support.sector)]-
                (r.slopeU*p.x+r.slopeV*p.z+r.level));
        }
        for (auto& band:crossRoofs) for (auto& r:band) r.level+=lift;
        for (const auto& support:tarsalSupports) {
            const auto& r=crossRoofs[std::size_t(support.band)][std::size_t(support.sector)];
            const Vec3 p=support.point;
            if (r.slopeU*p.x+r.slopeV*p.z+r.level-p.y <
                .0001*normalLengths[std::size_t(support.band)][std::size_t(support.sector)]-1e-10) return false;
        }
        return true;
    };
    const auto fitTarsals=[&](double frontSlope) {
        if (crossFit(frontSlope,-.12)) return true;
        double low=-.12,high=.3;
        if (!crossFit(frontSlope,high)) return false;
        for (int i=0;i<20;++i) {double mid=(low+high)*.5;
            if (crossFit(frontSlope,mid)) high=mid; else low=mid;}
        return crossFit(frontSlope,high);
    };
    const auto toeError=[&]() {
        double error=0;
        for (const auto& end:ends) {
            const Vec3 p=end.point; int section=0;
            while (section<4 && p.x>boundaryX(section+1,p.z)) ++section;
            const int band=p.z<tarsalZ?0:(p.z<metaZ?1:2);
            const auto& r=crossRoofs[std::size_t(band)][std::size_t(section)];
            error+=r.slopeU*p.x+r.slopeV*p.z+r.level-end.upper;
        }
        return error/5;
    };
    // Fit the toe-directed fold to the mean toe tops while retaining the
    // exact nearest metatarsal clearance and prior tarsal coverage.
    double bestSlope=0,bestError=1e30;
    bool foundFit=false;
    for (int i=0;i<=32;++i) {
        const double slope=-.5+double(i)/32;
        if (!fitTarsals(slope)) continue;
        const double error=std::abs(toeError());
        if (error<bestError) {bestError=error;bestSlope=slope;foundFit=true;}
    }
    if (!foundFit || !fitTarsals(bestSlope)) {
        // Keep the fitted five-sector roof for unusual profiles that cannot
        // satisfy both clearance constraints within the bounded fold angles.
        for (auto& band:crossRoofs) band=roofs;
    }
    for (auto& band:crossRoofs) for (auto& roof:band) {
        roof.point={0,roof.level,0};roof.normal=normalized(Vec3{-roof.slopeU,1,-roof.slopeV});
    }
    // Behind the ankle the roof must follow the back of the calcaneus, not
    // extrapolate the instep slope up into a tall wedge. Add one continuous
    // transverse fold just behind the talus; the five sections retain their
    // shared edges and the forefoot fit is unchanged.
    double heelFold=1e30;
    const typename Comparison::Part* calcaneus=nullptr;
    for (const auto& part:comparison.parts()) {
        if (part.name=="Cal"+suffix) calcaneus=&part;
        if (part.name=="Tal"+suffix)
            for(Vec3 p:part.points) heelFold=std::min(heelFold,(comparison.mapPart(p,part)+origin).z);
    }
    if (!calcaneus || heelFold>=tarsalZ-1e-6) {preserve();return false;}
    std::array<Plane,5> heelRoofs=crossRoofs[0];
    double heelSlope=1.6;
    std::vector<Vec3> calcPoints;
    calcPoints.reserve(calcaneus->points.size());
    for(Vec3 p:calcaneus->points) calcPoints.push_back(comparison.mapPart(p,*calcaneus)+origin);
    for(auto f:calcaneus->faces) {
        if(f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=calcPoints.size() ||
           std::size_t(f[1])>=calcPoints.size() || std::size_t(f[2])>=calcPoints.size()) continue;
        for(int section=0;section<5;++section) {
            std::vector<Vec3> poly{calcPoints[std::size_t(f[0])],calcPoints[std::size_t(f[1])],calcPoints[std::size_t(f[2])]};
            poly=clipBy(poly,[&](Vec3 p){return p.z-heelFold;});
            if(section>0 && !poly.empty()) poly=clipBy(poly,[&](Vec3 p){return boundaryX(section,p.z)-p.x;});
            if(section<4 && !poly.empty()) poly=clipBy(poly,[&](Vec3 p){return p.x-boundaryX(section+1,p.z);});
            const auto& roof=crossRoofs[0][std::size_t(section)];
            const double clearance=.0001*std::sqrt(1+roof.slopeU*roof.slopeU+
                                                   (std::abs(roof.slopeV)+1.6)*(std::abs(roof.slopeV)+1.6));
            for(Vec3 p:poly) if(heelFold-p.z>1e-8) {
                const double available=roof.slopeU*p.x+roof.slopeV*p.z+roof.level-p.y-clearance;
                heelSlope=std::min(heelSlope,std::max(0.0,available/(heelFold-p.z)));
            }
        }
    }
    for(auto& roof:heelRoofs) {
        roof.slopeV+=heelSlope;roof.level-=heelSlope*heelFold;
        roof.point={0,roof.level,0};roof.normal=normalized(Vec3{-roof.slopeU,1,-roof.slopeV});
    }
    const auto sectionRoof=[&](int band,int sector)->const Plane& {
        return band==0 ? heelRoofs[std::size_t(sector)] : crossRoofs[std::size_t(band-1)][std::size_t(sector)];
    };
    const std::array<double,3> folds{heelFold,tarsalZ,metaZ};
    // The outer segments pass through the 50% ring's outermost vertex as
    // well as its center, continuing to the side sheets. Account for the
    // different roof/side and sole/side intersections of each slanted side.
    const auto sideEnd=[&](const ToeEnd& end,double outward) {
        const Plane& side=planes[planes[1].normal.x*outward>0 ? 1 : 2];
        const Vec3 edge=outward<0 ? end.leftMid : end.rightMid;
        const double dx=edge.x-end.point.x;
        const double slope=std::abs(dx)<1e-8 ? 0 : (edge.z-end.point.z)/dx;
        const double intercept=end.point.z-slope*end.point.x;
        double extent=outward<0 ? end.left : end.right;
        std::array<Plane,16> faces{};
        for (int band=0;band<3;++band) for (int i=0;i<5;++i)
            faces[std::size_t(band*5+i)]=crossRoofs[std::size_t(band)][std::size_t(i)];
        faces[15]=planes[5];
        for (const Plane& face:faces) {
            const double fx=face.normal.x+face.normal.z*slope;
            const double sx=side.normal.x+side.normal.z*slope;
            const double a=dot(face.normal,face.point)-face.normal.z*intercept;
            const double b=dot(side.normal,side.point)-side.normal.z*intercept;
            const double det=fx*side.normal.y-sx*face.normal.y;
            if (std::abs(det)<1e-8) continue;
            const double x=(a*side.normal.y-b*face.normal.y)/det;
            extent=outward<0 ? std::min(extent,x) : std::max(extent,x);
        }
        extent=outward<0 ? std::min(extent,end.point.x-1e-8) : std::max(extent,end.point.x+1e-8);
        return Vec3{extent,0,intercept+slope*extent};
    };
    std::vector<Vec3> border;
    border.push_back(sideEnd(ends.front(),-1));
    for (const ToeEnd& end:ends) border.push_back(end.point);
    border.push_back(sideEnd(ends.back(),1));
    for (std::size_t i=1;i<border.size();++i)
        if (border[i].x-border[i-1].x<1e-8) { preserve(); return false; }
    std::vector<Vec3> bounds;
    for (const auto* sheet:sheets) bounds.insert(bounds.end(),sheet->points.begin(),sheet->points.end());
    const auto cuts=obstacles(comparison,suffix,origin);
    // Place the back edge of the transition in front of every fixed bone.
    // The original fitted roof stays unchanged behind this edge.
    double depth=1e30;
    for(std::size_t i=0;i+1<border.size();++i) {
        Vec3 a=border[i],b=border[i+1];
        for(const auto& bone:cuts) for(auto f:bone.faces) {
            if(f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=bone.points.size() ||
                std::size_t(f[1])>=bone.points.size() || std::size_t(f[2])>=bone.points.size()) continue;
            auto tri=std::vector<Vec3>{bone.points[std::size_t(f[0])],bone.points[std::size_t(f[1])],bone.points[std::size_t(f[2])]};
            tri=clipBy(tri,[&](Vec3 p){return a.x-p.x;});
            if(!tri.empty()) tri=clipBy(tri,[&](Vec3 p){return p.x-b.x;});
            for(Vec3 p:tri) depth=std::min(depth,a.z+(p.x-a.x)*(b.z-a.z)/(b.x-a.x)-p.z);
        }
    }
    depth=std::clamp(depth-.0003,0.0,(border.back().x-border.front().x)*.14);
    if(depth<1e-6) {preserve();return false;}
    for(Vec3& p:border) p.z-=depth;
    std::array<MetatarsalPlane,6> rebuilt;
    for (int index=0;index<6;++index) for (int band=0;band<4;++band) for (int sectionIndex=0;sectionIndex<5;++sectionIndex) {
        const Plane& current=index==0 ? sectionRoof(band,sectionIndex) : planes[std::size_t(index)];
        double loU=1e30,hiU=-1e30,loV=1e30,hiV=-1e30;
        for (Vec3 p:bounds) {
            p=current.local(p);
            loU=std::min(loU,p.x); hiU=std::max(hiU,p.x);
            loV=std::min(loV,p.z); hiV=std::max(hiV,p.z);
        }
        const double padding=2*std::max(hiU-loU,hiV-loV)+.01;
        std::vector<Vec3> polygon{{loU-padding,0,loV-padding},{hiU+padding,0,loV-padding},
                                  {hiU+padding,0,hiV+padding},{loU-padding,0,hiV+padding}};
        if (band>0) polygon=clipBy(polygon,[&](Vec3 p){return folds[std::size_t(band-1)]-current.world(p).z;});
        if (band<3 && !polygon.empty()) polygon=clipBy(polygon,[&](Vec3 p){return current.world(p).z-folds[std::size_t(band)];});
        if (sectionIndex>0 && !polygon.empty()) polygon=clipBy(polygon,[&](Vec3 p){const Vec3 q=current.world(p);return boundaryX(sectionIndex,q.z)-q.x;});
        if (sectionIndex<4 && polygon.size()>=3) polygon=clipBy(polygon,[&](Vec3 p){const Vec3 q=current.world(p);return q.x-boundaryX(sectionIndex+1,q.z);});
        for (int other=0;other<6 && polygon.size()>=3;++other) if (other!=index) {
            const Plane& limit=other==0 ? sectionRoof(band,sectionIndex) : planes[std::size_t(other)];
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
    for (const auto& mesh:rebuilt) if (mesh.faces.empty()) { preserve(); return false; }
    combine(rebuilt);
    // Evaluate the clipped sheet rims on the piecewise seam. All changes
    // forward of this seam are deformable between the foot and toe owners.
    const auto seam=[&](double x) {
        std::size_t i=0;while(i+2<border.size() && x>border[i+1].x) ++i;
        const Vec3 a=border[i],b=border[i+1];
        return a.z+(x-a.x)*(b.z-a.z)/(b.x-a.x);
    };
    const auto roofPoint=[&](double x) {
        const double z=seam(x);int section=0;
        while(section<4 && x>boundaryX(section+1,z)) ++section;
        const int band=z<heelFold?0:(z<tarsalZ?1:(z<metaZ?2:3));
        const auto& r=sectionRoof(band,section);
        return Vec3{x,r.slopeU*x+r.slopeV*z+r.level,z};
    };
    const auto solePoint=[&](double x) {const double z=seam(x);return planes[5].world({x,0,z});};
    const Plane& left=planes[planes[1].normal.x<0?1:2];
    const Plane& right=planes[planes[1].normal.x>0?1:2];
    const double middle=(toeRoots.front().center.x+toeRoots.back().center.x)*.5;
    const double width=std::max(border.back().x-border.front().x,.01);
    const auto edge=[&](const Plane& side,bool isLeft,const auto& point) {
        double lo=isLeft?border.front().x-width:middle;
        double hi=isLeft?middle:border.back().x+width;
        for(int k=0;k<48;++k) {
            const double m=(lo+hi)*.5;
            const bool outside=dot(side.normal,point(m)-side.point)>0;
            if(isLeft ? outside : !outside) lo=m; else hi=m;
        }
        return (lo+hi)*.5;
    };
    const double tl=edge(left,true,roofPoint),tr=edge(right,false,roofPoint);
    const double bl=edge(left,true,solePoint),br=edge(right,false,solePoint);
    std::vector<double> rimStations;
    for(Vec3 p:border) rimStations.push_back(p.x);
    for(std::size_t i=0;i+1<border.size();++i) {
        Vec3 a=border[i],b=border[i+1];
        for(int k=1;k<5;++k) {
            const double da=a.x-boundaryX(k,a.z),db=b.x-boundaryX(k,b.z);
            if((da<0)!=(db<0)) rimStations.push_back(a.x+(b.x-a.x)*da/(da-db));
        }
        for(double fold:folds) if((a.z<fold)!=(b.z<fold))
            rimStations.push_back(a.x+(b.x-a.x)*(fold-a.z)/(b.z-a.z));
    }
    top.owners.assign(top.points.size(),top.owner);
    toe_transition::band(top,toeRoots,rimStations,tl,tr,top.owner,true,roofPoint);
    toe_transition::band(top,toeRoots,rimStations,bl,br,top.owner,false,solePoint);
    using toe_transition::Point;
    const Point leftToe=toe_transition::front(toeRoots,toeRoots.front().left,top.owner)[0];
    const Point rightToe=toe_transition::front(toeRoots,toeRoots.back().right,top.owner)[0];
    toe_transition::add(top,Point{roofPoint(tl),top.owner},Point{solePoint(bl),top.owner},leftToe);
    toe_transition::add(top,Point{roofPoint(tr),top.owner},rightToe,Point{solePoint(br),top.owner});
    // A small toe-owned liner remains hidden inside the transition at rest.
    // It covers the proximal phalanx when the toe rotates away from the rim.
    for(const auto& root:toeRoots) for(int k=0;k<8;++k) for(int j=0;j<4;++j) {
        const int n=(k+1)%8;const double t0=j*.25,t1=(j+1)*.25;
        const Vec3 a=root.rear[k]+(root.ring[k]-root.rear[k])*t0;
        const Vec3 b=root.rear[n]+(root.ring[n]-root.rear[n])*t0;
        const Vec3 c=root.rear[n]+(root.ring[n]-root.rear[n])*t1;
        const Vec3 d=root.rear[k]+(root.ring[k]-root.rear[k])*t1;
        for(auto tri:{std::array<Vec3,3>{a,b,c},std::array<Vec3,3>{a,c,d}}) {
            auto poly=clipBy(std::vector<Vec3>{tri[0],tri[1],tri[2]},[&](Vec3 p){return seam(p.x)+.00005-p.z;});
            const auto limits=[&](Vec3 p,bool upper) {
                const Vec3 rear=upper?roofPoint(p.x):solePoint(p.x);
                const Vec3 fore=toe_transition::front(toeRoots,p.x,top.owner)[upper?0:1].p;
                const double t=std::clamp((p.z-rear.z)/std::max(fore.z-rear.z,1e-8),0.0,1.0);
                return rear.y+(fore.y-rear.y)*t;
            };
            poly=clipBy(poly,[&](Vec3 p){return p.y-limits(p,true)+.0001;});
            poly=clipBy(poly,[&](Vec3 p){return limits(p,false)+.0001-p.y;});
            for(std::size_t v=1;v+1<poly.size();++v)
                toe_transition::add(top,Point{poly[0],root.owner},Point{poly[v],root.owner},Point{poly[v+1],root.owner});
        }
    }
    return true;
}
} // namespace gtc::native
