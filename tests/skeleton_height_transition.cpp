// Audit motion of actual blue mesh vertices as the height control is dragged.
// This samples six axis-extreme vertices per structure, not every triangle or
// a proof of clinical validity. Units in the rate checks are cm per cm stature.
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
    for (bool male:{false,true}) {
        const auto& comparison=room.comparison(male);
        std::vector<Sample> samples;
        for (const auto& part:comparison.parts()) {
            assert(!part.points.empty());
            for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) {
                auto coordinate=[axis](Vec3 p) { return axis==0 ? p.x : axis==1 ? p.y : p.z; };
                const auto extreme=std::max_element(part.points.begin(),part.points.end(),
                    [&](Vec3 a,Vec3 b) { return sign*coordinate(a)<sign*coordinate(b); });
                samples.push_back({&part,*extreme});
            }
        }
        auto profile=comparison.profile();
        double peakSpeed=0, peakAcceleration=0;
        constexpr double step=0.1;
        for (int i=0;i<=2000;++i) {
            profile.height=50+i*step;
            assert(room.setComparison(male,profile,error));
            for (auto& sample:samples) {
                const auto& part=*sample.part;
                const Vec3 p=comparison.map(sample.source,part.armSide,part.cranial,part.girdle,part.humerus);
                if (i) peakSpeed=std::max(peakSpeed,length(p-sample.previous)*100/step);
                if (i>=2) peakAcceleration=std::max(peakAcceleration,
                    length(p-sample.previous*2+sample.beforePrevious)*100/(step*step));
                sample.beforePrevious=sample.previous; sample.previous=p;
            }
        }
        // Regression limits: the old 15 cm shoulder crossfade reached 0.115
        // female / 0.063 male cm per cm² on upper-arm/forearm samples.
        assert(peakSpeed<1.25 && peakAcceleration<0.05);
        std::printf("%s %zu sampled mesh vertices: peak speed %.4f, rate change %.4f\n",
                    male?"male":"female",samples.size(),peakSpeed,peakAcceleration);
    }
    std::puts("All-body height-transition mesh sweep: PASS");
}
