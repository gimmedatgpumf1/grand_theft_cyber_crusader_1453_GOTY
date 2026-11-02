// Audit every triangle of both neutral blue skeletons about ONE upright plane.
// This tests geometry, not anatomical quality, articulation or joint clearance.
#include "client/skeleton_room.h"
#include "mirror_triangle_audit.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
using namespace gtc::native;
static void axial(const SkeletonComparison& copy,const SkeletonComparison::Part& part) {
    assert(!part.points.empty() && !part.faces.empty());
    std::vector<Vec3> points; points.reserve(part.points.size());
    for (Vec3 p:part.points) points.push_back(copy.mapPart(p,part));
    if (!reflectedTriangles(points,part.faces,1e-9*std::max(1.0,copy.profile().height/100))) {
        std::fprintf(stderr,"Unmatched axial triangle in %s at %.1f cm\n",part.name.c_str(),copy.profile().height);
        std::abort();
    }
}
static void audit(const SkeletonComparison& copy) {
    std::map<std::string,const SkeletonComparison::Part*> parts;
    for (const auto& part:copy.parts()) assert(parts.emplace(part.name,&part).second);
    assert(parts.size()==182);
    std::set<std::string> central={"Head","Jaw","Sternum","Sacrum"};
    for (const auto& name:vertebraNames()) central.insert(name);
    assert(central.size()==28);
    for (const auto& name:central) {
        const auto found=parts.find(name); assert(found!=parts.end());
        axial(copy,*found->second);
    }
    int pairs=0;
    for (const auto& [name,left]:parts) {
        if (central.count(name)) continue;
        assert(name.size()>2);
        const auto suffix=name.substr(name.size()-2); assert(suffix=="_L" || suffix=="_R");
        if (suffix=="_R") continue;
        ++pairs; const auto found=parts.find(name.substr(0,name.size()-2)+"_R"); assert(found!=parts.end());
        const auto* right=found->second;
        assert(!left->points.empty() && !left->faces.empty());
        assert(left->points.size()==right->points.size() && left->faces.size()==right->faces.size());
        for (std::size_t i=0;i<left->points.size();++i) {
            Vec3 a=copy.mapPart(left->points[i],*left),b=copy.mapPart(right->points[i],*right);
            assert(length(Vec3{-a.x,a.y,a.z}-b)<1e-8*std::max(1.0,copy.profile().height/100));
        }
        for (std::size_t i=0;i<left->faces.size();++i) {
            auto a=left->faces[i],b=right->faces[i];
            assert(a[0]==b[0] && a[1]==b[2] && a[2]==b[1]);
        }
    }
    assert(pairs==77);
}
static void unchanged(const SkeletonModel& model,const SkeletonModel& raw) {
    assert(model.sourceStructures().size()==raw.sourceStructures().size());
    for (const auto& a:model.sourceStructures()) {
        const int j=raw.sourceStructure(a.name); assert(j>=0); const auto& b=raw.sourceStructures()[j];
        const auto& x=model.bones()[a.group]; const auto& y=raw.bones()[b.group];
        assert(a.pointCount==b.pointCount && a.triangleCount==b.triangleCount);
        for (std::size_t k=0;k<a.pointCount;++k) {
            const Vec3 p=x.points[a.firstPoint+k],q=y.points[b.firstPoint+k];
            assert(p.x==q.x && p.y==q.y && p.z==q.z);
        }
        for (std::size_t k=0;k<a.triangleCount;++k) for (int v=0;v<3;++v)
            assert(x.triangles[a.firstTriangle+k][v]-int(a.firstPoint)==y.triangles[b.firstTriangle+k][v]-int(b.firstPoint));
    }
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    for (bool male:{true,false}) {
        const auto& model=male?room.male():room.female(); SkeletonModel raw;
        assert(raw.load(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".obj",error));
        unchanged(model,raw); int profiles=0;
        const auto check=[&](SkeletonComparison::Profile p) {
            assert(room.setComparison(male,p,error)); audit(room.comparison(male)); ++profiles;
        };
        check(room.comparison(male).profile());
        for (double height:{50.,170.,250.}) {
            auto base=room.comparison(male).profile(); base.height=height; base.multiplier.fill(1); check(base);
            for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
                const auto bounds=room.comparison(male).dimensionLimits(dim,height); assert(bounds);
                for (double endpoint:{bounds->minimum,bounds->maximum}) {
                    auto p=base; p.multiplier[dim]=endpoint/bounds->center; check(p);
                }
            }
            for (bool upper:{false,true}) {
                auto p=base;
                for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
                    const auto bounds=room.comparison(male).dimensionLimits(dim,height); assert(bounds);
                    p.multiplier[dim]=(upper?bounds->maximum:bounds->minimum)/bounds->center;
                }
                check(p);
            }
        }
        assert(profiles==52); unchanged(model,raw);
        std::printf("%s: 77 pairs and 28 axial bones, one plane, %d profiles: PASS\n",male?"male":"female",profiles);
        room.resetComparison(male);
    }
    std::puts("Whole neutral blue skeleton reflection and white source preservation: PASS");
}
