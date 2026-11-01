// Audit every rib pair separately in both comparison skeletons. The ribs are
// paired left/right bones; a single curved rib is not reflected onto itself.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace gtc::native;

struct Surface { std::vector<Vec3> points; std::vector<std::array<int,3>> faces; };
static Vec3 transform(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Surface source(const SkeletonModel& model,int index,const std::array<double,16>& matrix) {
    const auto& s=model.sourceStructures()[index]; const auto& group=model.bones()[s.group]; Surface out;
    for (std::size_t i=0;i<s.pointCount;++i) out.points.push_back(transform(matrix,group.points[s.firstPoint+i]));
    for (std::size_t i=0;i<s.triangleCount;++i) {
        auto face=group.triangles[s.firstTriangle+i];
        for (int& vertex:face) vertex-=static_cast<int>(s.firstPoint);
        out.faces.push_back(face);
    }
    return out;
}
static double hipAxis(const SkeletonModel& model,const std::vector<std::array<double,16>>& matrices,const std::string& sex) {
    std::ifstream input("tools/skeleton-source/skeleton-"+sex+".hips"); std::string line;
    double x[2]={}; bool seen[2]={};
    while (std::getline(input,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line); char side; Vec3 p; double radius;
        assert(row>>side>>p.x>>p.y>>p.z>>radius); assert(side=='L' || side=='R');
        const int slot=side=='L'?0:1; assert(!seen[slot]); seen[slot]=true;
        const int id=model.sourceStructure(std::string("Femur_")+side); assert(id>=0);
        x[slot]=transform(matrices[id],p).x;
    }
    assert(input.eof() && seen[0] && seen[1]); return (x[0]+x[1])*0.5;
}
// Area-weighted triangle-centroid-to-opposite-surface distance. The source
// measurement includes the two bones' posed positions as well as shape.
static double distance(const Surface& a,const Surface& b) {
    bilateral::TriangleSurfaceTree target(b.points,b.faces); double sum=0,area=0;
    for (auto f:a.faces) {
        const Vec3 p=a.points[f[0]],q=a.points[f[1]],r=a.points[f[2]];
        const double weight=length(cross(q-p,r-p))*0.5; const Vec3 center=(p+q+r)/3;
        sum+=weight*length(target.nearest(center)-center); area+=weight;
    }
    assert(area>0); return sum/area;
}
static void unchanged(const SkeletonModel& model,const SkeletonModel& raw,const std::string& name) {
    const int i=model.sourceStructure(name),j=raw.sourceStructure(name); assert(i>=0 && j>=0);
    const auto& a=model.sourceStructures()[i]; const auto& b=raw.sourceStructures()[j];
    const auto& x=model.bones()[a.group]; const auto& y=raw.bones()[b.group];
    assert(a.pointCount==b.pointCount && a.triangleCount==b.triangleCount);
    for (std::size_t k=0;k<a.pointCount;++k) {
        const Vec3 p=x.points[a.firstPoint+k],q=y.points[b.firstPoint+k];
        assert(p.x==q.x && p.y==q.y && p.z==q.z);
    }
    for (std::size_t k=0;k<a.triangleCount;++k) for (int v=0;v<3;++v)
        assert(x.triangles[a.firstTriangle+k][v]-int(a.firstPoint)==
               y.triangles[b.firstTriangle+k][v]-int(b.firstPoint));
}
static void audit(const SkeletonComparison& comparison,const SkeletonModel& model,std::array<double,12>& worst) {
    for (int n=1;n<=12;++n) {
        const std::string base="Rib"+std::to_string(n);
        const int il=model.sourceStructure(base+"_L"),ir=model.sourceStructure(base+"_R");
        assert(il>=0 && ir>=0);
        const auto& left=comparison.parts()[il]; const auto& right=comparison.parts()[ir];
        assert(left.name==base+"_L" && right.name==base+"_R");
        assert(!left.points.empty() && !left.faces.empty());
        assert(left.points.size()==right.points.size() && left.faces.size()==right.faces.size());
        for (std::size_t i=0;i<left.points.size();++i) {
            const Vec3 l=comparison.mapPart(left.points[i],left),r=comparison.mapPart(right.points[i],right);
            worst[n-1]=std::max(worst[n-1],length(Vec3{-l.x,l.y,l.z}-r));
        }
        for (std::size_t i=0;i<left.faces.size();++i) {
            const auto l=left.faces[i],r=right.faces[i];
            assert(l[0]==r[0] && l[1]==r[2] && l[2]==r[1]);
        }
        assert(worst[n-1]<1e-8*std::max(1.0,comparison.profile().height/100));
    }
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    for (bool male:{true,false}) {
        const std::string sex=male?"male":"female"; const auto& model=male?room.male():room.female();
        const auto matrices=model.sourceMatrices({},0); SkeletonModel raw;
        assert(raw.load("tools/skeleton-source/skeleton-"+sex+".obj",error));
        const double axis=hipAxis(model,matrices,sex); std::array<double,12> asym{},worst{};
        for (int n=1;n<=12;++n) {
            const std::string base="Rib"+std::to_string(n);
            unchanged(model,raw,base+"_L"); unchanged(model,raw,base+"_R");
            const int il=model.sourceStructure(base+"_L"),ir=model.sourceStructure(base+"_R");
            assert(il>=0 && ir>=0);
            Surface left=source(model,il,matrices[il]),right=source(model,ir,matrices[ir]);
            for (Vec3& p:right.points) p.x=2*axis-p.x;
            asym[n-1]=(distance(left,right)+distance(right,left))*0.5;
        }
        int profiles=0;
        const auto check=[&](SkeletonComparison::Profile profile) {
            assert(room.setComparison(male,profile,error)); audit(room.comparison(male),model,worst); ++profiles;
        };
        check(room.comparison(male).profile());
        for (double height:{50.,170.,250.}) {
            auto base=room.comparison(male).profile(); base.height=height; base.multiplier.fill(1); check(base);
            for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
                const auto bounds=room.comparison(male).dimensionLimits(dim,height); assert(bounds);
                for (double endpoint:{bounds->minimum,bounds->maximum}) {
                    auto profile=base; profile.multiplier[dim]=endpoint/bounds->center; check(profile);
                }
            }
            for (bool upper:{false,true}) {
                auto profile=base;
                for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
                    const auto bounds=room.comparison(male).dimensionLimits(dim,height); assert(bounds);
                    profile.multiplier[dim]=(upper?bounds->maximum:bounds->minimum)/bounds->center;
                }
                check(profile);
            }
        }
        assert(profiles==52);
        for (int n=1;n<=12;++n) {
            const std::string base="Rib"+std::to_string(n);
            unchanged(model,raw,base+"_L"); unchanged(model,raw,base+"_R");
            std::printf("%s rib %d: source reflected mean distance %.3f mm; blue max mirror error %.6g mm\n",
                        sex.c_str(),n,asym[n-1]*1000,worst[n-1]*1000);
        }
        room.resetComparison(male);
    }
    std::puts("Every rib pair: PASS (both sexes, original preservation and 52 profiles)");
}
