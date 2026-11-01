// Verify the actual comparison skull/jaw meshes, including their triangle
// winding, and prove that the white reference mesh remains the source mesh.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
using namespace gtc::native;

static Vec3 transform(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
using PointKey=std::array<long long,3>;
using FaceKey=std::array<PointKey,3>;
static FaceKey canonical(FaceKey f) {
    const FaceKey a={f[1],f[2],f[0]}, b={f[2],f[0],f[1]};
    return std::min({f,a,b});
}
static void mirroredTriangles(const SkeletonComparison& copy,const SkeletonComparison::Part& part,bool mapped) {
    const double axis=mapped?0:(copy.hipCenters()[0].x+copy.hipCenters()[1].x)*0.5;
    std::vector<PointKey> vertices; vertices.reserve(part.points.size());
    for (Vec3 p:part.points) {
        if (mapped) p=copy.map(p,part.armSide,part.cranial,part.girdle,part.humerus);
        vertices.push_back({std::llround((p.x-axis)*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)});
    }
    std::map<FaceKey,int> faces;
    for (auto f:part.faces) {
        for (int i:f) assert(i>=0 && static_cast<std::size_t>(i)<vertices.size());
        ++faces[canonical({vertices[f[0]],vertices[f[1]],vertices[f[2]]})];
    }
    for (auto f:part.faces) {
        auto a=vertices[f[0]],b=vertices[f[1]],c=vertices[f[2]];
        a[0]=-a[0]; b[0]=-b[0]; c[0]=-c[0];
        auto found=faces.find(canonical({a,c,b}));
        assert(found!=faces.end() && found->second>0);
        --found->second;
    }
    for (const auto& entry:faces) assert(entry.second==0);
}
static void checkSkull(SkeletonRoom& room,bool male) {
    const auto& model=male?room.male():room.female();
    const auto& copy=room.comparison(male);
    const auto matrices=model.sourceMatrices({},0);
    const double axis=(copy.hipCenters()[0].x+copy.hipCenters()[1].x)*0.5;
    SkeletonModel source; std::string error;
    assert(source.load(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".obj",error));
    for (const char* name:{"Head","Jaw"}) {
        const int id=model.sourceStructure(name), rawId=source.sourceStructure(name);
        assert(id>=0 && rawId>=0);
        const auto& s=model.sourceStructures()[id]; const auto& raw=source.sourceStructures()[rawId];
        const auto& group=model.bones()[s.group]; const auto& rawGroup=source.bones()[raw.group];
        assert(s.pointCount==raw.pointCount && s.triangleCount==raw.triangleCount);
        std::vector<Vec3> original,opposite;
        std::vector<std::array<int,3>> triangles;
        for (std::size_t i=0;i<s.pointCount;++i) {
            const Vec3 p=group.points[s.firstPoint+i], q=rawGroup.points[raw.firstPoint+i];
            assert(p.x==q.x && p.y==q.y && p.z==q.z);
            Vec3 posed=transform(matrices[id],p); original.push_back(posed);
            posed.x=2*axis-posed.x; opposite.push_back(posed);
        }
        for (std::size_t i=0;i<s.triangleCount;++i) {
            auto f=group.triangles[s.firstTriangle+i], rf=rawGroup.triangles[raw.firstTriangle+i];
            for (int j=0;j<3;++j) {
                f[j]-=static_cast<int>(s.firstPoint); rf[j]-=static_cast<int>(raw.firstPoint);
                assert(f[j]==rf[j]);
            }
            triangles.push_back(f);
        }
        auto averaged=original;
        assert(bilateral::average(averaged,opposite,triangles));
        // Pure recentering plus mirroring would simply discard one side. A
        // nonzero shape displacement proves the other side contributed too.
        double centroid=0, maxDisplacement=0;
        for (Vec3 p:original) centroid+=p.x;
        centroid/=original.size();
        for (std::size_t i=0;i<original.size();++i) {
            Vec3 recentered=original[i]; recentered.x+=axis-centroid;
            maxDisplacement=std::max(maxDisplacement,length(averaged[i]-recentered));
        }
        assert(maxDisplacement>1e-5);
        assert(bilateral::mirrorAxial(averaged,triangles,axis));
        const auto& result=copy.parts()[static_cast<std::size_t>(id)];
        assert(result.points.size()==averaged.size() && result.faces==triangles);
        for (std::size_t i=0;i<averaged.size();++i) assert(length(result.points[i]-averaged[i])<1e-12);
        mirroredTriangles(copy,result,false); mirroredTriangles(copy,result,true);
        std::printf("%s %s: source unchanged; averaged, mirrored %zu triangles (max shape shift %.3f mm)\n",
                    male?"male":"female",name,result.faces.size(),maxDisplacement*1000);
    }
    for (double height:{50.,250.}) for (double headMultiplier:{0.8,1.2}) {
        auto profile=copy.profile(); profile.height=height; profile.multiplier[SkeletonComparison::Head]=headMultiplier;
        assert(room.setComparison(male,profile,error));
        for (const auto& part:copy.parts()) if (part.name=="Head" || part.name=="Jaw")
            mirroredTriangles(copy,part,true);
    }
    room.resetComparison(male);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    checkSkull(room,true); checkSkull(room,false);
    std::puts("Skull and jaw bilateral triangle symmetry: PASS");
}
