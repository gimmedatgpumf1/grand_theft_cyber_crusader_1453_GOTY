// Every blue vertebra must be a reflected triangle mesh about its own fitted
// 3D plane, including after the actual comparison slider mapping.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace gtc::native;
using PointKey=std::array<long long,3>;
using FaceKey=std::array<PointKey,3>;
static FaceKey canonical(FaceKey f) {
    return std::min({f,FaceKey{f[1],f[2],f[0]},FaceKey{f[2],f[0],f[1]}});
}
static Vec3 transform(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static void symmetric(const SkeletonComparison& comparison,const SkeletonComparison::Part& part,SkullMidplane plane) {
    plane.origin=comparison.mapPart(part.vertebraAnchor,part);
    if (!part.cranial) {
        plane.normal=normalized(cross(part.mappedUp,part.mappedForward));
        plane.up=normalized(part.mappedUp);
        plane.forward=normalized(cross(plane.normal,plane.up));
    }
    std::vector<PointKey> points; points.reserve(part.points.size());
    for (Vec3 p:part.points) {
        const Vec3 q=plane.local(comparison.mapPart(p,part));
        points.push_back({std::llround(q.x*1e7),std::llround(q.y*1e7),std::llround(q.z*1e7)});
    }
    std::map<FaceKey,int> triangles;
    for (auto f:part.faces) ++triangles[canonical({points[f[0]],points[f[1]],points[f[2]]})];
    for (auto f:part.faces) {
        auto a=points[f[0]],b=points[f[1]],c=points[f[2]];
        a[0]=-a[0]; b[0]=-b[0]; c[0]=-c[0];
        const auto found=triangles.find(canonical({a,c,b}));
        assert(found!=triangles.end() && found->second==triangles[canonical({points[f[0]],points[f[1]],points[f[2]]})]);
    }
}
int main() {
    std::string error; SkeletonRoom room; assert(room.load(".",error));
    for (bool male:{true,false}) {
        const auto& model=male?room.male():room.female(); const auto& copy=room.comparison(male);
        const auto matrices=model.sourceMatrices({},0); const std::string sex=male?"male":"female";
        std::map<std::string,VertebraPlane> fits;
        assert(loadVertebraPlanes("tools/skeleton-source/skeleton-"+sex+".vertebra-planes",fits));
        SkeletonModel raw; assert(raw.load("tools/skeleton-source/skeleton-"+sex+".obj",error));
        std::map<std::string,SkullMidplane> planes;
        for (const auto& name:vertebraNames()) {
            const int id=model.sourceStructure(name),rawId=raw.sourceStructure(name); assert(id>=0 && rawId>=0);
            const auto& s=model.sourceStructures()[id]; const auto& r=raw.sourceStructures()[rawId];
            const auto& group=model.bones()[s.group]; const auto& rg=raw.bones()[r.group];
            assert(s.pointCount==r.pointCount && s.triangleCount==r.triangleCount);
            std::vector<Vec3> vertices; std::vector<std::array<int,3>> faces;
            for (std::size_t i=0;i<s.pointCount;++i) {
                const Vec3 a=group.points[s.firstPoint+i],b=rg.points[r.firstPoint+i];
                assert(a.x==b.x && a.y==b.y && a.z==b.z);
                vertices.push_back(transform(matrices[id],a));
            }
            for (std::size_t i=0;i<s.triangleCount;++i) {
                auto a=group.triangles[s.firstTriangle+i],b=rg.triangles[r.firstTriangle+i];
                for (int k=0;k<3;++k) {
                    a[k]-=static_cast<int>(s.firstPoint); b[k]-=static_cast<int>(r.firstPoint); assert(a[k]==b[k]);
                }
                faces.push_back(a);
            }
            const auto plane=posedVertebraPlane(fits.at(name),matrices[id]); planes.emplace(name,plane);
            bilateral::TriangleSurfaceTree source(vertices,faces);
            double errorSum=0,totalArea=0;
            for (const auto& f:faces) {
                Vec3 p=(vertices[f[0]]+vertices[f[1]]+vertices[f[2]])/3;
                const double area=length(cross(vertices[f[1]]-vertices[f[0]],vertices[f[2]]-vertices[f[0]]))*0.5;
                p=p-plane.normal*(2*dot(p-plane.origin,plane.normal));
                errorSum+=area*length(source.nearest(p)-p); totalArea+=area;
            }
            assert(totalArea>0 && errorSum/totalArea>0.00001);
            const auto& part=copy.parts()[static_cast<std::size_t>(id)]; assert(part.vertebra);
            symmetric(copy,part,plane);
            std::printf("%s %s: original mean reflection error %.3f mm; blue mirrored triangles PASS\n",
                        sex.c_str(),name.c_str(),errorSum/totalArea*1000);
        }
        for (double height:{50.,170.,250.}) {
            auto profile=copy.profile(); profile.height=height; profile.multiplier.fill(1);
            for (int setting=-1;setting<2*int(SkeletonComparison::Count-1)+2;++setting) {
                auto candidate=profile;
                if (setting>=0 && setting<2*int(SkeletonComparison::Count-1)) {
                    const int dimension=SkeletonComparison::Head+setting/2;
                    const auto bounds=copy.dimensionLimits(dimension,height); assert(bounds);
                    candidate.multiplier[dimension]=(setting%2?bounds->maximum:bounds->minimum)/bounds->center;
                } else if (setting>=2*int(SkeletonComparison::Count-1)) {
                    const bool upper=setting%2;
                    for (int dimension=SkeletonComparison::Head;dimension<SkeletonComparison::Count;++dimension) {
                        const auto bounds=copy.dimensionLimits(dimension,height); assert(bounds);
                        candidate.multiplier[dimension]=(upper?bounds->maximum:bounds->minimum)/bounds->center;
                    }
                }
                assert(room.setComparison(male,candidate,error));
                for (const auto& name:vertebraNames()) {
                    const int id=model.sourceStructure(name);
                    symmetric(copy,copy.parts()[static_cast<std::size_t>(id)],planes.at(name));
                }
            }
        }
        room.resetComparison(male);
    }
    std::puts("24 independently fitted vertebrae: PASS (both sexes, original preservation, slider mapping)");
}
