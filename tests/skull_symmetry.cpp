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
static void mirroredTriangles(const SkeletonComparison& copy,const SkeletonComparison::Part& part,
                              SkullMidplane plane,bool mapped) {
    if (mapped) plane.origin=copy.map(plane.origin,0,true);
    std::vector<PointKey> vertices; vertices.reserve(part.points.size());
    for (Vec3 p:part.points) {
        if (mapped) p=copy.map(p,part.armSide,part.cranial,part.girdle,part.humerus);
        p=plane.local(p);
        vertices.push_back({std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)});
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
    const int headId=model.sourceStructure("Head"); assert(headId>=0);
    const SkullMidplane plane=skullMidplane(male,matrices[headId]);
    const SkullMidplane aligned{plane.origin,{1,0,0},{0,1,0},{0,0,1}};
    SkeletonModel source; std::string error;
    assert(source.load(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".obj",error));
    // Check the skull-only plane against the original skull and independent
    // eye landmarks, rather than comparing it with a body or hip midline.
    const std::array<double,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const SkullMidplane rawPlane=skullMidplane(male,identity);
    const int rawHeadId=source.sourceStructure("Head"); assert(rawHeadId>=0);
    const auto& head=source.sourceStructures()[static_cast<std::size_t>(rawHeadId)];
    const auto& headGroup=source.bones()[head.group];
    std::vector<Vec3> skull(headGroup.points.begin()+head.firstPoint,
                            headGroup.points.begin()+head.firstPoint+head.pointCount);
    Vec3 centroid{}; for (Vec3 p:skull) centroid+=p; centroid=centroid/static_cast<double>(skull.size());
    bilateral::PointTree tree(skull); double fittedError=0,axisError=0;
    for (std::size_t i=0;i<skull.size();i+=3) {
        const Vec3 p=skull[i];
        const Vec3 reflected=p-rawPlane.normal*(2*dot(p-rawPlane.origin,rawPlane.normal));
        const Vec3 axisReflected{2*centroid.x-p.x,p.y,p.z};
        const Vec3 fittedDelta=tree.nearest(reflected)-reflected, axisDelta=tree.nearest(axisReflected)-axisReflected;
        fittedError+=std::min(dot(fittedDelta,fittedDelta),0.006*0.006);
        axisError+=std::min(dot(axisDelta,axisDelta),0.006*0.006);
    }
    assert(fittedError<axisError*0.75);
    const auto& eyes=*model.orbitalCenters();
    assert(std::abs(dot((eyes[0]+eyes[1])*0.5-rawPlane.origin,rawPlane.normal))<0.003);
    assert(std::abs(dot(normalized(eyes[1]-eyes[0]),rawPlane.normal))>std::cos(5.0*3.141592653589793/180));
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
            Vec3 posed=plane.local(transform(matrices[id],p)); original.push_back(posed);
            posed.x=-posed.x; opposite.push_back(posed);
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
        assert(bilateral::average(averaged,opposite,triangles,false,false));
        // Mirroring only one side would discard the other. Verify that both
        // contribute a real shape change about the skull-derived plane.
        double maxDisplacement=0;
        for (std::size_t i=0;i<original.size();++i)
            maxDisplacement=std::max(maxDisplacement,length(averaged[i]-original[i]));
        assert(maxDisplacement>1e-5);
        assert(bilateral::mirrorAxial(averaged,triangles,0));
        const auto& result=copy.parts()[static_cast<std::size_t>(id)];
        assert(result.points.size()==averaged.size() && result.faces==triangles);
        for (std::size_t i=0;i<averaged.size();++i) assert(length(result.points[i]-(plane.origin+averaged[i]))<1e-12);
        mirroredTriangles(copy,result,aligned,false); mirroredTriangles(copy,result,aligned,true);
        std::printf("%s %s: source unchanged; averaged, mirrored %zu triangles (max shape shift %.3f mm)\n",
                    male?"male":"female",name,result.faces.size(),maxDisplacement*1000);
    }
    for (double height:{50.,250.}) for (double headMultiplier:{0.8,1.2}) {
        auto profile=copy.profile(); profile.height=height; profile.multiplier[SkeletonComparison::Head]=headMultiplier;
        assert(room.setComparison(male,profile,error));
        for (const auto& part:copy.parts()) if (part.name=="Head" || part.name=="Jaw")
            mirroredTriangles(copy,part,aligned,true);
    }
    room.resetComparison(male);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    checkSkull(room,true); checkSkull(room,false);
    std::puts("Skull and jaw bilateral triangle symmetry: PASS");
}
