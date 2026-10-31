// Audit motion of actual blue mesh vertices as the height control is dragged.
// Sweep six axis-extreme vertices per structure at 0.1 cm, then every mesh
// vertex at 0.5 cm. Neither sweep proves collision freedom or clinical validity.
// Units in the rate checks are cm per cm stature.
#include "client/skeleton_room.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace gtc::native;

struct Sample {
    const SkeletonComparison::Part* part;
    Vec3 source, previous{}, beforePrevious{};
};

int main() {
    std::string error; SkeletonRoom room; assert(room.load(".",error));
    for (bool male:{false,true}) for (bool allVertices:{false,true}) {
        const auto& comparison=room.comparison(male);
        std::vector<Sample> samples;
        for (const auto& part:comparison.parts()) {
            assert(!part.points.empty());
            if (allVertices) {
                for (const Vec3 p:part.points) samples.push_back({&part,p});
            } else for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
                auto coordinate=[axis](Vec3 p) { return axis==0 ? p.x : axis==1 ? p.y : p.z; };
                const auto extreme=std::max_element(part.points.begin(),part.points.end(),
                    [&](Vec3 a,Vec3 b) { return sign*coordinate(a)<sign*coordinate(b); });
                samples.push_back({&part,*extreme});
            }
        }
        auto profile=comparison.profile();
        double peakSpeed=0, peakAcceleration=0, peakHeight=0;
        std::string peakPart;
        const double step=allVertices ? 0.5 : 0.1;
        const int steps=static_cast<int>(200/step);
        for (int i=0;i<=steps;++i) {
            profile.height=50+i*step;
            assert(room.setComparison(male,profile,error));
            for (auto& sample:samples) {
                const auto& part=*sample.part;
                const Vec3 p=comparison.map(sample.source,part.armSide,part.cranial,part.girdle,part.humerus);
                if (i) peakSpeed=std::max(peakSpeed,length(p-sample.previous)*100/step);
                if (i>=2) {
                    const double acceleration=length(p-sample.previous*2+sample.beforePrevious)*100/(step*step);
                    if (acceleration>peakAcceleration) {
                        peakAcceleration=acceleration; peakHeight=profile.height; peakPart=part.name;
                    }
                }
                sample.beforePrevious=sample.previous; sample.previous=p;
            }
        }
        // Check the actual mesh response, not just continuity of the source
        // formulas. The supplied male exponential shoulder/hip equations have
        // positive curvature, especially at 250 cm; unlike a quadratic fit,
        // that curvature cannot be removed without changing those equations.
        assert(peakSpeed<1.25 && peakAcceleration<(male?0.006:0.005));
        std::printf("%s %zu %s vertices, %.1f cm steps: peak speed %.4f, rate change %.4f (%s at %.1f cm)\n",
                    male?"male":"female",samples.size(),allVertices?"all":"axis-extreme",step,
                    peakSpeed,peakAcceleration,peakPart.c_str(),peakHeight);
    }
    std::puts("All-body height-transition mesh sweep: PASS");
}
