// Actual comparison toe mesh topology, reflection and slider endpoint audit.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
using namespace gtc::native;

static void audit(const SkeletonComparison& copy) {
    const auto mesh=copy.toeSurface();
    assert(mesh.skin.size()==800 && mesh.skinFaces.size()==1560);
    assert(mesh.nails.size()==60 && mesh.nailFaces.size()==40);
    const auto check=[&](const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces,bool closed) {
        const auto nv=points.size()/2,nf=faces.size()/2;
        for (std::size_t i=0;i<nv;++i) {
            const Vec3 a=points[i],b=points[i+nv];
            assert(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z));
            assert(a.y>=-1e-10 && std::abs(a.x+b.x)<1e-12 && a.y==b.y && a.z==b.z);
        }
        std::map<std::pair<int,int>,int> count,orientation;
        for (std::size_t i=0;i<nf;++i) {
            const auto a=faces[i],b=faces[i+nf];
            assert(b[0]==a[0]+int(nv) && b[1]==a[2]+int(nv) && b[2]==a[1]+int(nv));
            for (int n:a) assert(n>=0 && n<int(nv));
            assert(length(cross(points[a[1]]-points[a[0]],points[a[2]]-points[a[0]]))>1e-14);
            for (int j=0;j<3;++j) {
                int x=a[j],y=a[(j+1)%3]; const auto edge=std::minmax(x,y);
                ++count[edge]; orientation[edge]+=x<y?1:-1;
            }
        }
        if (closed) for (const auto& [edge,n]:count) { assert(n==2 && orientation[edge]==0); }
    };
    check(mesh.skin,mesh.skinFaces,true);
    check(mesh.nails,mesh.nailFaces,false);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    for (bool male:{true,false}) {
        int profiles=0;
        const auto check=[&](SkeletonComparison::Profile p) {
            assert(room.setComparison(male,p,error)); audit(room.comparison(male)); ++profiles;
        };
        check(room.comparison(male).profile());
        for (double h:{50.,170.,250.}) {
            auto base=room.comparison(male).profile(); base.height=h; base.multiplier.fill(1); check(base);
            for (int d=SkeletonComparison::Head;d<SkeletonComparison::Count;++d) {
                const auto limits=room.comparison(male).dimensionLimits(d,h); assert(limits);
                for (double value:{limits->minimum,limits->maximum}) {
                    auto p=base; p.multiplier[d]=value/limits->center; check(p);
                }
            }
            for (bool upper:{false,true}) {
                auto p=base;
                for (int d=SkeletonComparison::Head;d<SkeletonComparison::Count;++d) {
                    const auto limits=room.comparison(male).dimensionLimits(d,h); assert(limits);
                    p.multiplier[d]=(upper?limits->maximum:limits->minimum)/limits->center;
                }
                check(p);
            }
        }
        assert(profiles==52);
        std::printf("%s: toe surface symmetry, winding, topology and floor in %d profiles: PASS\n",male?"male":"female",profiles);
        room.resetComparison(male);
    }
}
