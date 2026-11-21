// The four paired source labels must share one actual rig body without
// preventing the C-room loader or joint controls from working.
#include "client/skeleton_room.h"
#include "client/skeleton_joint_limits_view.h"
#include "client/room_freecam.h"
#include "vulkan/vk_internal.h"
#include "vulkan/vk_renderer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>

using namespace gtc::native;

static std::size_t capturedBatches=0;
static bool captureScene(const GpuScene& scene,NativeFrame& frame) {
    capturedBatches=scene.batches.size();
    std::vector<detail::BatchRun> runs;std::vector<float> slots;
    detail::planRuns(scene,runs,slots);
    std::unordered_set<std::uint32_t> skinOwners;
    for(std::size_t i=0;i<scene.batches.size();++i)
        if(scene.batches[i].skinOwner && i<slots.size() && slots[i]>=0)
            skinOwners.insert(scene.batches[i].objectId);
    for(const auto& batch:scene.batches) {
        for(std::uint32_t i:batch.indices)if(i>=batch.vertices.size())return false;
        for(const auto& vertex:batch.vertices) {
            if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||!std::isfinite(vertex.z))return false;
            if(batch.skinObjectIds && !skinOwners.count(static_cast<std::uint32_t>(vertex.nx)))return false;
            if(batch.blendedSkin && !skinOwners.count(static_cast<std::uint32_t>(vertex.ny)))return false;
        }
    }
    frame.width=scene.width;frame.height=scene.height;
    frame.rgb.assign(static_cast<std::size_t>(frame.width*frame.height*3),0);
    return true;
}

static void checkPair(SkeletonModel& model,const char* primary,const char* alias) {
    const int a=model.sourceStructure(primary),b=model.sourceStructure(alias);
    assert(a>=0 && b>=0);
    const auto& map=model.sourceBodyMap();
    assert(map[static_cast<std::size_t>(a)]==map[static_cast<std::size_t>(b)]);
    assert(model.sourceRig()->joint(map[static_cast<std::size_t>(a)]));
}

static void sameTransform(const SkeletonModel& model,const char* a,const char* b) {
    const int ai=model.sourceStructure(a),bi=model.sourceStructure(b);
    const auto pose=model.sourceMatrices({},0);
    assert(ai>=0 && bi>=0 && pose.size()==model.sourceStructures().size());
    for(std::size_t k=0;k<16;++k)
        assert(std::abs(pose[static_cast<std::size_t>(ai)][k]-pose[static_cast<std::size_t>(bi)][k])<1e-12);
}

static void checkElbowMotionAxes(const SkeletonModel& model,const char* suffix) {
    const int radius=model.sourceStructure(std::string("Radius_")+suffix);
    const int ulna=model.sourceStructure(std::string("Ulna_")+suffix);
    assert(radius>=0 && ulna>=0);
    const auto& mapping=model.sourceBodyMap();const auto* rig=model.sourceRig();
    assert(mapping[static_cast<std::size_t>(radius)]==mapping[static_cast<std::size_t>(ulna)]);
    const auto* elbow=rig->joint(mapping[static_cast<std::size_t>(radius)]);
    assert(elbow && elbow->axes().size()>=2);
    assert(elbow->axes()[0].role==JointAxisRole::Motion);
    assert(elbow->axes()[1].role==JointAxisRole::Alignment);
    const double radians=3.14159265358979323846/180.0;
    assert(std::abs(elbow->axes()[0].minimum)<1e-12);
    assert(std::abs(elbow->axes()[0].maximum-150*radians)<1e-12);
    const auto coupled=std::find_if(elbow->axes().begin(),elbow->axes().end(),
        [](const auto& axis){return axis.role==JointAxisRole::Coupled;});
    assert(coupled!=elbow->axes().end());
    assert(!joint_view::mergedSecondary(model.sourceStructures()[static_cast<std::size_t>(radius)].name));
    assert(joint_view::mergedSecondary(model.sourceStructures()[static_cast<std::size_t>(ulna)].name));
}

static void checkKneeMotionAxes(const SkeletonModel& model,const char* suffix) {
    const int tibia=model.sourceStructure(std::string("Tibia_")+suffix);
    const int fibula=model.sourceStructure(std::string("Fibula_")+suffix);
    const int patella=model.sourceStructure(std::string("Patella_")+suffix);
    assert(tibia>=0 && fibula>=0 && patella>=0);
    const auto* rig=model.sourceRig();const auto& mapping=model.sourceBodyMap();
    const auto* knee=rig->joint(mapping[static_cast<std::size_t>(tibia)]);
    assert(knee && knee->axes().size()==2);
    assert(knee->axes()[0].role==JointAxisRole::Motion);
    assert(knee->axes()[1].role==JointAxisRole::Alignment);
    const double radians=3.14159265358979323846/180.0;
    assert(std::abs(knee->axes()[0].minimum)<1e-12);
    assert(std::abs(knee->axes()[0].maximum-135*radians)<1e-12);
    const auto* patellaJoint=rig->joint(mapping[static_cast<std::size_t>(patella)]);
    assert(patellaJoint && patellaJoint->axes().size()==1);
    assert(patellaJoint->axes()[0].role==JointAxisRole::Coupled);
}

