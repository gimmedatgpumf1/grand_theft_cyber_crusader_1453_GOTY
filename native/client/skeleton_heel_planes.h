#pragma once

#include "client/skeleton_metatarsal_plane.h"
#include "client/skeleton_heel_join.h"

namespace gtc::native {

// Two nearly vertical planes meet at the back of the heel. Each is fitted
// outside the reference foot bones and follows the same rigid foot body as
// the existing side sheets. No toe, nail, or bone geometry is modified.
template<class Comparison>
std::array<MetatarsalPlane,2> makeHeelPlanes(const Comparison& comparison, const std::string& suffix,
                                              Vec3 origin, MetatarsalPlane& medial,
                                              MetatarsalPlane& lateral) {
    using namespace metatarsal_plane_detail;
    std::array<MetatarsalPlane,2> result;
    if (medial.faces.empty() || lateral.faces.empty() || medial.owner<0) return result;
    std::vector<Vec3> heel,foot;
    double toeZ=0; int toeCount=0;
    const auto& parts=comparison.parts();
    for (const auto& part:parts) {
        if (!part.name.ends_with(suffix)) continue;
        const bool calc=part.name=="Cal"+suffix;
        const bool metatarsal=part.name.rfind("FootMeta",0)==0;
        const bool fixed=calc || metatarsal || part.name.rfind("FootSesa",0)==0 ||
            part.name.rfind("Tal_",0)==0 || part.name.rfind("Nav_",0)==0 ||
            part.name.rfind("Cub_",0)==0 || part.name.rfind("MedCun_",0)==0 ||
            part.name.rfind("InterCun_",0)==0 || part.name.rfind("LatCun_",0)==0;
        if (!fixed) continue;
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin;
            foot.push_back(p);
            if (calc) heel.push_back(p);
            if (metatarsal) { toeZ+=p.z; ++toeCount; }
        }
    }
    if (heel.size()<3 || !toeCount) return result;
    Vec3 low=heel.front(),high=low,center{};
    for (Vec3 p:heel) {
        low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
        center+=p;
    }
    center=center/double(heel.size());
    if (high.x-low.x<1e-8 || high.y-low.y<1e-8) return result;
    const double backward=center.z<toeZ/double(toeCount) ? -1.0 : 1.0;
    const double hinge=(low.x+high.x)*.5;
    constexpr double wrap=.50, lean=-.15;
    // The two half-planes form a shallow V when seen from above. A common
    // support level makes their central edge coincide exactly.
    double support=-1e30;
    for (Vec3 p:foot)
        support=std::max(support,backward*p.z+wrap*std::abs(p.x-hinge)-lean*(p.y-center.y));
    const double clearance=.0001*std::sqrt(1+wrap*wrap+lean*lean);
    const double level=support+clearance;
    const auto sidePlane=[](const MetatarsalPlane& sheet,Vec3& point,Vec3& normal) {
        double largest=0;
        for (auto f:sheet.faces) {
            Vec3 a=sheet.points[f[0]],b=sheet.points[f[1]],c=sheet.points[f[2]];
            Vec3 n=cross(b-a,c-a);
            if (length(n)>largest) { largest=length(n); point=a; normal=n; }
        }
        if (largest<1e-20) return false;
        normal=normal/largest;
        return true;
    };
    Vec3 medialPoint{},medialNormal{},lateralPoint{},lateralNormal{};
    if (!sidePlane(medial,medialPoint,medialNormal) || !sidePlane(lateral,lateralPoint,lateralNormal)) return result;
    const auto trim=[](const std::vector<Vec3>& polygon,const auto& distance) {
        std::vector<Vec3> out;
        for (std::size_t i=0;i<polygon.size();++i) {
            Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
            double dp=distance(p),dq=distance(q);
            if (dp<=0) out.push_back(p);
            if ((dp<=0)!=(dq<=0)) out.push_back(p+(q-p)*(dp/(dp-dq)));
        }
        return out;
    };
    const double padding=(high.x-low.x)*.8;
    for (int half=0;half<2;++half) {
        auto& sheet=result[std::size_t(half)];
        sheet.owner=medial.owner;
        const double slope=half==0 ? wrap : -wrap;
        const auto world=[&](Vec3 p) {
            return Vec3{p.x,p.z,backward*(level+slope*(p.x-hinge)+lean*(p.z-center.y))};
        };
        const double x0=half==0 ? low.x-padding : hinge;
        const double x1=half==0 ? hinge : high.x+padding;
        // Local coordinates are (world x, outward depth, world y), so the
        // existing polygon helpers operate on the projected x/y footprint.
        std::vector<Vec3> polygon{{x0,0,low.y},{x1,0,low.y},{x1,0,high.y},{x0,0,high.y}};
        polygon=trim(polygon,[&](Vec3 p){ return dot(medialNormal,world(p)-medialPoint); });
        polygon=trim(polygon,[&](Vec3 p){ return dot(lateralNormal,world(p)-lateralPoint); });
        if (polygon.size()<3) continue;
        std::vector<std::vector<Vec3>> pieces{std::move(polygon)};
        // Ankle bones are separately articulated. Trim their reference-pose
        // cross-sections without shifting the foot-attached heel panels.
        const double localLevel=level-slope*hinge-lean*center.y;
        for (const auto& part:parts) {
            if (part.name!="Tibia"+suffix && part.name!="Fibula"+suffix) continue;
            std::vector<Vec3> mapped,overlap;
            mapped.reserve(part.points.size());
            for (Vec3 p:part.points) {
                p=comparison.mapPart(p,part)+origin;
                mapped.push_back({p.x,backward*p.z,p.y});
            }
            for (auto f:part.faces) {
                if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                    std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
                auto slab=heightClip({mapped[f[0]],mapped[f[1]],mapped[f[2]]},slope,lean,localLevel-clearance,true);
                slab=heightClip(slab,slope,lean,localLevel+clearance,false);
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
        for (const auto& piece:pieces) for (std::size_t i=1;i+1<piece.size();++i) {
            if (std::abs(turn(piece[0],piece[i],piece[i+1]))<1e-12*(high.x-low.x)*(high.y-low.y)) continue;
            const int base=static_cast<int>(sheet.points.size());
            for (Vec3 p:{piece[0],piece[i],piece[i+1]}) sheet.points.push_back(world(p));
            sheet.faces.push_back(backward>0 ? std::array<int,3>{base,base+1,base+2}
                                             : std::array<int,3>{base,base+2,base+1});
        }
    }
    joinHeelToSide(comparison,suffix,origin,backward,result,medial);
    joinHeelToSide(comparison,suffix,origin,backward,result,lateral);
    return result;
}
} // namespace gtc::native
