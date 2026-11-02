// The male blue sacrum must end in one centered coccyx, not two mirrored tips.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <utility>
#include <vector>
using namespace gtc::native;

// Count connected cross-section contours by joining intersected mesh edges.
// Sampling strictly between vertex heights avoids ambiguous vertex cuts.
static int contours(const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces,double y) {
    using Edge=std::pair<int,int>;
    std::map<Edge,int> nodes; std::vector<int> parent;
    const auto node=[&](int a,int b) {
        Edge edge=std::minmax(a,b); auto [it,inserted]=nodes.emplace(edge,int(nodes.size()));
        if (inserted) parent.push_back(it->second);
        return it->second;
    };
    const auto root=[&](int x) { while(parent[x]!=x) x=parent[x]; return x; };
    for (auto face:faces) {
        int crossed[3],count=0;
        for (int i=0;i<3;++i) {
            const int a=face[i],b=face[(i+1)%3];
            if ((points[a].y<y)!=(points[b].y<y)) crossed[count++]=node(a,b);
        }
        assert(count==0 || count==2);
        if (count==2) parent[root(crossed[0])]=root(crossed[1]);
    }
    int count=0; for (int i=0;i<int(parent.size());++i) if (root(i)==i) ++count;
    return count;
}
static void audit(const SkeletonComparison& copy) {
    auto it=std::find_if(copy.parts().begin(),copy.parts().end(),[](const auto& p){return p.name=="Sacrum";});
    assert(it!=copy.parts().end()); const auto& part=*it; std::vector<Vec3> points;
    for (Vec3 p:part.points) points.push_back(copy.mapPart(p,part));
    const auto low=std::min_element(points.begin(),points.end(),[](Vec3 a,Vec3 b){return a.y<b.y;});
    const auto high=std::max_element(points.begin(),points.end(),[](Vec3 a,Vec3 b){return a.y<b.y;});
    const double length=high->y-low->y; assert(length>0);
    if (std::abs(low->x)>1e-8) {
        std::fprintf(stderr,"Coccyx tip is %.3f mm off the mirror plane at height %.1f cm\n",low->x*1000,copy.profile().height);
        std::abort();
    }
    for (double f:{.007,.015,.025,.035,.045,.060,.080}) {
        const int n=contours(points,part.faces,low->y+length*f);
        if (n!=1) {
            std::fprintf(stderr,"Coccyx has %d cross-section contours at %.1f%%, height %.1f cm\n",n,100*f,copy.profile().height);
            std::abort();
        }
    }
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    auto& copy=room.comparison(true); int profiles=0;
    const auto check=[&](SkeletonComparison::Profile p){assert(room.setComparison(true,p,error));audit(copy);++profiles;};
    check(copy.profile());
    for (double height:{50.,170.,250.}) {
        auto base=copy.profile(); base.height=height; base.multiplier.fill(1); check(base);
        for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
            auto bounds=copy.dimensionLimits(dim,height); assert(bounds);
            for (double endpoint:{bounds->minimum,bounds->maximum}) {
                auto p=base; p.multiplier[dim]=endpoint/bounds->center; check(p);
            }
        }
        for (bool upper:{false,true}) {
            auto p=base;
            for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
                auto bounds=copy.dimensionLimits(dim,height); assert(bounds);
                p.multiplier[dim]=(upper?bounds->maximum:bounds->minimum)/bounds->center;
            }
            check(p);
        }
    }
    assert(profiles==52); std::puts("Male blue coccyx: one centered tip and one contour at seven levels, 52 profiles: PASS");
}
