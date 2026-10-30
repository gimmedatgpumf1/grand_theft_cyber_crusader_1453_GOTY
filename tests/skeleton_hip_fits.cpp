// Check the real room loader's fitted hip pivots and their rigid propagation.
// Joint axes and limits are not yet calibrated; this is not a ROM test.
#include "client/skeleton_body.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace gtc::native;

static Vec3 apply(const std::array<double, 16>& m, Vec3 v) {
    return {m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],
            m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],
            m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]};
}
static void verify(const char* sex) {
    SkeletonModel model;
    std::string error;
    const std::string base = std::string("tools/skeleton-source/skeleton-") + sex;
    assert(model.load(base+".obj", error));
    assert(model.loadHipCenters(base+".hips", error));
    std::ifstream file(base+".hips");
    std::string line;
    int checked = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        char side; Vec3 center; double radius;
        assert(std::sscanf(line.c_str(), "%c %lf %lf %lf %lf", &side, &center.x, &center.y, &center.z, &radius) == 5);
        const std::string suffix = side == 'L' ? "_l" : "_r";
        const int femur = model.bone("femur"+suffix), shin = model.bone("lower_leg"+suffix), foot = model.bone("foot"+suffix);
        assert(femur >= 0 && shin >= 0 && foot >= 0);
        assert(length(model.bones()[femur].pivot-center) < 1e-12);
        // A nonzero test rotation uses the fitted centre as its rigid pivot;
        // this numerical exercise does not assert anatomical ROM.
        model.bones()[femur].pitch = 0.25;
        const auto matrices = model.boneMatrices({}, 0);
        assert(length(apply(matrices[femur], center)-center) < 1e-12);
        assert(matrices[femur] == matrices[shin] && matrices[femur] == matrices[foot]);
        const int source = model.sourceStructure(std::string("Femur_")+side);
        assert(source >= 0 && model.sourceMatrices({}, 0)[source] == matrices[femur]);
        model.bones()[femur].pitch = 0;
        ++checked;
    }
    assert(checked == 2);
    const Vec3 beforeL = model.bones()[model.bone("femur_l")].pivot;
    const Vec3 beforeR = model.bones()[model.bone("femur_r")].pivot;
    { std::ofstream bad("build/bad-hip-fits.txt"); bad << "L 0 0 0 0.02\nR 0 0 0 0.02\n"; }
    assert(!model.loadHipCenters("build/bad-hip-fits.txt", error));
    assert(length(model.bones()[model.bone("femur_l")].pivot-beforeL) == 0);
    assert(length(model.bones()[model.bone("femur_r")].pivot-beforeR) == 0);
    std::remove("build/bad-hip-fits.txt");
}
int main() {
    verify("male"); verify("female");
    std::puts("CT-fitted hip pivots: PASS (both bodies; no joint-ROM claim)");
}
