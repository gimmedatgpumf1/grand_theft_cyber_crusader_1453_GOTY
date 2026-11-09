#include "client/playground_room.h"
#include "client/playground_seesaw.h"
#include "client/renderer.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"
#include "vulkan/physics/rigid_timing.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gtc::native {
bool PlaygroundRoom::reset() {
    GpuSkeletonRig next;
    seesaw::append(next);
    const std::size_t count=next.bodies.size(),stride=(count+31)/32;
    next.exclusions.assign(count*stride,0);
    for (const auto& joint:next.joints) {
        const auto a=joint.bodies[0],b=joint.bodies[1];
        next.exclusions[a*stride+b/32]|=1u<<(b%32);
        next.exclusions[b*stride+a/32]|=1u<<(a%32);
    }
    std::vector<gpu_physics::Joint> ordered;
    if (!gpu_physics::colourJoints(count,next.joints,ordered,next.batches)) return false;
    next.joints=std::move(ordered);
    rig_=std::move(next);
    generation_=nextPhysicsGeneration(); steps_=0; time_=0; dragging_=false;
    return true;
}
void PlaygroundRoom::advance(double seconds) {
    if (!generation_ || !std::isfinite(seconds) || seconds<=0) return;
    constexpr double hz=gpu_physics::kPhysicsHz;
    time_+=std::min(seconds,2.0/hz);
    const auto steps=static_cast<std::uint64_t>(time_*hz);
    time_-=double(steps)/hz; steps_+=steps;
}
void PlaygroundRoom::setDragRay(Vec3 origin,Vec3 direction) {
    dragOrigin_={float(origin.x),float(origin.y),float(origin.z),0};
    dragDirection_={float(direction.x),float(direction.y),float(direction.z),0};
}
void PlaygroundRoom::beginDrag() {
    if (!generation_) return;
    dragging_=true; if (++dragSequence_==0) ++dragSequence_;
}
bool PlaygroundRoom::render(NativeFrame& frame,const ViewState& view,const RoomFreecam& freecam) {
    const Camera camera=freecam.camera(view.width,view.height,view.gpu);
    if (!sceneReady_) {
        Rasterizer raster(camera.width,camera.height,colorHex("#111c1b"),camera.focal(),true);
        raster.setCamera(camera);
        Material floor; floor.color=colorHex("#31433c"); floor.constantLighting=true;
        Material wall; wall.color=colorHex("#263831"); wall.constantLighting=true;
        drawBox(raster,camera,{0,-0.05,0},{12,0.10,12},floor);
        drawBox(raster,camera,{0,1.5,-6.05},{12,3,0.1},wall);
        drawBox(raster,camera,{-6.05,1.5,0},{0.1,3,12},wall);
        drawBox(raster,camera,{6.05,1.5,0},{0.1,3,12},wall);
        drawBox(raster,camera,{0,1.5,6.05},{12,3,0.1},wall);
        seesaw::draw(raster,camera);
        scene_=raster.takeGpuScene(); scene_.revision=1; sceneReady_=true;
    }
    scene_.width=camera.width; scene_.height=camera.height;
    scene_.focal=camera.focal(); scene_.viewMatrix=camera.viewMatrix(); scene_.covered=false;
    scene_.physicsRig=&rig_; scene_.physicsWorld=1; scene_.physicsGeneration=generation_; scene_.physicsSteps=steps_;
    scene_.physicsDragging=dragging_; scene_.physicsDragSequence=dragSequence_;
    scene_.physicsDragOrigin=dragOrigin_; scene_.physicsDragDirection=dragDirection_;
    const auto count=scene_.batches.size(); frame.interfaceInFrame=false;
    if (frame.uiLayer && !frame.uiLayer->empty()) {
        appendUiBatches(*frame.uiLayer,camera,scene_); frame.interfaceInFrame=true;
    }
    frame.width=camera.width; frame.height=camera.height; frame.wantsBgra=true; frame.rgb.clear();
    const bool ok=renderGpuScene(scene_,frame);
    scene_.batches.resize(count);
    return ok;
}
} // namespace gtc::native
