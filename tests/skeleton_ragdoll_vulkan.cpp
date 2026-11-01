// Check both source-mesh and V views through the actual Vulkan frame path.
#include "client/skeleton_room.h"
#include "vulkan/vk_renderer.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace gtc::native;
static std::vector<unsigned char> pixels(SkeletonRoom& room, const ViewState& view) {
    NativeFrame frame; frame.wantPixels=true; room.render(frame,view,0);
    const auto count=std::size_t(view.width)*view.height*4;
    const auto* data=frame.direct ? frame.direct : frame.bgra.data();
    assert(data && (frame.direct || frame.bgra.size()>=count));
    return {data,data+count};
}
static std::size_t difference(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    assert(a.size()==b.size()); std::size_t count=0;
    for (std::size_t i=0;i<a.size();i+=4)
        if (std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>24) ++count;
    return count;
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    setGpuRenderer(&renderVulkanScene); ViewState view; view.gpu=true; view.width=960; view.height=720;
    auto before=pixels(room,view); room.toggleLineView(); auto beforeLines=pixels(room,view); room.toggleLineView();
    assert(room.toggleRagdoll()); for (int i=0;i<360;++i) room.advanceRagdoll(1.0/120.0);
    auto after=pixels(room,view); room.toggleLineView(); auto afterLines=pixels(room,view);
    const auto mesh=difference(before,after), lines=difference(beforeLines,afterLines);
    std::printf("Vulkan ragdoll changed pixels: mesh %zu, V view %zu\n",mesh,lines);
    assert(mesh>1000 && lines>1000);
}
