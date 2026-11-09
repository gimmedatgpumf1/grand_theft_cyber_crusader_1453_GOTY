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
namespace {
void polygonBox(GpuPlanarRig& rig, Vec3 center, Vec3 size, bool movable) {
    gpu_physics::Body body;
    const double mass=seesaw::density*size.x*size.y*size.z;
    body.positionMass={float(center.x),float(center.y),0,movable ? float(1.0/mass) : 0};
    body.inverseInertia[2]=movable ? float(12.0/(mass*(size.x*size.x+size.y*size.y))) : 0;
    body.material={0.5f,float(size.z),1.05f,0.15f};
    body.shapes={static_cast<std::uint32_t>(rig.vertices.size()),4,0,0};
    const float x=float(size.x*0.5),y=float(size.y*0.5);
    rig.vertices.push_back({-x,-y,0,0}); rig.vertices.push_back({x,-y,0,0});
    rig.vertices.push_back({x,y,0,0}); rig.vertices.push_back({-x,y,0,0});
    rig.bodies.push_back(body);
}
}
bool PlaygroundRoom::reset() {
    GpuPlanarRig next;
    if(water_) next.environment.medium={1.225f,1000.0f,1.15f,0};
    gpu_physics::Body pin; pin.positionMass={float(seesaw::pivot.x),float(seesaw::pivot.y),0,0};
    pin.shapes={0,3,0,0};pin.material[0]=0.5f;
    next.vertices.push_back({-0.33f,-0.75f,0,0});
    next.vertices.push_back({0.33f,-0.75f,0,0});
    next.vertices.push_back({0,-0.15f,0,0});
    next.bodies.push_back(pin);
    polygonBox(next,seesaw::pivot,seesaw::beamSize,true);
    polygonBox(next,seesaw::smallCenter,seesaw::smallSize,true);
    polygonBox(next,seesaw::largeCenter,seesaw::largeSize,true);
    polygonBox(next,{0,-0.05,0},{12,0.10,1},false);
    gpu_physics::Joint hinge; hinge.bodies={0,1,0,0}; next.joints.push_back(hinge);
    seesaw::bind(next,seesaw::beamId,1,seesaw::pivot);
    seesaw::bind(next,seesaw::smallId,2,seesaw::smallCenter);
    seesaw::bind(next,seesaw::largeId,3,seesaw::largeCenter);
    rig_=std::move(next);
    generation_=nextPhysicsGeneration(); steps_=0; time_=0; dragging_=false;
    return true;
}
void PlaygroundRoom::open2d() { planar_=true;water_=false;sceneReady_=false;reset(); }
void PlaygroundRoom::toggleWater() { water_=!water_;sceneReady_=false;reset(); }
void PlaygroundRoom::advance(double seconds) {
    if (!planar_ || !generation_ || !std::isfinite(seconds) || seconds<=0) return;
    constexpr double hz=gpu_physics::kPhysicsHz;
    time_=std::min(time_+seconds,0.25);
    const auto steps=std::min<std::uint64_t>(static_cast<std::uint64_t>(time_*hz),8);
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
        if (!planar_) {
            drawBox(raster,camera,{0,-0.05,0},{12,0.10,12},floor);
            drawBox(raster,camera,{0,1.5,-6.05},{12,3,0.1},wall);
            drawBox(raster,camera,{-6.05,1.5,0},{0.1,3,12},wall);
            drawBox(raster,camera,{6.05,1.5,0},{0.1,3,12},wall);
            drawBox(raster,camera,{0,1.5,6.05},{12,3,0.1},wall);
        } else {
            const auto background=[&](double bottom,double top,const Material& m) {
                std::vector<Vec3> p={{-6,bottom,-0.0001},{6,bottom,-0.0001},{6,top,-0.0001},{-6,top,-0.0001}};
                const std::vector<std::array<int,3>> f={{0,1,2},{0,2,3}};
                raster.mesh(p,f,m);
            };
            floor.unlit=true;wall.unlit=true;
            background(-4,0,floor);background(0,4,wall);
            if(water_) {
                Material water;water.color=colorHex("#315c70");water.unlit=true;
                std::vector<Vec3> p={{-6,0,-0.00005},{6,0,-0.00005},{6,1.15,-0.00005},{-6,1.15,-0.00005}};
                const std::vector<std::array<int,3>> f={{0,1,2},{0,2,3}};
                raster.mesh(p,f,water);
            }
        }
        if (planar_) {
            Material support; support.color=colorHex("#78827c"); support.constantLighting=true;
            const Vec3 a{-0.33,0,0},b{0.33,0,0},c{0,0.60,0},n{0,0,1};
            raster.triangle({cameraVertex(camera,a,n,a),cameraVertex(camera,b,n,b),cameraVertex(camera,c,n,c)},support);
            const Vec3 d{-0.015,0.60,0},e{0.015,0.60,0},f{0.015,0.75,0},g{-0.015,0.75,0};
            raster.triangle({cameraVertex(camera,d,n,d),cameraVertex(camera,e,n,e),cameraVertex(camera,f,n,f)},support);
            raster.triangle({cameraVertex(camera,d,n,d),cameraVertex(camera,f,n,f),cameraVertex(camera,g,n,g)},support);
            const auto box=[&](std::uint32_t id,Vec3 center,Vec3 size,const char* color) {
                Material material;material.color=colorHex(color);
                const double x=size.x*0.5,y=size.y*0.5;
                std::vector<Vec3> points={{center.x-x,center.y-y,0},{center.x+x,center.y-y,0},{center.x+x,center.y+y,0},{center.x-x,center.y+y,0}};
                const std::vector<std::array<int,3>> faces={{0,1,2},{0,2,3}};
                raster.setObject(id,{0,0},0);raster.mesh(points,faces,material);
            };
            box(seesaw::beamId,seesaw::pivot,seesaw::beamSize,"#c8a56b");
            box(seesaw::smallId,seesaw::smallCenter,seesaw::smallSize,"#66aed2");
            box(seesaw::largeId,seesaw::largeCenter,seesaw::largeSize,"#df765a");
            raster.setObject(0,{0,0},0);
        } else seesaw::draw(raster,camera);
        scene_=raster.takeGpuScene(); scene_.revision=++revision_; sceneReady_=true;
    }
    scene_.width=camera.width; scene_.height=camera.height;
    scene_.focal=camera.focal(); scene_.viewMatrix=camera.viewMatrix(); scene_.covered=false;
    scene_.physicsRig=planar_ ? &rig_ : nullptr; scene_.physicsWorld=1; scene_.physicsGeneration=generation_; scene_.physicsSteps=steps_;
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
