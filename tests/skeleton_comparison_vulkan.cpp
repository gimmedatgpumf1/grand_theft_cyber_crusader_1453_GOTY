// End-to-end comparison and HUD device pixels. An offscreen ICD is sufficient;
// a headless or software ICD does not establish swapchain or physical-GPU FPS.
#include "client/skeleton_comparison_ui.h"
#include "client/room_freecam.h"
#include "client/ui_layer.h"
#include "vulkan/vk_renderer.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace gtc::native;
static std::vector<unsigned char> picture;
static std::vector<int> contributions;
static bool render(const GpuScene& scene,NativeFrame& frame) {
    if (!renderVulkanScene(scene,frame)) return false;
    auto bytes=std::size_t(scene.width)*scene.height*4;
    const unsigned char* p=frame.direct?frame.direct:frame.bgra.data();
    if (!p) return false;
    picture.assign(p,p+bytes); contributions.clear();
    for (unsigned id:{12000u,14000u,0u,1u}) {
        GpuScene removed=scene; removed.revision+=id+1;
        removed.batches.erase(std::remove_if(removed.batches.begin(),removed.batches.end(),[id](const GpuBatch& b){
            if (id==0) return b.isUi;
            if (id==1) return b.isUi && b.material.texture!=nullptr;
            return b.objectId>=id && b.objectId<id+1000;
        }),removed.batches.end());
        NativeFrame out; out.width=scene.width;out.height=scene.height;out.wantPixels=true;out.wantsBgra=true;
        if (!renderVulkanScene(removed,out)) return false;
        const unsigned char* q=out.direct?out.direct:out.bgra.data(); if (!q) return false;
        int changed=0;
        for (std::size_t i=0;i<bytes;i+=4) {
            int d=0;for(int c=0;c<3;++c)d+=std::abs(int(q[i+c])-int(picture[i+c]));
            if (d>24) ++changed;
        }
        contributions.push_back(changed);
    }
    return true;
}
int main() {
    assert(vulkanAvailable()); std::string error; SkeletonRoom room; assert(room.load(".",error));
    SkeletonComparisonUi controls; RoomFreecam camera; ViewState view;view.gpu=true;view.width=960;view.height=540;
    NativeFrame frame;frame.width=view.width;frame.height=view.height;frame.wantPixels=true; UiLayer ui;
    assert(controls.handle(WindowEvent{WindowEvent::Type::KeyDown,'p'},true,view.width,view.height,room));
    ui.clear(view.width,view.height);frame.ui=&ui;controls.draw(frame,room);frame.ui=nullptr;frame.uiLayer=&ui;
    setGpuRenderer(render);room.render(frame,view,0,&camera);
    assert(frame.interfaceInFrame && contributions.size()==4);
    for (int n:contributions) assert(n>30);
    std::printf("Vulkan comparison: male blue %d, female blue %d, UI %d, text %d changed pixels\n",contributions[0],contributions[1],contributions[2],contributions[3]);
    const auto before=picture;
    auto p=room.comparison(true).profile();p.multiplier[SkeletonComparison::Head]=1.12;
    assert(room.setComparison(true,p,error));room.render(frame,view,0,&camera);
    int changed=0;for (std::size_t i=0;i<picture.size();i+=4) {
        int d=0;for (int c=0;c<3;++c)d+=std::abs(int(picture[i+c])-int(before[i+c]));if(d>24)++changed;
    }
    std::printf("Vulkan head slider: %d changed pixels\n",changed);assert(changed>20);
    room.toggleLineView();room.render(frame,view,0,&camera);
    assert(frame.interfaceInFrame && contributions.size()==4 && contributions[0]>30 && contributions[1]>30 && contributions[2]>30 && contributions[3]>30);
    setGpuRenderer(nullptr);shutdownVulkan();std::puts("Vulkan comparison and GPU UI: PASS");
}
