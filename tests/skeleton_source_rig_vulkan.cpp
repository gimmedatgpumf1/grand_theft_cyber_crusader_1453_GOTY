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
    auto femur = rig.addFixed(0, {}); assert(femur);
    auto hinge = rig.addJoint(*femur, *joint); assert(hinge);
    const auto femurSource = model.sourceLinks()[link].first;
    std::vector<std::size_t> bodies(model.sourceStructures().size());
    for (std::size_t i=0; i<bodies.size(); ++i) {
        if (i == femurSource) { bodies[i] = *femur; continue; }
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

// Use the default room rig, not a synthetic replacement. This checks actual
// GPU-visible motion and V-view cache invalidation, not clinical calibration.
static void liveBend(SkeletonRoom& room, ViewState view, SkeletonModel& model, const char* name) {
    std::size_t link=0;
    for (; link<model.sourceLinks().size(); ++link) {
        const auto& edge=model.sourceLinks()[link];
        if (edge.mobility=="elbow_hinge" && model.sourceStructures()[edge.first].name=="Humerus_L" &&
            model.sourceStructures()[edge.second].name=="Ulna_L") break;
    }
    assert(link<model.sourceLinks().size()); const double initial=*model.referenceBend(link);
    const double target=initial > 65 ? 15 : 110;
    for (int line=0;line<2;++line) {
        if (line) room.toggleLineView();
        const auto before=pixels(room,view);
        assert(model.setReferenceBend(link,target)); const auto bent=pixels(room,view);
        const int changed=differences(before,bent);
        std::printf("Vulkan live %s %s elbow: %d changed pixels\n",name,line?"V-view":"mesh",changed);
        assert(changed>10);
        assert(model.setReferenceBend(link,initial)); assert(differences(before,pixels(room,view))==0);
    }
    room.toggleLineView();
}

static void liveWrist(SkeletonRoom& room, ViewState view, SkeletonModel& model, const char* name) {
    std::size_t scaphoid=0, lunate=0;
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& edge=model.sourceLinks()[i];
        const auto& a=model.sourceStructures()[edge.first].name;
        const auto& b=model.sourceStructures()[edge.second].name;
        if (a=="Radius_L" && b=="HandScaphoid_L") scaphoid=i;
        if (a=="Radius_L" && b=="HandLuna_L") lunate=i;
    }
    assert(model.referenceBend(scaphoid) && model.referenceBend(lunate));
    const double original=*model.referenceBend(scaphoid);
    for (int line=0;line<2;++line) {
        if (line) room.toggleLineView();
        const auto before=pixels(room,view);
        assert(model.setReferenceBend(scaphoid,65));
        assert(std::abs(*model.referenceBend(lunate)-65)<1e-7);
        const auto after=pixels(room,view); const int changed=differences(before,after);
        std::printf("Vulkan live %s %s grouped wrist: %d changed pixels\n",name,line?"V-view":"mesh",changed);
        assert(changed>10);
        assert(model.setReferenceBend(scaphoid,original));
        assert(differences(before,pixels(room,view))==0);
    }
    room.toggleLineView();
}

static int differencesHalf(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b,
                           int width, int height, bool right) {
    int changed=0;
    for (int y=0;y<height;++y) for (int x=right ? width/2 : 0;x<(right ? width : width/2);++x) {
        const auto i=static_cast<std::size_t>(y*width+x)*4; int delta=0;
        for (int channel=0;channel<3;++channel) delta+=std::abs(int(a[i+channel])-int(b[i+channel]));
        if (delta>24) ++changed;
    }
    return changed;
}
// The same group operation that the game's key events call, not a test rig.
static void liveRoomControls(SkeletonRoom& room, ViewState view) {
    for (int line=0;line<2;++line) {
        if (line) room.toggleLineView();
        for (const auto key : {'q','e','f','2','6','g','h','j','n'}) {
            const auto before=pixels(room,view);
            assert(room.adjustReferenceGroup(key,30)); const auto moved=pixels(room,view);
            const int male=differencesHalf(before,moved,view.width,view.height,false);
            const int female=differencesHalf(before,moved,view.width,view.height,true);
            std::printf("Vulkan room %s %c controls: %d/%d changed pixels\n",line?"V-view":"mesh",key,male,female);
            assert(male>0 && female>0);
            assert(room.restoreReferencePose()); assert(differences(before,pixels(room,view))==0);
        }
    }
    room.toggleLineView();
}

int main() {
    assert(vulkanAvailable());
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    setGpuRenderer(&renderVulkanScene);
    ViewState view; view.gpu=true; view.width=640; view.height=480;
    liveBend(room,view,room.male(),"male"); liveBend(room,view,room.female(),"female");
    liveWrist(room,view,room.male(),"male"); liveWrist(room,view,room.female(),"female");
    liveRoomControls(room,view);
    assert(room.male().restoreSourcePose() && room.female().restoreSourcePose());
    clinicalBend(room,view,room.male()); clinicalBend(room,view,room.female());
    run(room,view,room.male()); run(room,view,room.female());
    setGpuRenderer(nullptr);
    std::puts("Vulkan source rig frame path: PASS (synthetic hinge; no medical validation)");
}
