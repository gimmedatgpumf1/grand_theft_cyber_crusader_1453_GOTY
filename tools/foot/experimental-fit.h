#pragma once

// EXPERIMENTAL: not used by the runtime; bone and nail fit is incomplete.
// The CC0 MakeHuman foot is extracted and simplified offline. Its toe-joint
// landmarks are fitted to the current comparison skeleton, then its original
// rig influences are mapped onto the comparison's articulated toe bones.
#include "client/skeleton_makehuman_foot_data.h"
#include "client/skeleton_toe_skin.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native {
struct MakeHumanFoot {
    std::vector<Vec3> points;
    std::vector<std::array<int,3>> faces;
    std::vector<int> owners;
};
namespace makehuman_foot_detail {
template<class Comparison,std::size_t N,std::size_t F>
MakeHumanFoot fit(const Comparison& c,const std::string& suffix,Vec3 origin,
                  const std::array<makehuman_foot_data::Vertex,N>& vertices,
                  const std::array<std::array<int,3>,F>& faces,
                  const std::array<std::array<float,3>,20>& anchors) {
    MakeHumanFoot result;
    const bool mirror=suffix=="_R";
    const auto source=[&](float x,float y,float z){return Vec3{mirror?-x:x,y,z};};
    std::array<int,16> bones{}; bones.fill(-1);
    int foot=-1,talus=-1;
    Vec3 low{1e30,1e30,1e30},high{-1e30,-1e30,-1e30};
    for(std::size_t i=0;i<c.parts().size();++i) {
        const auto& part=c.parts()[i];if(!part.name.ends_with(suffix))continue;
        if(part.name=="FootMeta3"+suffix) foot=static_cast<int>(i);
        if(part.name=="Tal"+suffix) talus=static_cast<int>(i);
        for(int toe=1;toe<=5;++toe) {
            const std::string id=std::to_string(toe)+suffix;
            if(part.name=="FootProxP"+id) bones[toe*3-3]=static_cast<int>(i);
            if(part.name=="FootMidP"+id) bones[toe*3-2]=static_cast<int>(i);
            if(part.name=="FootDistP"+id) bones[toe*3-1]=static_cast<int>(i);
        }
        const bool included=part.name.rfind("Foot",0)==0 || part.name=="Cal"+suffix || part.name=="Tal"+suffix ||
            part.name=="Nav"+suffix || part.name=="Cub"+suffix || part.name.find("Cun"+suffix)!=std::string::npos;
        if(!included)continue;
        for(Vec3 p:part.points) {
            p=c.mapPart(p,part)+origin;
            low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
        }
    }
    if(foot<0 || talus<0 || bones[0]<0 || bones[14]<0 || low.x>high.x) return result;
    const double size=high.z-low.z;
    low.x-=size*.020;high.x+=size*.020;low.y-=size*.012;
    low.z-=size*.025;high.z+=size*.015;high.y+=size*.10;
    Vec3 srcLow{1e30,1e30,1e30},srcHigh{-1e30,-1e30,-1e30};
    for(const auto& v:vertices) {Vec3 p=source(v.x,v.y,v.z);
        srcLow.x=std::min(srcLow.x,p.x);srcLow.y=std::min(srcLow.y,p.y);srcLow.z=std::min(srcLow.z,p.z);
        srcHigh.x=std::max(srcHigh.x,p.x);srcHigh.y=std::max(srcHigh.y,p.y);srcHigh.z=std::max(srcHigh.z,p.z);}
    const auto affine=[&](Vec3 p){return Vec3{low.x+(p.x-srcLow.x)*(high.x-low.x)/(srcHigh.x-srcLow.x),
        low.y+(p.y-srcLow.y)*(high.y-low.y)/(srcHigh.y-srcLow.y),
        low.z+(p.z-srcLow.z)*(high.z-low.z)/(srcHigh.z-srcLow.z)};};
    const auto center=[&](int id,bool front) {
        const auto& part=c.parts()[std::size_t(id)]; Vec3 avg{};double z=front?-1e30:1e30;
        for(Vec3 p:part.points) {p=c.mapPart(p,part)+origin;avg+=p;z=front?std::max(z,p.z):std::min(z,p.z);}
        avg=avg/double(part.points.size());avg.z=z;return avg;
    };
    const auto ring=[&](int id) {
        auto prism=c.toePrism(std::size_t(id),origin);Vec3 p{};
        if(prism.size()!=16)return center(id,false);
        c.resizeGreatToe(std::size_t(id),origin,prism);
        for(int k=0;k<8;++k)p+=prism[std::size_t(k)]/8.0;return p;
    };
    std::array<Vec3,19> srcControl{},delta{};
    int j=0;
    for(int toe=1;toe<=5;++toe) {
        const int prox=bones[toe*3-3],mid=bones[toe*3-2],dist=bones[toe*3-1];
        if(prox<0 || dist<0 || (toe>1 && mid<0))return {};
        std::array<Vec3,4> targets{ring(prox),toe==1?center(dist,false):ring(mid),
            toe==1?center(dist,true):center(dist,false),center(dist,true)};
        const int count=toe==1?3:4;
        for(int k=0;k<count;++k,++j) {
            srcControl[j]=affine(source(anchors[j][0],anchors[j][1],anchors[j][2]));
            delta[j]=targets[k]-srcControl[j];delta[j].y+=size*.045;
        }
    }
    result.points.reserve(N);result.owners.reserve(N);
    for(const auto& v:vertices) {
        Vec3 p=affine(source(v.x,v.y,v.z));Vec3 adjustment{};double sum=0;
        for(int k=0;k<19;++k) {
            Vec3 d=p-srcControl[k];d.y*=.45;
            const double radius=size*.075, w=std::exp(-dot(d,d)/(radius*radius));
            adjustment+=delta[k]*w;sum+=w;
        }
        p+=adjustment/std::max(sum,.5);
        p.y=std::max(p.y,low.y);
        result.points.push_back(p);
        int owner=foot;
        const int toe=v.influence/10,segment=v.influence%10;
        if(toe>=1 && toe<=5 && segment>=1 && segment<=3) {
            const int mapped=(toe==1 && segment==2)?2:segment-1;
            if(bones[toe*3-3+mapped]>=0)owner=bones[toe*3-3+mapped];
        }
        result.owners.push_back(owner);
    }
    result.faces.assign(faces.begin(),faces.end());
    if(mirror) for(auto& face:result.faces)std::swap(face[1],face[2]);
    return result;
}
} // namespace makehuman_foot_detail
template<class Comparison>
MakeHumanFoot makeHumanFoot(const Comparison& c,bool male,const std::string& side,Vec3 origin) {
    using namespace makehuman_foot_data;
    return male ? makehuman_foot_detail::fit(c,side,origin,maleVertices,maleFaces,maleAnchors)
                : makehuman_foot_detail::fit(c,side,origin,femaleVertices,femaleFaces,femaleAnchors);
}
} // namespace gtc::native
