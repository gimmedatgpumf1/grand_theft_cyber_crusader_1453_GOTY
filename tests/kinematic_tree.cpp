// Exercise the reusable open-chain solver against independently computed
// OpenSim 4.6 AAH reference finger poses. Not subject-fitted or a ROM test.
#include "client/skeleton_kinematic_tree.h"
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

static JointRigidTransform frame(std::istringstream& in) {
    JointRigidTransform f;
    for (auto& v : f.rotation) in >> v;
    in >> f.translation.x >> f.translation.y >> f.translation.z;
    assert(in); return f;
}
static bool near(const JointRigidTransform& a, const JointRigidTransform& b) {
    for (int i = 0; i < 9; ++i) if (std::abs(a.rotation[i]-b.rotation[i]) > 1e-9) return false;
    return length(a.translation-b.translation) < 1e-9;
}
static std::map<std::string, CalibratedRotationalJoint> loadJoints() {
    std::ifstream in("tests/aah_hand_joint_cases.txt"); assert(in);
    std::map<std::string, CalibratedRotationalJoint> joints;
    JointRigidTransform parent, child;
    std::vector<JointRotationalAxis> axes;
    std::string line, name;
    while (std::getline(in,line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line); std::string type; row >> type;
        if (type == "joint") { row >> name; axes.clear(); }
        else if (type == "parent") parent = frame(row);
        else if (type == "child") child = frame(row);
        else if (type == "axis") {
            JointRotationalAxis a;
            row >> a.direction.x >> a.direction.y >> a.direction.z >> a.minimum >> a.maximum;
            assert(row); axes.push_back(a);
        } else if (type == "end") {
            auto j = CalibratedRotationalJoint::create(parent, child, axes);
            assert(j && joints.emplace(name, *j).second);
        }
    }
    assert(joints.size() == 32); return joints;
}
int main() {
    // Three anatomical hip coordinates plus a separate reference alignment.
    std::vector<JointRotationalAxis> hipAxes{{{1,0,0},-1,2},{{0,1,0},-1,1},{{0,0,1},-1,1},{{1,0,0},0,3}};
    auto hip=CalibratedRotationalJoint::create({}, {}, hipAxes); assert(hip);
    assert(hip->setAngles({0.1,0.2,0.3,0.4}) && joint_detail::rigid(hip->childInParent()));
    hipAxes.push_back({{1,0,0},0,1}); assert(!CalibratedRotationalJoint::create({}, {}, hipAxes));
    auto joints = loadJoints();
    std::ifstream in("tests/aah_finger_chain_cases.txt"); assert(in);
    std::string line, name;
    int chains = 0, poses = 0;
    AnatomicalKinematicTree tree;
    std::vector<std::size_t> bodies;
    while (std::getline(in,line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line); std::string type; row >> type;
        if (type == "chain") {
            tree = AnatomicalKinematicTree{};
            bodies.clear();
            int count; row >> count;
            std::size_t parent = 0;
            for (int i = 0; i < count; ++i) {
                row >> name; auto found = joints.find(name); assert(found != joints.end());
                auto body = tree.addJoint(parent, found->second); assert(body);
                bodies.push_back(*body); parent = *body;
            }
            tree.captureBindPose();
            assert(tree.evaluate());
            for (const auto& transform : tree.skinTransforms()) assert(near(transform, {}));
            ++chains;
        } else if (type == "pose") {
            std::vector<AnatomicalKinematicTree::PoseUpdate> update;
            for (auto body : bodies) {
                std::vector<double> q(tree.joint(body)->angles().size());
                for (auto& value : q) row >> value;
                update.push_back({body, q});
            }
            const auto expected = frame(row);
            assert(tree.setPose(update) && tree.evaluate());
            if (!near(tree.absoluteTransforms()[bodies.back()], expected)) {
                std::cerr << "OpenSim kinematic tree mismatch: sample " << poses << '\n'; return 1;
            }
            JointRigidTransform movedRoot;
            movedRoot.rotation = {0,-1,0, 1,0,0, 0,0,1};
            movedRoot.translation = {2,3,4};
            assert(tree.evaluate(movedRoot));
            assert(near(tree.absoluteTransforms()[bodies.back()], joint_detail::compose(movedRoot, expected)));
            for (const auto& skin : tree.skinTransforms()) assert(joint_detail::rigid(skin));
            const auto previous = tree.joint(bodies[0])->angles();
            auto invalid = update; invalid[0].angles[0] = std::numeric_limits<double>::quiet_NaN();
            assert(!tree.setPose(invalid) && tree.joint(bodies[0])->angles() == previous);
            invalid = update; invalid.push_back(invalid[0]);
            assert(!tree.setPose(invalid) && tree.joint(bodies[0])->angles() == previous);
            ++poses;
        }
    }
    assert(chains == 10 && poses == 50);
    AnatomicalKinematicTree branch;
    assert(!branch.evaluate());
    assert(!branch.addFixed(9, {}));
    JointRigidTransform fixed; fixed.translation = {0.1,0.2,0.3};
    const auto left = branch.addFixed(0, fixed), right = branch.addFixed(0, {});
    assert(left && right);
    const auto found = joints.begin();
    auto child = branch.addJoint(*left, found->second); assert(child);
    branch.captureBindPose(); assert(branch.evaluate());
    auto before = branch.skinTransforms()[*right];
    std::vector<double> changed(branch.joint(*child)->angles().size(), 100.0);
    assert(branch.setPose({{*child, changed}}) && branch.evaluate());
    assert(near(before, branch.skinTransforms()[*right]));
    for (auto q : branch.joint(*child)->angles()) assert(q < 100.0);
    JointRigidTransform invalid; invalid.rotation[0] = -1;
    assert(!branch.evaluate(invalid) && !branch.addFixed(0, invalid));
    const auto added = branch.addFixed(*right, {}); assert(added && !branch.hasBindPose());
    assert(!branch.evaluate());
    std::cout << "AAH open-chain tree: PASS (10 finger chains, 50 OpenSim poses; not subject fitted)\n";
}
