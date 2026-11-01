#include "client/skeleton_room.h"
#include "client/skeleton_comparison_ui.h"
#include "client/ui_layer.h"
#include "vulkan/vk_renderer.h"
#include <cstdio>
#include <string>
using namespace gtc::native;
int main() {
    int dummy = 1;
    setVulkanSurfaceTarget(&dummy, 2);
    if (!vulkanAvailable()) { std::puts("no Vulkan WSI"); return 1; }
    SkeletonRoom room; std::string error;
    if (!room.load(".", error)) { std::puts(error.c_str()); return 1; }
    setGpuRenderer(&renderVulkanScene);
    NativeFrame frame; ViewState view; view.gpu = true; UiLayer layer; SkeletonComparisonUi controls;
    controls.handle(WindowEvent{WindowEvent::Type::KeyDown,'p'},true,640,480,room);
    const int widths[] = {320, 800, 640, 400, 960, 320, 1, 640};
    const int heights[] = {240, 600, 480, 600, 540, 240, 1, 480};
    for (int i = 0; i < 24; ++i) {
        view.width = widths[i % 8]; view.height = heights[i % 8];
        frame.width=view.width; frame.height=view.height; frame.uiScale=1; frame.uiOffsetX=frame.uiOffsetY=0;
        layer.clear(view.width,view.height); frame.ui=&layer; controls.draw(frame,room);
        frame.ui=nullptr; frame.uiLayer=&layer;
        room.render(frame, view, i < 8 ? 1.75 : 1.90 + (i / 8.0));
        if (!frame.interfaceInFrame || !frame.presented || frame.wantPixels || !frame.rgb.empty() || !frame.bgra.empty() || frame.direct) {
            std::printf("frame %d: got %dx%d requested %dx%d present %d\n", i, frame.width, frame.height, view.width, view.height, frame.presented); return 1;
        }
    }
    std::puts("real Vulkan headless swapchain: PASS (24 frames with GPU controls, 24 forced swapchain recreations)");
    setGpuRenderer(nullptr); shutdownVulkan();
}
