// Real Vulkan frame-path test of the source-rig plumbing. Synthetic hinge only:
// this does NOT claim that either subject has a medically fitted joint.
#include "client/skeleton_room.h"
#include "vulkan/vk_renderer.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace gtc::native;

static std::vector<unsigned char> pixels(SkeletonRoom& room, ViewState view) {
    NativeFrame frame; frame.wantPixels = true;
    room.render(frame, view, 0);
    assert(frame.width == view.width && frame.height == view.height);
    const auto n = static_cast<std::size_t>(view.width)*view.height*4;
    const auto* p = frame.direct ? frame.direct : frame.bgra.data();
    assert(p && (frame.direct || frame.bgra.size() >= n));
    return {p, p+n};
}
static int differences(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    assert(a.size() == b.size());
    int n = 0;
    for (std::size_t i=0; i<a.size(); i+=4) {
        int diff=0; for (int j=0; j<3; ++j) diff += std::abs(int(a[i+j])-int(b[i+j]));
        if (diff > 24) ++n;
    }
    return n;
}
static void run(SkeletonRoom& room, ViewState view, SkeletonModel& model) {
    const auto original = pixels(room, view);
    const auto head = static_cast<std::size_t>(model.sourceStructure("Head"));
    const auto& part = model.sourceStructures()[head];
    const auto& group = model.bones()[part.group];
    Vec3 center{};
    for (std::size_t i=part.firstPoint; i<part.firstPoint+part.pointCount; ++i) center = center + group.points[i];
    center = center / static_cast<double>(part.pointCount);
    JointRigidTransform frame; frame.translation = center;
    auto joint = CalibratedRotationalJoint::create(frame,frame,{{{0,1,0},-1,1}}); assert(joint);
    AnatomicalKinematicTree rig; std::vector<std::size_t> bodies;
    for (std::size_t i=0; i<model.sourceStructures().size(); ++i) {
        auto body = i == head ? rig.addJoint(0, *joint) : rig.addFixed(0, {});
        assert(body); bodies.push_back(*body);
    }
    rig.captureBindPose(); std::string error;
    assert(model.setSourceRig(rig, bodies, error));
    const auto bind = pixels(room, view);
    assert(differences(original,bind) == 0);
    assert(model.setSourcePose({{bodies[head], {0.8}}}));
    const auto moved = pixels(room, view);
    const int changed = differences(bind,moved);
    std::printf("Vulkan synthetic source joint: %d changed pixels\n", changed);
    assert(changed > 20);
    assert(model.setSourcePose({{bodies[head], {0}}}));
    const auto restored = pixels(room, view);
    assert(differences(bind,restored) == 0);
}
// A reference bend routed through the actual source-batch GPU path. The
// neutral frame is deliberately synthetic and does not validate subject ROM.
static void clinicalBend(SkeletonRoom& room, ViewState view, SkeletonModel& model) {
    const auto before = pixels(room, view);
    const auto tibia = static_cast<std::size_t>(model.sourceStructure("Tibia_L"));
    std::size_t link = 0;
    while (link < model.sourceLinks().size() && model.sourceLinks()[link].second != tibia) ++link;
    assert(link < model.sourceLinks().size());
    JointRigidTransform frame; frame.translation = model.kneeAxes()[0].origin;
    auto joint = CalibratedRotationalJoint::create(frame, frame,
        {{model.kneeAxes()[0].medialDirection, -4, 4}}); assert(joint);
    AnatomicalKinematicTree rig;
    auto hinge = rig.addJoint(0, *joint); assert(hinge);
    std::vector<std::size_t> bodies(model.sourceStructures().size());
    for (std::size_t i=0; i<bodies.size(); ++i) {
        if (i == tibia) { bodies[i] = *hinge; continue; }
        const auto& part = model.sourceStructures()[i];
        const auto& group = model.bones()[part.group].name;
        const bool child = group == "lower_leg_l" || group == "foot_l";
        auto body = rig.addFixed(child ? *hinge : 0, {}); assert(body); bodies[i] = *body;
    }
    rig.captureBindPose(); std::string error;
    assert(model.setSourceRig(rig, bodies, error));
    assert(differences(before, pixels(room, view)) == 0);
    assert(model.setReferenceBend(link, 20));
    const auto moved = pixels(room, view);
    const int changed = differences(before,moved);
    std::printf("Vulkan reference knee bend: %d changed pixels\n", changed);
    assert(changed > 20);
    assert(model.setReferenceBend(link, 200)); const auto clamped = pixels(room, view);
    assert(model.setReferenceBend(link, 135)); assert(differences(clamped,pixels(room, view)) == 0);
    assert(model.setReferenceBend(link, 0)); assert(differences(before,pixels(room, view)) == 0);
}

int main() {
    assert(vulkanAvailable());
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    setGpuRenderer(&renderVulkanScene);
    ViewState view; view.gpu=true; view.width=640; view.height=480;
    clinicalBend(room,view,room.male()); clinicalBend(room,view,room.female());
    run(room,view,room.male()); run(room,view,room.female());
    setGpuRenderer(nullptr);
    std::puts("Vulkan source rig frame path: PASS (synthetic hinge; no medical validation)");
}
