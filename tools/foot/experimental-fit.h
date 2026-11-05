#pragma once

// EXPERIMENTAL: not used by the runtime; bone and nail fit is incomplete.
// The CC0 MakeHuman full-resolution foot is fitted before any simplification. Its toe-joint
// landmarks are fitted to the current comparison skeleton, then its original
// rig influences are mapped onto the comparison's articulated toe bones.
#include "source-foot-male.h"
#include "source-foot-female.h"
#include "correction-male.h"
#include "correction-female.h"
#include "reduced-foot-male.h"
#include "reduced-foot-female.h"
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
    std::vector<int> secondaryOwners;
    std::vector<double> ownerWeights;
    double length=0;
};
// Fit the eight-sided nail to the actual distal surface, rather than the
// old procedural toe tip. It remains owned by that toe's distal bone.
inline ToeSkinMesh makeHumanNail(const MakeHumanFoot& foot,int toeOwner) {
    ToeSkinMesh nail;Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
    for(std::size_t i=0;i<foot.points.size();++i) if(foot.owners[i]==toeOwner) {
        const Vec3 p=foot.points[i];lo.x=std::min(lo.x,p.x);hi.x=std::max(hi.x,p.x);
        lo.z=std::min(lo.z,p.z);hi.z=std::max(hi.z,p.z);
    }
    if(lo.x>=hi.x || lo.z>=hi.z)return nail;
    const auto surface=[&](double x,double z) {
        double best=-1e30;
        for(auto f:foot.faces) {
            if(foot.owners[std::size_t(f[0])]!=toeOwner && foot.owners[std::size_t(f[1])]!=toeOwner &&
               foot.owners[std::size_t(f[2])]!=toeOwner)continue;
            Vec3 a=foot.points[std::size_t(f[0])],b=foot.points[std::size_t(f[1])],c=foot.points[std::size_t(f[2])];
            const double det=(b.x-a.x)*(c.z-a.z)-(c.x-a.x)*(b.z-a.z);
            if(std::abs(det)<1e-14)continue;
            const double u=((x-a.x)*(c.z-a.z)-(c.x-a.x)*(z-a.z))/det;
            const double v=((b.x-a.x)*(z-a.z)-(x-a.x)*(b.z-a.z))/det;
            if(u>=-1e-6 && v>=-1e-6 && u+v<=1.000001)
                best=std::max(best,a.y+(b.y-a.y)*u+(c.y-a.y)*v);
        }
        return best;
    };
    const double cx=(lo.x+hi.x)*.5,cz=lo.z+(hi.z-lo.z)*.70;
    // Flat front, chamfered front corners, nearly parallel sides, concave root.
    constexpr std::array<std::array<double,2>,8> outline{{
        {{-.42,-.38}},{{0,-.31}},{{.42,-.38}},{{.48,.27}},
        {{.32,.48}},{{-.32,.48}},{{-.48,.27}},{{-.48,-.17}}}};
    for(int attempt=0;attempt<6;++attempt) {
        const double shrink=std::pow(.85,attempt),width=(hi.x-lo.x)*.53*shrink,depth=(hi.z-lo.z)*.37*shrink;
        nail.nailPoints.clear();nail.nailFaces.clear();bool good=true;
        // Fit a gently tilted plane to the dorsal patch. Lift by the largest
        // residual so the flat nail does not disappear into the skin.
        double matrix[3][4]{};std::vector<std::array<double,3>> samples;
        for(int ring=0;ring<=4 && good;++ring) for(int i=0;i<32;++i) {
            const int edge=i/4;const double t=(i%4)/4.0,rad=ring/4.0;
            const auto a=outline[edge],b=outline[(edge+1)%8];
            const double u=(a[0]+(b[0]-a[0])*t)*rad,v=(a[1]+(b[1]-a[1])*t)*rad;
            const double y=surface(cx+u*width,cz+v*depth);if(y< -1e20){good=false;break;}
            const double q[3]={1,u,v};samples.push_back({u,v,y});
            for(int row=0;row<3;++row){for(int col=0;col<3;++col)matrix[row][col]+=q[row]*q[col];matrix[row][3]+=q[row]*y;}
        }
        if(!good)continue;
        for(int pivot=0;pivot<3;++pivot) {
            if(std::abs(matrix[pivot][pivot])<1e-12){good=false;break;}
            const double scale=matrix[pivot][pivot];for(int col=pivot;col<4;++col)matrix[pivot][col]/=scale;
            for(int row=0;row<3;++row)if(row!=pivot){const double amount=matrix[row][pivot];
                for(int col=pivot;col<4;++col)matrix[row][col]-=amount*matrix[pivot][col];}
        }
        if(!good)continue;
        double lift=.00015;
        for(auto q:samples)lift=std::max(lift,q[2]-(matrix[0][3]+matrix[1][3]*q[0]+matrix[2][3]*q[1])+.00015);
        for(auto q:outline)nail.nailPoints.push_back({cx+q[0]*width,
            matrix[0][3]+matrix[1][3]*q[0]+matrix[2][3]*q[1]+lift,cz+q[1]*depth});
        for(int i=1;i<7;++i)nail.nailFaces.push_back({0,i,i+1});
        return nail;
    }
    nail.nailPoints.clear();return nail;
}
namespace makehuman_foot_detail {
template<class Comparison,std::size_t N,std::size_t F>
MakeHumanFoot fit(const Comparison& c,const std::string& suffix,Vec3 origin,
                  const std::array<makehuman_foot_data::Vertex,N>& vertices,
                  const std::array<std::array<int,3>,F>& faces,
                  const std::array<std::array<float,3>,20>& anchors,
                  const std::array<std::array<float,3>,N>& correction,bool applyCorrection) {
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
    const double size=high.z-low.z; result.length=size;
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
            if(k==count-1) targets[k].z+=size*.025;
            delta[j]=targets[k]-srcControl[j];delta[j].y=0;
        }
    }
    // Source-surface cross sections and skeleton envelopes, sampled before
    // simplification, keep the foot shaped around the phalanges instead of
    // treating MakeHuman's helper joints as surface landmarks.
    std::array<std::array<double,2>,5> sourceZ{};
    std::array<std::vector<Vec3>,5> toeBones;
    for(int t=0;t<5;++t) {
        sourceZ[t]={1e30,-1e30};
        for(const auto& v:vertices) if(v.influence/10==t+1) {
            sourceZ[t][0]=std::min(sourceZ[t][0],double(v.z));
            sourceZ[t][1]=std::max(sourceZ[t][1],double(v.z));
        }
        for(int segment=0;segment<3;++segment) {
            int id=bones[t*3+segment];if(id<0)continue;
            const auto& part=c.parts()[std::size_t(id)];
            for(Vec3 p:part.points)toeBones[t].push_back(c.mapPart(p,part)+origin);
        }
    }
    std::array<int,5> anchorStart{0,3,7,11,15};
    result.points.reserve(N);result.owners.reserve(N);
    std::size_t pointIndex=0;
    for(const auto& v:vertices) {
        Vec3 p=affine(source(v.x,v.y,v.z));Vec3 adjustment{};double sum=0;
        for(int k=0;k<19;++k) {
            Vec3 d=p-srcControl[k];d.y*=.45;
            const double radius=size*.075, w=std::exp(-dot(d,d)/(radius*radius));
            adjustment+=delta[k]*w;sum+=w;
        }
        p+=adjustment/std::max(sum,.5);
        // Match the full source cross section to the bone envelope at this
        // station, retaining the original surface's relative up/down shape.
        double total=0,targetY=0;
        for(int t=0;t<5;++t) {
            if(toeBones[t].empty())continue;
            const auto& start=anchors[anchorStart[t]];
            double dz=double(v.z)-start[2];
            double fade=std::clamp((dz+.10)/.20,0.0,1.0);
            if(!fade || double(v.z)>sourceZ[t][1]+.12)continue;
            double dx=double(v.x)-start[0];
            const double w=fade*std::exp(-(dx*dx)/(.14*.14));
            if(w<1e-5)continue;
            double minY=1e30,maxY=-1e30,nearest=1e30;
            for(const auto& q:vertices) if(q.influence/10==t+1)
                nearest=std::min(nearest,std::abs(double(q.z)-v.z));
            const double sample=std::max(.18,nearest+1e-5);
            for(const auto& q:vertices) if(q.influence/10==t+1 && std::abs(double(q.z)-v.z)<=sample) {
                minY=std::min(minY,double(q.y));maxY=std::max(maxY,double(q.y));
            }
            if(maxY-minY<1e-5)continue;
            const double rel=std::clamp((double(v.y)-minY)/(maxY-minY),0.0,1.0);
            double boneLo=1e30,boneHi=-1e30,nearZ=1e30;
            for(Vec3 q:toeBones[t]) nearZ=std::min(nearZ,std::abs(q.z-p.z));
            const double width=std::max(size*.05,nearZ+1e-6);
            for(Vec3 q:toeBones[t]) if(std::abs(q.z-p.z)<=width) {
                boneLo=std::min(boneLo,q.y);boneHi=std::max(boneHi,q.y);
            }
            if(boneLo>boneHi)continue;
            const double padding=size*.018;
            const double y=(boneLo-padding)+(boneHi-boneLo+2*padding)*rel;
            targetY+=y*w;total+=w;
        }
        if(total>1e-8) {const double blend=std::min(1.0,total*1.2);p.y+=(targetY/total-p.y)*blend;}
        p.y=std::max(p.y,low.y);
        const auto& fix=correction[pointIndex++];
        if(applyCorrection) p+=Vec3{mirror ? -fix[0]*size : fix[0]*size,fix[1]*size,fix[2]*size};
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
MakeHumanFoot makeHumanFoot(const Comparison& c,bool male,const std::string& side,Vec3 origin,bool applyCorrection=true) {
    using namespace makehuman_foot_data;
    return male ? makehuman_foot_detail::fit(c,side,origin,maleVertices,maleFaces,maleAnchors,maleCorrections,applyCorrection)
                : makehuman_foot_detail::fit(c,side,origin,femaleVertices,femaleFaces,femaleAnchors,femaleCorrections,applyCorrection);
}
namespace makehuman_foot_detail {
template<class Comparison,std::size_t N,std::size_t V,std::size_t F>
MakeHumanFoot reduce(const Comparison& c,const std::string& side,const MakeHumanFoot& high,
                     const std::array<makehuman_foot_data::Vertex,N>& source,
                     const std::array<makehuman_foot_data::ReducedVertex,V>& verts,
                     const std::array<std::array<int,3>,F>& faces) {
    if(high.points.size()!=N)return {};
    MakeHumanFoot low;low.length=high.length;
    int foot=-1;std::array<int,64> ids{};ids.fill(-1);
    for(std::size_t i=0;i<c.parts().size();++i) {
        const auto& part=c.parts()[i];if(!part.name.ends_with(side))continue;
        if(part.name=="FootMeta3"+side)foot=static_cast<int>(i);
        for(int toe=1;toe<=5;++toe)for(int segment=1;segment<=3;++segment) {
            const std::string prefix=segment==1?"FootProxP":(segment==2 && toe>1?"FootMidP":"FootDistP");
            if(part.name==prefix+std::to_string(toe)+side)ids[toe*10+segment]=static_cast<int>(i);
        }
    }
    if(foot<0)return {};
    for(const auto& vertex:verts) {
        Vec3 point{};std::array<double,64> weights{};
        for(int k=0;k<3;++k) {
            const int index=vertex.source[k];const double weight=vertex.barycentric[k];
            point+=high.points[std::size_t(index)]*weight;
            const auto& v=source[std::size_t(index)];
            for(int t=0;t<4;++t) weights[v.bones[t]]+=weight*v.weights[t];
        }
        point+=Vec3{(side=="_R"?-1:1)*vertex.offset[0]*high.length,
                    vertex.offset[1]*high.length,vertex.offset[2]*high.length};
        int first=0,second=0;
        for(int i=1;i<64;++i)if(weights[i]>weights[first])first=i;
        for(int i=0;i<64;++i)if(i!=first && (second==first || weights[i]>weights[second]))second=i;
        const int firstId=first==0 || ids[first]<0 ? foot:ids[first];
        const int secondId=second==0 || ids[second]<0 ? foot:ids[second];
        const double sum=weights[first]+weights[second];
        low.points.push_back(point);low.owners.push_back(firstId);low.secondaryOwners.push_back(secondId);
        low.ownerWeights.push_back(sum>1e-9?weights[first]/sum:1.0);
    }
    low.faces.assign(faces.begin(),faces.end());
    if(side=="_R")for(auto& f:low.faces)std::swap(f[1],f[2]);
    return low;
}
} // namespace makehuman_foot_detail
template<class Comparison>
MakeHumanFoot makeHumanFootReduced(const Comparison& c,bool male,const std::string& side,Vec3 origin) {
    const auto high=makeHumanFoot(c,male,side,origin);
    using namespace makehuman_foot_data;
    return male ? makehuman_foot_detail::reduce(c,side,high,maleVertices,maleReducedVertices,maleReducedFaces)
                : makehuman_foot_detail::reduce(c,side,high,femaleVertices,femaleReducedVertices,femaleReducedFaces);
}
} // namespace gtc::native
