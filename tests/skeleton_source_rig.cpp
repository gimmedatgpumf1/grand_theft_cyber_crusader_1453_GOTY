// Pipeline test with synthetic joint data. The joint is deliberately NOT a
// fitted human joint; this exercises attachment and GPU-ready matrices only.
#include "client/skeleton_body.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace gtc::native;

static void verify(const char* sex) {
    SkeletonModel model; std::string error;
    assert(model.load(std::string("tools/skeleton-source/skeleton-")+sex+".obj", error));
    assert(model.sourceStructures().size() == 182);
    const auto original = model.sourceMatrices({1,2,3}, 0.2);
    const auto head = static_cast<std::size_t>(model.sourceStructure("Head"));
    AnatomicalKinematicTree tree;
    JointRigidTransform frame; frame.translation = {0.1, 1.4, 0.0};
    auto joint = CalibratedRotationalJoint::create(frame, frame, {{{0,1,0}, -0.4, 0.4}});
    assert(joint);
    std::vector<std::size_t> mapping;
    for (std::size_t i = 0; i < model.sourceStructures().size(); ++i) {
        auto body = i == head ? tree.addJoint(0, *joint) : tree.addFixed(0, {});
        assert(body); mapping.push_back(*body);
    }
    auto incomplete = mapping; incomplete.pop_back();
    tree.captureBindPose();
    assert(!model.setSourceRig(tree, incomplete, error));
    auto duplicate = mapping; duplicate[0] = duplicate[1];
    assert(!model.setSourceRig(tree, duplicate, error));
    assert(model.sourceMatrices({1,2,3}, 0.2) == original);
    assert(model.setSourceRig(tree, mapping, error));
    const auto bind = model.sourceMatrices({1,2,3}, 0.2);
    assert(bind.size() == original.size());
    for (std::size_t i = 0; i < bind.size(); ++i)
        for (int k = 0; k < 16; ++k) assert(std::abs(bind[i][k]-original[i][k]) < 1e-12);
    const auto version = model.sourcePoseRevision();
    assert(!model.setSourcePose({{mapping[head], {std::numeric_limits<double>::quiet_NaN()}}}));
    assert(model.sourcePoseRevision() == version);
    assert(model.setSourcePose({{mapping[head], {99}}}));
    assert(model.sourcePoseRevision() > version);
    const auto moved = model.sourceMatrices({1,2,3}, 0.2);
    assert(moved[head] != bind[head]);
    for (std::size_t i = 0; i < moved.size(); ++i) if (i != head) assert(moved[i] == bind[i]);
    const auto rotated = std::acos(std::clamp(moved[head][0]*std::cos(0.2)-moved[head][2]*std::sin(0.2), -1., 1.));
    assert(std::abs(rotated-0.4) < 1e-9); // bounded by the supplied model
    assert(model.setSourcePose({{mapping[head], {0}}}));
    const auto restored = model.sourceMatrices({1,2,3}, 0.2);
    for (std::size_t i = 0; i < bind.size(); ++i)
        for (int k = 0; k < 16; ++k) assert(std::abs(bind[i][k]-restored[i][k]) < 1e-12);
    // Re-loading cannot leave the previous subject's rig attached.
    assert(model.load(std::string("tools/skeleton-source/skeleton-")+sex+".obj", error));
    assert(!model.setSourcePose({{mapping[head], {0.2}}}));
}
int main() {
    verify("male"); verify("female");
    std::cout << "Source rig plumbing: PASS (synthetic joint, both bodies; not anatomical calibration)\n";
}
