#pragma once

// Direct cut from the CC0 MakeHuman body. The whole foot is scaled, rotated
// and positioned as one mesh; source triangles remain at original resolution.
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
    const std::array<std::array<int,3>,F>& faces,bool male,double widthPadding,double lateralShift) {
    RawMakeHumanFoot result;
    int fallback=-1;std::array<int,64> ids{};ids.fill(-1);
    struct Candidate {int owner;std::vector<Vec3> points;};
    std::vector<Candidate> candidates;
    Vec3 low{1e30,1e30,1e30},high{-1e30,-1e30,-1e30};
    for(std::size_t i=0;i<c.parts().size();++i) {
        const auto& part=c.parts()[i];if(!part.name.ends_with(side))continue;
        if(part.name=="FootMeta3"+side)fallback=static_cast<int>(i);
        if(part.name=="Cal"+side)ids[0]=static_cast<int>(i);
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
    // Fit width to the blue bones' lateral extent with visual padding. Keep
    // the original height and preserve the existing width fit while shortening
    // the heel-to-toe axis independently; this is not a clearance solver.
    const double scale=1.22*std::max(length/(srcHigh.z-srcLow.z),(high.x-low.x)/(srcHigh.x-srcLow.x));
    double widthScale=std::min(scale,widthPadding*(high.x-low.x)/(srcHigh.x-srcLow.x));
    Vec3 shift{(low.x+high.x)*.5-(srcLow.x+srcHigh.x)*.5*widthScale+(mirror?-1:1)*length*lateralShift,
                     low.y-length*.02-srcLow.y*scale,
                     (low.z+high.z)*.5-(srcLow.z+srcHigh.z)*.5*scale};
    // Match the original toe-surface centres to the actual blue distal bones,
    // not only the outer box of the entire foot. Use a z-local weighted mean
    // for each source toe, then a single width scale and translation for the
    // whole mesh. This is evaluated in the previous longitudinal placement
    // so shortening below does not silently change the width fit.
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
    // Shorten around the bone envelope. The extra 10% supplies
    // visual room beyond the bones at heel and toe, not a clearance guarantee.
    const double lengthScale=std::min(scale,1.10*length/(srcHigh.z-srcLow.z));
    shift.z=(low.z+high.z)*.5-(srcLow.z+srcHigh.z)*.5*lengthScale-length*.0125;
    const auto placed=[&](const makehuman_foot_data::Vertex& v) {
        return Vec3{(mirror?-v.x:v.x)*widthScale,v.y*scale,v.z*lengthScale}+shift;
    };
    // Fit the heel-to-toe direction to this skeleton, using skin cross-section
    // centers and the actual calcaneus and distal toe bones. The toe center is
    // the rotation pivot, preserving the previously fitted toe placement.
    const auto boneCenter=[&](int id,Vec3& center) {
        if(id<0)return false;
        const auto& part=c.parts()[std::size_t(id)];if(part.points.empty())return false;
        center={};for(Vec3 p:part.points)center+=c.mapPart(p,part)+origin;
        center=center/double(part.points.size());return true;
    };
    const auto skinCenter=[&](int toe,Vec3 bone,Vec3& center) {
        double sum=0;center={};const double band=std::max((high.z-low.z)*.05,1e-6);
        for(const auto& v:vertices) {
            double weight=0;
            for(int j=0;j<4;++j) {
                const int code=v.bones[std::size_t(j)];
                if((toe==0&&code==0)||(toe>0&&code/10==toe&&
                    (code%10==3||(toe==1&&code%10==2))))weight+=v.weights[std::size_t(j)];
            }
            const Vec3 p=placed(v);const double dz=(p.z-bone.z)/band;
            weight*=std::exp(-dz*dz);center+=p*weight;sum+=weight;
        }
        if(sum<1e-8)return false;
        center=center/sum;return true;
    };
    Vec3 heelBone{},heelSkin{},toeBone{},toeSkin{};int fitToes=0;
    const bool haveHeel=boneCenter(ids[0],heelBone)&&skinCenter(0,heelBone,heelSkin);
    for(int toe=1;toe<=5;++toe) {
        Vec3 bone{},skin{};
        if(boneCenter(ids[toe*10+3],bone)&&skinCenter(toe,bone,skin)) {
            toeBone+=bone;toeSkin+=skin;++fitToes;
        }
    }
    double yaw=0,pitch=0;Vec3 pivot{};
    if(haveHeel&&fitToes>=3) {
        toeBone=toeBone/double(fitToes);toeSkin=toeSkin/double(fitToes);pivot=toeSkin;
        const Vec3 source=toeSkin-heelSkin,target=toeBone-heelBone;
        constexpr double limit=.3490658503988659; // 20 degrees
        yaw=std::clamp(std::atan2(target.x,target.z)-std::atan2(source.x,source.z),-limit,limit);
        const double sourceZ=std::cos(yaw)*source.z-std::sin(yaw)*source.x;
        pitch=std::clamp(std::atan2(target.y,target.z)-std::atan2(source.y,sourceZ),-limit,limit);
    }
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const auto orient=[&](Vec3 p) {
        p-=pivot;const double x=cy*p.x+sy*p.z,z=cy*p.z-sy*p.x;
        return pivot+Vec3{x,cp*p.y+sp*z,cp*z-sp*p.y};
    };
    // Fine fit calibrated separately against sampled blue foot-bone surfaces
    // at the default male/female profiles, also considering toe-tip proportions.
    // Tibia and fibula are excluded. Corrections apply to the whole
    // mesh, not individual vertices; translations scale with the foot and lateral terms
    // mirror on the right. This calibration is not a guarantee for every pose.
    Vec3 fitLow{1e30,1e30,1e30},fitHigh{-1e30,-1e30,-1e30};
    for(const auto& v:vertices) {
        const Vec3 p=orient(placed(v));
        fitLow.x=std::min(fitLow.x,p.x);fitHigh.x=std::max(fitHigh.x,p.x);
        fitLow.y=std::min(fitLow.y,p.y);fitHigh.y=std::max(fitHigh.y,p.y);
        fitLow.z=std::min(fitLow.z,p.z);fitHigh.z=std::max(fitHigh.z,p.z);
    }
    const Vec3 fitPivot=(fitLow+fitHigh)*.5;
    const double lateral=mirror?-1.0:1.0;
    const Vec3 fitScale=male?Vec3{.99295932,1.02926735,.98291640}:
                             Vec3{.98442179,1.04176544,.99914149};
    const Vec3 fitRotation=male?Vec3{.01198934,-.00786202*lateral,-.01161334*lateral}:
                                Vec3{.09125368,.00821542*lateral,.01836777*lateral};
    const Vec3 fitShift=(male?Vec3{.003597*lateral,-.005054,-.002474}:
                                      Vec3{-.0073543*lateral,.0312917,.0269597})*length;
    const double fitAngle=std::sqrt(dot(fitRotation,fitRotation));
    const Vec3 axis=fitRotation/fitAngle;
    const auto fineFit=[&](Vec3 p) {
        p-=fitPivot;p={p.x*fitScale.x,p.y*fitScale.y,p.z*fitScale.z};
        return fitPivot+fitShift+p*std::cos(fitAngle)+cross(axis,p)*std::sin(fitAngle)
            +axis*(dot(axis,p)*(1-std::cos(fitAngle)));
    };
    // Successive whole-mesh transforms refine the placement against the blue
    // foot bones. The last female-only stage shortens the heel-to-toe axis
    // and readjusts the standing fit; every stage retains the source mesh.
    struct WholeFootFit {Vec3 scale,rotation,shift;};
    const std::array<WholeFootFit,4> corrections=male?std::array<WholeFootFit,4>{{
        {{.97859785,1.00142187,.99539021},{-.00172617,-.00690658*lateral,-.00023237*lateral},{.00321737*lateral,.00007928,-.00222667}},
        {{.99313868,1.02010852,1.00506043},{-.00000454,-.00152017*lateral,-.01587345*lateral},{.00302022*lateral,.00276445,-.00217476}},
        {{.99686880,1.01466473,1.00095150},{-.00270637,.00114379*lateral,-.00907142*lateral},{.00058503*lateral,.00022392,-.00087254}},
        {{1,1,1},{0,0,0},{0,0,0}}
    }}:std::array<WholeFootFit,4>{{
        {{.97067911,1.03067010,1.03789693},{.00710349,-.00225730*lateral,.00214380*lateral},{.00130118*lateral,.00864686,.00312933}},
        {{1.00955205,.998028,1.00922851},{.00139138,-.00023489*lateral,-.00726774*lateral},{.00114996*lateral,.00259941,-.00037831}},
        {{1.00816616,1.02939159,1.02685130},{.00834466,.00287617*lateral,.00275625*lateral},{-.00157865*lateral,.00216967,.00669425}},
        {{.99398625,1.00097114,.95},{.00909611,-.00121139*lateral,-.003001*lateral},{.00011805*lateral,.00671472,-.00289150}}
    }};
    std::vector<Vec3> fitted;fitted.reserve(N);
    for(const auto& v:vertices)fitted.push_back(fineFit(orient(placed(v))));
    for(std::size_t step=0;step<(male?3:4);++step) {
        const auto& fit=corrections[step];
        Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
        for(Vec3 p:fitted) {
            lo.x=std::min(lo.x,p.x);hi.x=std::max(hi.x,p.x);
            lo.y=std::min(lo.y,p.y);hi.y=std::max(hi.y,p.y);
            lo.z=std::min(lo.z,p.z);hi.z=std::max(hi.z,p.z);
        }
        const Vec3 pivot=(lo+hi)*.5,shift=fit.shift*length;
        const double angle=std::sqrt(dot(fit.rotation,fit.rotation));
        const Vec3 axis=fit.rotation/angle;const double cs=std::cos(angle),sn=std::sin(angle);
        for(Vec3& p:fitted) {
            p-=pivot;p={p.x*fit.scale.x,p.y*fit.scale.y,p.z*fit.scale.z};
            p=pivot+shift+p*cs+cross(axis,p)*sn+axis*(dot(axis,p)*(1-cs));
        }
    }
    result.points.reserve(N);result.owners.reserve(N);result.secondaryOwners.reserve(N);result.ownerWeights.reserve(N);
    for(std::size_t vertexIndex=0;vertexIndex<N;++vertexIndex) {
        const auto& v=vertices[vertexIndex];const Vec3 p=fitted[vertexIndex];
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
    return male?rawMakeHumanFoot(c,side,origin,originalMaleVertices,originalMaleFaces,true,1.24,.02):
                rawMakeHumanFoot(c,side,origin,originalFemaleVertices,originalFemaleFaces,false,1.27,.04);
}
} // namespace gtc::native