static void checkDisplayBone(const SkeletonModel& model,const SkeletonLines& lines,
                             const char* group,const char* representative) {
    const int source=model.sourceStructure(representative);assert(source>=0);
    const auto solid=std::find_if(lines.solids().begin(),lines.solids().end(),
        [&](const auto& item){return item.name==group;});
    assert(solid!=lines.solids().end() && !solid->faces.empty() && !solid->owners.empty());
    for(int owner:solid->owners)assert(owner==source);
}

int main() {
    SkeletonRoom room; std::string error;
    if(!room.load(".",error)) { std::cerr<<"C-room load failed: "<<error<<'\n'; return 1; }
    for(SkeletonModel* model:{&room.male(),&room.female()}) {
        assert(model->sourceStructures().size()==182);
        assert(model->sourceRig()->size()==179);
        checkPair(*model,"Radius_L","Ulna_L");checkPair(*model,"Radius_R","Ulna_R");
        checkPair(*model,"Tibia_L","Fibula_L");checkPair(*model,"Tibia_R","Fibula_R");
        checkKneeMotionAxes(*model,"L");checkKneeMotionAxes(*model,"R");
        checkElbowMotionAxes(*model,"L");checkElbowMotionAxes(*model,"R");
        SkeletonLines lines;
        const char* side=model==&room.male()?"male":"female";
        assert(lines.load(std::string("tools/skeleton-source/skeleton-")+side+".lines",error));
        assert(lines.bind(*model,error));
        checkDisplayBone(*model,lines,"Forearm_L","Radius_L");
        checkDisplayBone(*model,lines,"Forearm_R","Radius_R");
        checkDisplayBone(*model,lines,"Shin_L","Tibia_L");
        checkDisplayBone(*model,lines,"Shin_R","Tibia_R");
        sameTransform(*model,"Radius_L","Ulna_L");sameTransform(*model,"Radius_R","Ulna_R");
        sameTransform(*model,"Tibia_L","Fibula_L");sameTransform(*model,"Tibia_R","Fibula_R");
    }
    assert(room.adjustReferenceGroup('e',5.0));
    assert(room.adjustReferenceGroup('h',5.0));
    assert(room.adjustReferenceGroup('m',5.0));
    for(SkeletonModel* model:{&room.male(),&room.female()}) {
        sameTransform(*model,"Radius_L","Ulna_L");sameTransform(*model,"Radius_R","Ulna_R");
        sameTransform(*model,"Tibia_L","Fibula_L");sameTransform(*model,"Tibia_R","Fibula_R");
    }
    room.toggleLineView();
    ViewState view;view.width=640;view.height=480;
    RoomFreecam camera;
    NativeFrame cpuFrame;room.render(cpuFrame,view,0.0,&camera);
    assert(cpuFrame.width==view.width && cpuFrame.height==view.height && !cpuFrame.rgb.empty());
    view.gpu=true;setGpuRenderer(&captureScene);
    NativeFrame gpuFrame;room.render(gpuFrame,view,0.0,&camera);
    setGpuRenderer(nullptr);
    assert(capturedBatches>0 && gpuFrame.width==view.width && gpuFrame.height==view.height);
    if(std::getenv("GTC_TEST_VULKAN")) {
        if(vulkanAvailable()) {
            setGpuRenderer(&renderVulkanScene);
            NativeFrame deviceFrame;deviceFrame.width=view.width;deviceFrame.height=view.height;deviceFrame.wantPixels=true;
            room.render(deviceFrame,view,0.0,&camera);
            setGpuRenderer(nullptr);
            const std::size_t pixels=static_cast<std::size_t>(view.width)*view.height;
            assert(deviceFrame.direct || deviceFrame.bgra.size()>=pixels*4 || deviceFrame.rgb.size()>=pixels*3);
            shutdownVulkan();
            std::cout<<"Vulkan device V render: PASS\n";
        } else std::cout<<"Vulkan device V render: SKIP (no loader/device)\n";
    }
    std::cout<<"Merged forearm/shin rig, C-room controls, and V render paths: PASS\n";
}
