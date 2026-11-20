// Audit every blue left/right mesh and its actual mapped vertices. The white
// CT-derived structures are intentionally not made symmetric.
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

static int category(const std::string& name) {
    if (name.rfind("Hand",0)==0 || name=="Humerus" || name=="Radius" || name=="Ulna") return 0;
    if (name.rfind("Foot",0)==0 || name=="Femur" || name=="Patella" || name=="Tibia" ||
        name=="Fibula" || name=="Tal" || name=="Cal" || name=="Nav" || name=="Cub" ||
        name=="MedCun" || name=="InterCun" || name=="LatCun") return 1;
    if (name=="Scapula" || name=="Clavicula") return 2;
    if (name.rfind("Rib",0)==0) return 3;
    if (name=="Pelvis") return 4;
    return -1; // A new or unaccounted-for pair must update the checklist.
}
static void audit(const SkeletonComparison& comparison,std::array<double,5>& worst) {
    std::map<std::string,const SkeletonComparison::Part*> left,right;
    for (const auto& part:comparison.parts()) {
        if (part.name.size()<3) continue;
        const auto suffix=part.name.substr(part.name.size()-2);
        const auto name=part.name.substr(0,part.name.size()-2);
        if (suffix=="_L") assert(left.emplace(name,&part).second);
        if (suffix=="_R") assert(right.emplace(name,&part).second);
    }
    assert(left.size()==77 && right.size()==77);
    std::array<int,5> counts{};
    for (const auto& [name,a]:left) {
        const int group=category(name); assert(group>=0); ++counts[group];
        const auto found=right.find(name); assert(found!=right.end());
        const auto* b=found->second;
        assert(a->points.size()==b->points.size() && a->faces.size()==b->faces.size());
        for (std::size_t i=0;i<a->points.size();++i) {
            const Vec3 l=comparison.map(a->points[i],a->armSide,a->cranial,a->girdle,a->humerus);
            const Vec3 r=comparison.map(b->points[i],b->armSide,b->cranial,b->girdle,b->humerus);
            worst[group]=std::max(worst[group],length(Vec3{-l.x,l.y,l.z}-r));
        }
        for (std::size_t i=0;i<a->faces.size();++i) {
            const auto l=a->faces[i],r=b->faces[i];
            assert(l[0]==r[0] && l[1]==r[2] && l[2]==r[1]);
        }
    }
    assert((counts==std::array<int,5>{30,32,2,12,1}));
    const double tolerance=1e-8*std::max(1.0,comparison.profile().height/100);
    for (double error:worst) assert(error<tolerance);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    const char* names[]={"arms and hands","legs and feet","shoulder girdle","ribs","pelvis"};
    for (bool male:{true,false}) {
        std::array<double,5> worst{}; int profiles=0;
        const auto check=[&](SkeletonComparison::Profile profile) {
            assert(room.setComparison(male,profile,error));
            audit(room.comparison(male),worst); ++profiles;
        };
        check(room.comparison(male).profile());
        for (double height:{50.,170.,250.}) {
            auto base=room.comparison(male).profile(); base.height=height;
            base.multiplier.fill(1); check(base);
            for (int dimension=SkeletonComparison::Head;dimension<SkeletonComparison::Count;++dimension) {
                const auto bounds=room.comparison(male).dimensionLimits(dimension,height); assert(bounds);
                for (double endpoint:{bounds->minimum,bounds->maximum}) {
                    auto profile=base; profile.multiplier[dimension]=endpoint/bounds->center; check(profile);
                }
            }
            for (bool upper:{false,true}) {
                auto profile=base;
                for (int dimension=SkeletonComparison::Head;dimension<SkeletonComparison::Count;++dimension) {
                    const auto bounds=room.comparison(male).dimensionLimits(dimension,height); assert(bounds);
                    profile.multiplier[dimension]=(upper?bounds->maximum:bounds->minimum)/bounds->center;
                }
                check(profile);
            }
        }
        for (int i=0;i<5;++i)
            std::printf("%s %s: max mirrored vertex error %.6g mm (%d profiles)\n",
                        male?"male":"female",names[i],worst[i]*1000,profiles);
        room.resetComparison(male);
    }
    std::puts("All 77 paired blue bone meshes and winding: PASS (both sexes, 52 profiles each)");
}
