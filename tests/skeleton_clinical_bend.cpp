// Runtime application of simple reference angles on an intentionally synthetic
// rig. The actual subject geometry supplies the pivot, not a measured neutral.
#include "client/skeleton_body.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
using namespace gtc::native;

static void verify(const char* sex) {
    const std::string base=std::string("tools/skeleton-source/skeleton-")+sex;
    SkeletonModel model; std::string error;
    assert(model.load(base+".obj", error));
    assert(model.loadSourceLinks("tools/skeleton-source/skeleton-joint-links.txt", error));
    assert(model.loadKneeAxes(base+".knees", error));
    std::size_t link=0;
    while (link < model.sourceLinks().size() &&
           model.sourceStructures()[model.sourceLinks()[link].second].name != "Tibia_L") ++link;
    assert(link < model.sourceLinks().size());
    assert(!model.setReferenceBend(link, 20)); // no calibrated source rig installed
    const auto tibia = model.sourceLinks()[link].second;
    JointRigidTransform frame; frame.translation = model.kneeAxes()[0].origin;
    auto joint = CalibratedRotationalJoint::create(frame,frame,{{model.kneeAxes()[0].medialDirection,-4,4}});
    assert(joint);
    AnatomicalKinematicTree tree; std::vector<std::size_t> bodies(model.sourceStructures().size());
    const auto femur = model.sourceLinks()[link].first;
    auto parent = tree.addFixed(0, {}); assert(parent); bodies[femur] = *parent;
    auto hinge = tree.addJoint(*parent,*joint); assert(hinge); bodies[tibia] = *hinge;
    for (std::size_t i=0; i<model.sourceStructures().size(); ++i) {
        if (i == femur || i == tibia) continue;
        auto node = tree.addFixed(0,{}); assert(node); bodies[i] = *node;
    }
    tree.captureBindPose(); assert(model.setSourceRig(tree,bodies,error));
    const auto bind=model.sourceMatrices({},0);
    const auto version=model.sourcePoseRevision();
    assert(!model.setReferenceBend(link,std::numeric_limits<double>::quiet_NaN()));
    assert(model.sourcePoseRevision()==version);
    assert(model.setReferenceBend(link,200)); // knee reference is 0 to 135 degrees
    const auto changed=model.sourceMatrices({},0);
    assert(changed[tibia] != bind[tibia]);
    const double trace=changed[tibia][0]+changed[tibia][5]+changed[tibia][10];
    const double radians=std::acos(std::clamp((trace-1)/2,-1.,1.));
    assert(std::abs(radians-135.0*3.14159265358979323846/180.0) < 1e-8);
    assert(model.setReferenceBend(link,-10));
    const auto restored=model.sourceMatrices({},0);
    for (std::size_t i=0; i<bind.size(); ++i) for (int j=0; j<16; ++j)
        assert(std::abs(bind[i][j]-restored[i][j]) < 1e-12);
    // A biological graph edge must not drive a joint attached elsewhere.
    AnatomicalKinematicTree disconnected; std::vector<std::size_t> wrong;
    for (std::size_t i=0; i<model.sourceStructures().size(); ++i) {
        auto node = i==tibia ? disconnected.addJoint(0,*joint) : disconnected.addFixed(0,{});
        assert(node); wrong.push_back(*node);
    }
    disconnected.captureBindPose(); assert(model.setSourceRig(disconnected,wrong,error));
    const auto revision = model.sourcePoseRevision();
    assert(!model.setReferenceBend(link,20));
    assert(model.sourcePoseRevision() == revision);
}
int main() {
    verify("male"); verify("female");
    std::cout << "Clinical reference bend: PASS (real source batches, synthetic neutral; not subject ROM)\n";
}
