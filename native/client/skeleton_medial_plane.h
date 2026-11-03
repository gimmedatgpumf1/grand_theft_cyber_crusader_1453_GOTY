#pragma once

#include "client/skeleton_metatarsal_plane.h"
#include "client/skeleton_medial_bridge.h"

namespace gtc::native {

// A side sheet runs from a metatarsal to the heel, fitted to outward-facing
// bones. It moves with the dorsal sheet's foot body.
template<class Comparison>
MetatarsalPlane makeFootSidePlane(const Comparison& comparison, const std::string& suffix,
                                  Vec3 origin, MetatarsalPlane& top, bool littleToe=false) {
    using namespace metatarsal_plane_detail;
    MetatarsalPlane result;
    const auto& parts=comparison.parts();
    std::vector<Vec3> fitted, first, fifth, talus, heel;
    std::size_t sideToe=parts.size();
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& part=parts[i];
        if (!part.name.ends_with(suffix)) continue;
        if (part.name=="FootMeta3"+suffix) result.owner=static_cast<int>(i);
        if (part.name==(littleToe ? "FootDistP5" : "FootDistP1")+suffix) sideToe=i;
        const bool firstMeta=part.name=="FootMeta1"+suffix;
        const bool fifthMeta=part.name=="FootMeta5"+suffix;
        const bool isHeel=part.name=="Cal"+suffix;
        const bool fit=part.name=="Tal"+suffix || (littleToe
            ? (fifthMeta || part.name=="Cub"+suffix || part.name=="LatCun"+suffix)
            : (firstMeta || part.name=="MedCun"+suffix || part.name=="Nav"+suffix));
        if (!fit && !firstMeta && !fifthMeta && !isHeel) continue;
        for (Vec3 p:part.points) {
            p=comparison.mapPart(p,part)+origin;
            if (fit) fitted.push_back(p);
            if (firstMeta) first.push_back(p);
            if (fifthMeta) fifth.push_back(p);
            if (part.name=="Tal"+suffix) talus.push_back(p);
            if (isHeel) heel.push_back(p);
        }
    }
    if (result.owner<0 || fitted.size()<3 || first.empty() || fifth.empty() || talus.empty() || heel.empty() || top.faces.empty()) return {};
    double firstX=0,fifthX=0;
    for (Vec3 p:first) firstX+=p.x;
    for (Vec3 p:fifth) fifthX+=p.x;
    const double medialOutward=firstX/double(first.size())<fifthX/double(fifth.size()) ? -1.0 : 1.0;
    const double outward=littleToe ? -medialOutward : medialOutward;
    // Convert to a 2-D (z,y) footprint with the outward coordinate as height.
    const auto local=[&](Vec3 p) { return Vec3{p.z,outward*p.x,p.y}; };
    std::vector<Vec3> candidates;
    candidates.reserve(fitted.size());
    for (Vec3 p:fitted) candidates.push_back(local(p));
    double loU=candidates[0].x,hiU=loU,loV=candidates[0].z,hiV=loV;
    for (Vec3 p:candidates) {
        loU=std::min(loU,p.x); hiU=std::max(hiU,p.x);
        loV=std::min(loV,p.z); hiV=std::max(hiV,p.z);
    }
    if (hiU-loU<1e-8 || hiV-loV<1e-8) return {};
    constexpr int bins=12;
    std::array<Vec3,bins*bins> outer{};
    std::array<bool,bins*bins> used{};
    for (Vec3 p:candidates) {
        const int u=std::clamp(int((p.x-loU)/(hiU-loU)*bins),0,bins-1);
        const int v=std::clamp(int((p.z-loV)/(hiV-loV)*bins),0,bins-1);
        const int index=v*bins+u;
        if (!used[index] || p.y>outer[index].y) { outer[index]=p; used[index]=true; }
    }
    Vec3 mean{}; int count=0;
    for (std::size_t i=0;i<outer.size();++i) if (used[i]) { mean+=outer[i]; ++count; }
    if (count<3) return {};
    mean=mean/double(count);
    double uu=0,uv=0,vv=0,uw=0,vw=0;
    for (std::size_t i=0;i<outer.size();++i) if (used[i]) {
        Vec3 d=outer[i]-mean;
        uu+=d.x*d.x; uv+=d.x*d.z; vv+=d.z*d.z; uw+=d.x*d.y; vw+=d.z*d.y;
    }
    const double determinant=uu*vv-uv*uv;
    if (determinant<1e-20) return {};
    const double slopeU=std::clamp((uw*vv-vw*uv)/determinant,-.7,.7);
    // Keep the side sheet nearly vertical even at extreme body scales.
    const double slopeV=std::clamp((vw*uu-uw*uv)/determinant,-.25,.25);
    for (Vec3 p:heel) {
        p=local(p); candidates.push_back(p);
        loU=std::min(loU,p.x); hiU=std::max(hiU,p.x);
        loV=std::min(loV,p.z); hiV=std::max(hiV,p.z);
    }
    auto boundary=hull(candidates);
    if (boundary.size()<3) return {};
    // Extend the fitting footprint upwards before finding bone support, so
    // the later connection to the dorsal plane cannot cut through a bone.
    std::vector<Vec3> supportFootprint=candidates;
    supportFootprint.push_back({loU,0,hiV+(hiV-loV)});
    supportFootprint.push_back({hiU,0,hiV+(hiV-loV)});
    const auto supportBoundary=hull(std::move(supportFootprint));
    // Use the clipped surfaces of the rigid foot bones, not a rectangular
    // bounding box, to keep the sheet on the outside without lifting it for
    // unrelated bones outside the footprint.
    double support=-1e30;
    for (const auto& part:parts) {
        if (!part.name.ends_with(suffix)) continue;
        const bool fixed=part.name.rfind("FootMeta",0)==0 || part.name.rfind("FootSesa",0)==0 ||
            part.name.rfind("Tal_",0)==0 || part.name.rfind("Cal_",0)==0 ||
            part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 ||
            part.name.rfind("MedCun_",0)==0 || part.name.rfind("InterCun_",0)==0 ||
            part.name.rfind("LatCun_",0)==0;
        if (!fixed) continue;
        std::vector<Vec3> mapped; mapped.reserve(part.points.size());
        for (Vec3 p:part.points) mapped.push_back(local(comparison.mapPart(p,part)+origin));
        for (const auto& f:part.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            for (Vec3 p:intersect({mapped[f[0]],mapped[f[1]],mapped[f[2]]},supportBoundary))
                support=std::max(support,p.y-slopeU*p.x-slopeV*p.z);
        }
    }
    if (support<-1e20) return {};
    const double offset=.0001*std::sqrt(1+slopeU*slopeU+slopeV*slopeV);
    const double level=support+offset;
    // Use the largest dorsal triangle, not a tiny sliver at the start of the
    // mesh, to recover its stable plane equation.
    Vec3 a{},n{};
    double largest=0;
    for (const auto& face:top.faces) {
        const Vec3 p=top.points[face[0]],q=top.points[face[1]],r=top.points[face[2]];
        const Vec3 candidate=cross(q-p,r-p);
        if (std::abs(candidate.y)>largest) { largest=std::abs(candidate.y); a=p; n=candidate; }
    }
    if (largest<1e-20) return {};
    if (n.y<0) n=n*-1;
    const double denominator=n.y+n.x*outward*slopeV;
    if (std::abs(denominator)<1e-20) return {};
    const auto joinPoint=[&](double z) {
        const double y=(dot(n,a)-n.x*outward*level-(n.x*outward*slopeU+n.z)*z)/denominator;
        return Vec3{z,0,y};
    };
    // Extend the side up to the dorsal plane over its full length. Where
    // that line lies outside the dorsal outline, a coplanar bridge fills it.
    double minTop=1e30,maxTop=-1e30;
    for (Vec3 p:top.points) { minTop=std::min(minTop,p.z); maxTop=std::max(maxTop,p.z); }
    std::vector<Vec3> expanded=candidates;
    expanded.push_back(joinPoint(minTop));
    expanded.push_back(joinPoint(maxTop));
    boundary=hull(std::move(expanded));
    if (boundary.size()<3) return {};
    // End the sheet before it crosses the adjacent toe's existing skin.
    if (sideToe<parts.size()) {
        double fore=0,back=0;
        const auto& metatarsal=littleToe ? fifth : first;
        for (Vec3 p:metatarsal) fore+=p.z;
        for (Vec3 p:talus) back+=p.z;
        const double towardToe=fore/double(metatarsal.size())>back/double(talus.size()) ? 1.0 : -1.0;
        const auto skin=comparison.toeSurface(sideToe,origin);
        double toeStart=1e30;
        for (const auto& f:skin.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=skin.points.size() ||
                std::size_t(f[1])>=skin.points.size() || std::size_t(f[2])>=skin.points.size()) continue;
            auto polygon=intersect({local(skin.points[f[0]]),local(skin.points[f[1]]),local(skin.points[f[2]])},boundary);
            for (std::size_t i=0;i<polygon.size();++i) {
                const Vec3 p=polygon[i],q=polygon[(i+1)%polygon.size()];
                const double dp=p.y-slopeU*p.x-slopeV*p.z-level;
                const double dq=q.y-slopeU*q.x-slopeV*q.z-level;
                if ((dp>=0)!=(dq>=0))
                    toeStart=std::min(toeStart,towardToe*(p.x+(q.x-p.x)*(dp/(dp-dq))));
            }
        }
        if (toeStart<1e20) {
            const double z=towardToe*(toeStart-.0001);
            boundary=clip(boundary,{z,0,0},{z,0,towardToe},true);
        }
    }
    // Clip at the dorsal plane; the generated upper edge meets it exactly.
    const auto height=[&](Vec3 p) {
        const double x=outward*(slopeU*p.x+slopeV*p.z+level);
        return dot(n,Vec3{x,p.z,p.x}-a);
    };
    std::vector<Vec3> clipped;
    for (std::size_t i=0;i<boundary.size();++i) {
        const Vec3 p=boundary[i],q=boundary[(i+1)%boundary.size()];
        const double dp=height(p),dq=height(q);
        if (dp<=0) clipped.push_back(p);
        if ((dp<=0)!=(dq<=0)) clipped.push_back(p+(q-p)*(dp/(dp-dq)));
    }
    // The leg articulates separately from the foot. In the reference pose,
    // remove its cross-section instead of letting the extended sheet cut it.
    std::vector<std::vector<Vec3>> pieces;
    if (clipped.size()>=3) pieces.push_back(std::move(clipped));
    for (const auto& part:parts) {
        if (part.name!="Tibia"+suffix && part.name!="Fibula"+suffix) continue;
        std::vector<Vec3> mapped,overlap;
        mapped.reserve(part.points.size());
        for (Vec3 p:part.points) mapped.push_back(local(comparison.mapPart(p,part)+origin));
        for (const auto& f:part.faces) {
            if (f[0]<0 || f[1]<0 || f[2]<0 || std::size_t(f[0])>=mapped.size() ||
                std::size_t(f[1])>=mapped.size() || std::size_t(f[2])>=mapped.size()) continue;
            auto slab=heightClip({mapped[f[0]],mapped[f[1]],mapped[f[2]]},slopeU,slopeV,level-offset,true);
            slab=heightClip(slab,slopeU,slopeV,level+offset,false);
            overlap.insert(overlap.end(),slab.begin(),slab.end());
        }
        const auto cut=hull(std::move(overlap));
        if (cut.size()<3) continue;
        std::vector<std::vector<Vec3>> next;
        for (const auto& piece:pieces) {
            auto remaining=subtract(piece,cut);
            next.insert(next.end(),remaining.begin(),remaining.end());
        }
        pieces=std::move(next);
    }
    for (const auto& piece:pieces) for (std::size_t i=1;i+1<piece.size();++i) {
        if (std::abs(turn(piece[0],piece[i],piece[i+1]))<1e-12*(hiU-loU)*(hiV-loV)) continue;
        const int base=static_cast<int>(result.points.size());
        for (Vec3 p:{piece[0],piece[i],piece[i+1]})
            result.points.push_back({outward*(slopeU*p.x+slopeV*p.z+level),p.z,p.x});
        result.faces.push_back(outward<0 ? std::array<int,3>{base,base+1,base+2}
                                         : std::array<int,3>{base,base+2,base+1});
    }
    if (!result.faces.empty()) bridgeFootSidePlane(comparison,suffix,origin,result,top);
    return result;
}
} // namespace gtc::native
