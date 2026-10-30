// Real reference-model joint motions, not a test of the OpenSkeleton subjects.
// Expected transforms were independently computed with OpenSim 4.6 from the
// published, MIT-licensed AAH left/right hand models (see fixture provenance).
#include "client/skeleton_joint.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace gtc::native;

static JointRigidTransform readFrame(std::istringstream& in) {
    JointRigidTransform frame;
    for (double& v : frame.rotation) in >> v;
    in >> frame.translation.x >> frame.translation.y >> frame.translation.z;
    assert(in);
    return frame;
}
static bool near(JointRigidTransform a, JointRigidTransform b) {
    for (int i = 0; i < 9; ++i) if (std::abs(a.rotation[i] - b.rotation[i]) > 1e-9) return false;
    return length(a.translation - b.translation) < 1e-9;
}
int main() {
    std::ifstream file("tests/aah_hand_joint_cases.txt");
    assert(file);
    std::string line, name;
    int n = 0, joints = 0, samples = 0;
    std::vector<JointRotationalAxis> axes;
    JointRigidTransform parent, child;
    std::optional<CalibratedRotationalJoint> joint;
    std::map<std::string, CalibratedRotationalJoint> models;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string type; in >> type;
        if (type == "joint") {
            assert(!joint); in >> name >> n;
            assert(n == 1 || n == 2);
            axes.clear(); ++joints;
        } else if (type == "parent") parent = readFrame(in);
        else if (type == "child") child = readFrame(in);
        else if (type == "axis") {
            JointRotationalAxis axis;
            in >> axis.direction.x >> axis.direction.y >> axis.direction.z >> axis.minimum >> axis.maximum;
            assert(in); axes.push_back(axis);
        } else if (type == "sample") {
            if (!joint) { assert(static_cast<int>(axes.size()) == n); joint = CalibratedRotationalJoint::create(parent, child, axes); assert(joint); }
            std::vector<double> q(n);
            for (double& v : q) in >> v;
            const auto expected = readFrame(in);
            assert(joint->setAngles(q));
            if (!near(joint->childInParent(), expected)) {
                std::cerr << "OpenSim transform mismatch: " << name << " sample " << samples << '\n';
                return 1;
            }
            ++samples;
        } else if (type == "end") {
            assert(joint);
            std::vector<double> below(n), above(n);
            for (int i = 0; i < n; ++i) { below[i] = axes[i].minimum - 10; above[i] = axes[i].maximum + 10; }
            assert(joint->setAngles(below));
            for (int i = 0; i < n; ++i) assert(joint->angles()[i] == axes[i].minimum);
            assert(joint->setAngles(above));
            for (int i = 0; i < n; ++i) assert(joint->angles()[i] == axes[i].maximum);
            const auto previous = joint->angles();
            above[0] = std::numeric_limits<double>::quiet_NaN();
            assert(!joint->setAngles(above) && previous == joint->angles());
            assert(!joint->setAngles({}) && previous == joint->angles());
            assert(models.emplace(name, *joint).second);
            joint.reset();
        } else { assert(false); }
    }
    assert(joints == 32 && samples == 160 && !joint);
    std::ifstream chains("tests/aah_finger_chain_cases.txt");
    assert(chains);
    std::vector<CalibratedRotationalJoint> chain;
    int chainCount = 0, chainSamples = 0;
    while (std::getline(chains, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string type; in >> type;
        if (type == "chain") {
            assert(chain.empty());
            int size;
            in >> size;
            assert(size == 3 || size == 4);
            for (int i = 0; i < size; ++i) {
                in >> name;
                const auto found = models.find(name);
                assert(found != models.end());
                chain.push_back(found->second);
            }
            ++chainCount;
        } else if (type == "pose") {
            assert(chain.size() == 3 || chain.size() == 4);
            for (auto& member : chain) {
                std::vector<double> q(member.angles().size());
                for (double& v : q) in >> v;
                assert(member.setAngles(q));
            }
            const auto expected = readFrame(in);
            JointRigidTransform transform;
            for (const auto& member : chain) transform = joint_detail::compose(transform, member.childInParent());
            if (!near(transform, expected)) {
                std::cerr << "OpenSim whole-finger chain mismatch: " << chainSamples << '\n';
                return 1;
            }
            ++chainSamples;
        } else if (type == "end") chain.clear();
        else assert(false);
    }
    assert(chain.empty() && chainCount == 10 && chainSamples == 50);
    JointRigidTransform identity;
    assert(!CalibratedRotationalJoint::create(identity, identity, {}));
    assert(!CalibratedRotationalJoint::create(identity, identity, {{{0,0,0}, 0, 1}}));
    assert(!CalibratedRotationalJoint::create(identity, identity, {{{1,0,0}, 2, 1}}));
    assert(!CalibratedRotationalJoint::create(identity, identity, {{{1,0,0}, 0, std::numeric_limits<double>::infinity()}}));
    identity.rotation[0] = -1; // reflection, not a rigid orientation
    assert(!CalibratedRotationalJoint::create(identity, {}, {{{1,0,0}, 0, 1}}));
    std::cout << "AAH reference-hand rotational joint solver: PASS (32 joints, 160 poses, 50 whole-finger poses; not fitted to OpenSkeleton)\n";
}
