// Comparison against OpenSim for the published reference gait model's knee.
// This verifies math, not the anatomy or motion of the two CT subjects.
#include "client/skeleton_kinematic_tree.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
using namespace gtc::native;

static JointRigidTransform frame(std::istream& in) {
    JointRigidTransform t;
    for (auto& v : t.rotation) in >> v;
    in >> t.translation.x >> t.translation.y >> t.translation.z;
    assert(in && joint_detail::rigid(t)); return t;
}
static void compare(const JointRigidTransform& a, const JointRigidTransform& b) {
    double largest = length(a.translation-b.translation);
    for (int i = 0; i < 9; ++i) largest = std::max(largest, std::abs(a.rotation[i]-b.rotation[i]));
    if (largest > 2e-7) std::cerr << "OpenSim transform discrepancy " << largest << '\n';
    assert(largest < 2e-7);
}
int main() {
    std::ifstream in("tests/rajagopal_knee_cases.txt"); assert(in);
    std::string line; std::getline(in,line);
    int joints = 0, samples = 0;
    std::string token;
    while (in >> token) {
        assert(token == "joint"); std::string side; double lo, hi; in >> side >> lo >> hi;
        in >> token; assert(token == "parent"); auto parent = frame(in);
        in >> token; assert(token == "child"); auto child = frame(in);
        std::array<SpatialJointAxis,3> r, t;
        for (int i=0; i<6; ++i) {
            in >> token; assert(token == "axis"); SpatialJointAxis a; int n;
            in >> a.direction.x >> a.direction.y >> a.direction.z >> n >> a.constant;
            if (n) {
                std::vector<CouplingKnot> knots(n);
                for (auto& k : knots) { in >> token; assert(token == "knot");
                    in >> k.x >> k.y >> k.slope >> k.quadratic >> k.cubic; }
                a.function = CalibratedJointCoupling::create(knots); assert(a.function);
            }
            if (i<3) r[i]=a; else t[i-3]=a;
        }
        auto joint = CalibratedSpatialJoint::create(parent,child,lo,hi,r,t); assert(joint);
        AnatomicalKinematicTree tree; auto body = tree.addSpatialJoint(0,*joint); assert(body);
        tree.captureBindPose(); assert(tree.evaluate());
        const auto bind = tree.skinTransforms()[*body];
        compare(bind,{});
        while (in >> token && token != "end") {
            assert(token == "sample"); double q; in >> q; const auto expected=frame(in);
            assert(joint->setAngle(q)); compare(joint->childInParent(),expected);
            assert(tree.setPose({{*body,{q}}}) && tree.evaluate());
            compare(tree.absoluteTransforms()[*body],expected);
            ++samples;
        }
        assert(joint->setAngle(1e5) && joint->angle() == hi);
        assert(joint->setAngle(-1e5) && joint->angle() == lo);
        assert(!joint->setAngle(std::numeric_limits<double>::quiet_NaN()));
        assert(!tree.setPose({{*body,{0.1, 0.2}}}));
        assert(!tree.setPose({{*body,{0.1}}, {*body,{0.2}}}));
        assert(tree.setPose({{*body,{lo}}}) && tree.evaluate());
        compare(tree.skinTransforms()[*body],{});
        ++joints;
    }
    assert(joints == 2 && samples == 22);
    std::cout << "OpenSim knee spatial transforms: PASS (22 poses, 2 reference knees; not subject-fitted)\n";
}
