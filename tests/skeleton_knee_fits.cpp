// CT atlas-derived femoral epicondylar line in the real source model.
// These tests do not establish knee contact, neutral angle, coupling or ROM.
#include "client/skeleton_body.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
using namespace gtc::native;

static Vec3 apply(const std::array<double, 16>& m, Vec3 v) {
    return {m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],
            m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],
            m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]};
}
static void verify(const char* sex) {
    SkeletonModel model; std::string error;
    const std::string base = std::string("tools/skeleton-source/skeleton-") + sex;
    assert(model.load(base+".obj", error));
    assert(!model.hasKneeAxes());
    const auto original = model.sourceMatrices({}, 0);
    assert(model.loadKneeAxes(base+".knees", error) && model.hasKneeAxes());
    assert(model.sourceMatrices({}, 0) == original); // loading does not repose or scale any bone
    std::ifstream input(base+".knees"); assert(input);
    std::string line; int count = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        char side; Vec3 center, axis; double width;
        assert(std::sscanf(line.c_str(), "%c %lf %lf %lf %lf %lf %lf %lf", &side,
                           &center.x,&center.y,&center.z,&axis.x,&axis.y,&axis.z,&width) == 8);
        const int which = side == 'L' ? 0 : 1;
        const std::string suffix = side == 'L' ? "_l" : "_r";
        const int shin = model.bone("lower_leg"+suffix), foot = model.bone("foot"+suffix);
        assert(shin >= 0 && foot >= 0);
        const auto& estimate = model.kneeAxes()[which];
        assert(length(estimate.origin-center) < 1e-12 && length(estimate.medialDirection-axis) < 1e-12);
        assert(std::abs(estimate.width-width) < 1e-12);
        assert(length(model.bones()[shin].pivot-center) < 1e-12 && model.bones()[shin].hasFittedBendAxis);
        // Small rigid test motion only: the femur-derived anatomical line stays
        // fixed and the unrotated child foot inherits the same transform.
        model.bones()[shin].pitch = 0.2;
        const auto pose = model.boneMatrices({}, 0);
        assert(length(apply(pose[shin], center)-center) < 1e-12);
        assert(length(apply(pose[shin], center+axis)-(center+axis)) < 2e-9);
        assert(pose[shin] == pose[foot]);
        model.bones()[shin].pitch = 0;
        ++count;
    }
    assert(count == 2);
    const auto before = model.kneeAxes();
    { std::ofstream bad("build/bad-knee-fits.txt"); bad << "L 0 0 0 1 0 0 0.08\nR 0 0 0 1 0 0 0.08\n"; }
    assert(!model.loadKneeAxes("build/bad-knee-fits.txt", error));
    assert(model.hasKneeAxes() && length(model.kneeAxes()[0].origin-before[0].origin) == 0);
    assert(length(model.kneeAxes()[1].origin-before[1].origin) == 0);
    std::remove("build/bad-knee-fits.txt");
}
int main() {
    verify("male"); verify("female");
    std::puts("CT-fitted femoral knee axes: PASS (rigid propagation; no ROM claim)");
}
