#pragma once

#include "client/planar_gpu_scene.h"
#include <cstdint>

namespace gtc::native {
class RoomFreecam;
struct ViewState;

// A separate, four-body diagnostic world using the normal Vulkan physics.
class PlaygroundRoom {
public:
    bool reset();
    void open2d();
    void open3d() { planar_=false; sceneReady_=false; }
    bool is2d() const { return planar_; }
    void advance(double seconds);
    void setDragRay(Vec3 origin, Vec3 direction);
    void beginDrag();
    void endDrag() { dragging_=false; }
    bool render(NativeFrame& frame,const ViewState& view,const RoomFreecam& camera);
private:
    GpuPlanarRig rig_;
    GpuScene scene_;
    std::uint64_t generation_=0,steps_=0,revision_=0;
    double time_=0;
    std::array<float,4> dragOrigin_{},dragDirection_{};
    std::uint32_t dragSequence_=0;
    bool dragging_=false,sceneReady_=false,planar_=false;
};
} // namespace gtc::native
