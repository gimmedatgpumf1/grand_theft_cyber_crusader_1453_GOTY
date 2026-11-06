#pragma once

// Direct cut from the CC0 MakeHuman body. Width is scaled independently to
// fit the bones; source vertices and triangles remain at original resolution.
// No fitted planes, vertex corrections or mesh simplification are applied.
#include "../../tools/foot/original-foot-male.h"
#include "../../tools/foot/original-foot-female.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native {
struct RawMakeHumanFoot {
    std::vector<Vec3> points;
    std::vector<std::array<int,3>> faces;
    std::vector<int> owners, secondaryOwners;
    std::vector<double> ownerWeights;
};

template<class Comparison,std::size_t N,std::size_t F>
RawMakeHumanFoot rawMakeHumanFoot(const Comparison& c,const std::string& side,Vec3 origin,
    const std::array<makehuman_foot_data::Vertex,N>& vertices,
    const std::array<std::array<int,3>,F>& faces,double widthPadding,double lateralShift) {
    RawMakeHumanFoot result;
    int fallback=-1;std::array<int,64> ids{};ids.fill(-1);
    struct Candidate {int owner;std::vector<Vec3> points;};
    std::vector<Candidate> candidates;
    Vec3 low{1e30,1e30,1e30},high{-1e30,-1e30,-1e30};
    for(std::size_t i=0;i<c.parts().size();++i) {
        const auto& part=c.parts()[i];if(!part.name.ends_with(side))continue;
        if(part.name=="FootMeta3"+side)fallback=static_cast<int>(i);
        for(int toe=1;toe<=5;++toe)for(int segment=1;segment<=3;++segment) {
            const std::string prefix=segment==1?"FootProxP":(segment==2 && toe>1?"FootMidP":"FootDistP");
            if(part.name==prefix+std::to_string(toe)+side)ids[toe*10+segment]=static_cast<int>(i);
        }
        const bool fixed=part.name.starts_with("FootMeta") || part.name.starts_with("Cal_") ||
            part.name.starts_with("Tal_") || part.name.starts_with("Nav_") || part.name.starts_with("Cub_") ||
            part.name.find("Cun_")!=std::string::npos;
        const bool ankle=part.name.starts_with("Tibia_") || part.name.starts_with("Fibula_");
        const bool toe=part.name.starts_with("FootProxP") || part.name.starts_with("FootMidP") ||
            part.name.starts_with("FootDistP") || part.name.starts_with("FootSesa");
        if(!fixed && !ankle && !toe)continue;
        Candidate candidate;candidate.owner=static_cast<int>(i);
        for(Vec3 p:part.points) {
            p=c.mapPart(p,part)+origin;
            if(fixed || toe) {
                low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
                high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
            }
            if(fixed || ankle)candidate.points.push_back(p);
        }
        if(!candidate.points.empty())candidates.push_back(std::move(candidate));
    }
    if(fallback<0 || low.z>=high.z)return result;
    const bool mirror=side=="_R";
    Vec3 srcLow{1e30,1e30,1e30},srcHigh{-1e30,-1e30,-1e30};
    for(const auto& v:vertices) {
        const Vec3 p{mirror?-v.x:v.x,v.y,v.z};
        srcLow.x=std::min(srcLow.x,p.x);srcLow.y=std::min(srcLow.y,p.y);srcLow.z=std::min(srcLow.z,p.z);
        srcHigh.x=std::max(srcHigh.x,p.x);srcHigh.y=std::max(srcHigh.y,p.y);srcHigh.z=std::max(srcHigh.z,p.z);
    }
    const double length=high.z-low.z;
    // Keep the existing length and height. Fit the width to the blue bones'
    // lateral extent with visual padding; this is not a clearance solver.
    const double scale=1.22*std::max(length/(srcHigh.z-srcLow.z),(high.x-low.x)/(srcHigh.x-srcLow.x));
    double widthScale=std::min(scale,widthPadding*(high.x-low.x)/(srcHigh.x-srcLow.x));
    Vec3 shift{(low.x+high.x)*.5-(srcLow.x+srcHigh.x)*.5*widthScale+(mirror?-1:1)*length*lateralShift,
                     low.y-length*.02-srcLow.y*scale,
                     (low.z+high.z)*.5-(srcLow.z+srcHigh.z)*.5*scale};
    // Match the original toe-surface centres to the actual blue distal bones,
    // not only the outer box of the entire foot. Use a z-local weighted mean
    // for each source toe, then a single width scale and translation for the
    // whole mesh. This leaves the source topology, length and height unchanged.
    std::array<double,5> sourceToeX{},boneToeX{};int toes=0;
    for(int toe=1;toe<=5;++toe) {
        const int id=ids[toe*10+(toe==1?2:3)];if(id<0)continue;
        const auto& bone=c.parts()[std::size_t(id)];if(bone.points.empty())continue;
        Vec3 target{};for(Vec3 p:bone.points)target+=c.mapPart(p,bone)+origin;
        target=target/double(bone.points.size());
        double weightedX=0,total=0;
        for(const auto& v:vertices) {
            double weight=0;
            for(int k=0;k<4;++k)if(v.bones[k]/10==toe &&
                (v.bones[k]%10==3 || (toe==1 && v.bones[k]%10==2)))weight+=v.weights[k];
            const double dz=(v.z*scale+shift.z-target.z)/(length*.04);
            weight*=std::exp(-dz*dz);
            weightedX+=((mirror?-v.x:v.x)*widthScale+shift.x)*weight;total+=weight;
        }
        if(total<1e-9)continue;
        sourceToeX[toes]=weightedX/total;boneToeX[toes]=target.x;++toes;
    }
    if(toes>=3) {
        double sourceMean=0,boneMean=0;
        for(int i=0;i<toes;++i){sourceMean+=sourceToeX[i];boneMean+=boneToeX[i];}
        sourceMean/=toes;boneMean/=toes;
        double covariance=0,variance=0;
        for(int i=0;i<toes;++i){const double d=sourceToeX[i]-sourceMean;
            covariance+=d*(boneToeX[i]-boneMean);variance+=d*d;}
        if(variance>1e-12) {
            const double factor=std::clamp(covariance/variance,.7,1.0);
            widthScale*=factor;
            shift.x=shift.x*factor+boneMean-factor*sourceMean;
        }
    }
    result.points.reserve(N);result.owners.reserve(N);result.secondaryOwners.reserve(N);result.ownerWeights.reserve(N);
    for(const auto& v:vertices) {
        const Vec3 p=Vec3{(mirror?-v.x:v.x)*widthScale,v.y*scale,v.z*scale}+shift;
        result.points.push_back(p);
        std::array<double,64> weights{};
        for(int j=0;j<4;++j)weights[v.bones[j]]+=v.weights[j];
        std::vector<double> byOwner(c.parts().size(),0);
        for(int code=1;code<64;++code)byOwner[std::size_t(ids[code]>=0?ids[code]:fallback)]+=weights[code];
        std::array<int,2> closest{{fallback,fallback}};std::array<double,2> distance{{1e30,1e30}};
        for(const auto& candidate:candidates) {
            double best=1e30;
            for(Vec3 q:candidate.points)best=std::min(best,dot(p-q,p-q));
            if(best<distance[0]){distance[1]=distance[0];closest[1]=closest[0];distance[0]=best;closest[0]=candidate.owner;}
            else if(best<distance[1]){distance[1]=best;closest[1]=candidate.owner;}
        }
        const double pad=length*.025;
        const double a=1/(distance[0]+pad*pad),b=1/(distance[1]+pad*pad),blend=a*a/(a*a+b*b);
        byOwner[std::size_t(closest[0])]+=weights[0]*blend;
        byOwner[std::size_t(closest[1])]+=weights[0]*(1-blend);
        int first=fallback,second=fallback;
        for(std::size_t i=0;i<byOwner.size();++i)if(byOwner[i]>byOwner[std::size_t(first)])first=static_cast<int>(i);
        for(std::size_t i=0;i<byOwner.size();++i)if(int(i)!=first && (second==first || byOwner[i]>byOwner[std::size_t(second)]))second=static_cast<int>(i);
        const double sum=byOwner[std::size_t(first)]+byOwner[std::size_t(second)];
        result.owners.push_back(first);result.secondaryOwners.push_back(second);
        result.ownerWeights.push_back(sum>1e-9?byOwner[std::size_t(first)]/sum:1.0);
    }
    result.faces.assign(faces.begin(),faces.end());
    if(mirror)for(auto& f:result.faces)std::swap(f[1],f[2]);
    return result;
}

template<class Comparison>
RawMakeHumanFoot rawMakeHumanFoot(const Comparison& c,bool male,const std::string& side,Vec3 origin) {
    using namespace makehuman_foot_data;
    return male?rawMakeHumanFoot(c,side,origin,originalMaleVertices,originalMaleFaces,1.24,.02):
                rawMakeHumanFoot(c,side,origin,originalFemaleVertices,originalFemaleFaces,1.27,.04);
}
} // namespace gtc::native
