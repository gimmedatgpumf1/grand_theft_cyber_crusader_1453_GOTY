// Off-screen Vulkan close-ups of the *current* resting pose. This tool makes
// pictures only: it does not measure bones or choose/apply pose corrections.
#include "client/skeleton_room.h"
#include "vulkan/vk_renderer.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
using namespace gtc::native;
static Vec3 transformed(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
int main(int argc,char** argv) {
    if(argc!=6) {std::cerr<<"inspect-toes OUTPUT male|female L|R 1..5 v|isolated|solid|blue|bluebones\n";return 1;}
    const bool male=std::string(argv[2])=="male",blue=std::string(argv[5])=="blue" || std::string(argv[5])=="bluebones",linesView=std::string(argv[5])=="v" || std::string(argv[5])=="isolated";
    const char side=argv[3][0];const int digit=std::atoi(argv[4]);
    if((side!='L'&&side!='R') || digit<1 || digit>5)return 1;
    std::filesystem::create_directories(argv[1]);SkeletonRoom room;std::string error;
    if(!room.load(".",error)){std::cerr<<error<<'\n';return 2;}
    if(std::getenv("ORIGINAL") && !room.restoreOriginalPose())return 7;
    auto& model=male?room.male():room.female();const auto& comparison=room.comparison(male);
    SkeletonLines lines;const std::string source=std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".lines";
    std::string lineSource=source;
    if(std::string(argv[5])=="isolated") {
        lineSource=std::string(argv[1])+"/selected.lines";std::ifstream in(source);std::ofstream out(lineSource);std::string text;
        const auto tag=std::to_string(digit)+"_"+side;
        while(std::getline(in,text)) {std::istringstream row(text);char kind;std::string name;row>>kind>>name;
            if(kind=='B' && name.starts_with("Foot") && name.ends_with(tag))out<<text<<"\n";
        }
    }
    if(!lines.load(lineSource,error)||!lines.bind(model,error)){std::cerr<<error<<'\n';return 3;}
    const auto pose=model.sourceMatrices({},0);const Vec3 origin{0,.10,0};
    const auto bluePose=comparison.toePose(pose,origin);
    const std::string suffix=std::to_string(digit)+"_"+side;
    Vec3 center{};
    for(const std::string& prefix:{"FootProxP","FootDistP"}) {
        const int i=model.sourceStructure(prefix+suffix);if(i<0)return 4;
        const auto& part=model.sourceStructures()[std::size_t(i)];Vec3 p{};
        if(blue) {const auto& b=comparison.parts()[std::size_t(i)];for(Vec3 v:b.points)p+=v;
            p=comparison.mapPart(p/double(b.points.size()),b)+origin;p=transformed(bluePose[std::size_t(i)],p);
        } else {for(std::size_t j=0;j<part.pointCount;++j)p+=model.bones()[part.group].points[part.firstPoint+j];
            p=transformed(pose[std::size_t(i)],p/double(part.pointCount));}
        center+=p*.5;
    }
    for(int view=0;view<3;++view) {
        Camera camera;camera.width=1000;camera.height=900;camera.fovDegrees=32;camera.target=center;camera.worldSpaceGpu=true;
        if(view==0){camera.position=center+Vec3{0,.18,0};camera.up={0,0,-1};}
        if(view==1)camera.position=center+Vec3{side=='L'?-.14:.14,.13,.10};
        if(view==2)camera.position=center+Vec3{side=='L'?-.19:.19,.04,-.02};
        Rasterizer raster(camera.width,camera.height,colorHex("#0d1210"),camera.focal(),true);raster.setCamera(camera);
        if(std::string(argv[5])=="bluebones") {
            Material m;m.color=colorHex("#358fff");m.specular=.12;
            for(std::size_t i=0;i<comparison.parts().size();++i) {
                const auto& part=comparison.parts()[i];
                if(!part.name.starts_with("Foot") || !part.name.ends_with(suffix))continue;
                std::vector<Vec3> points;for(Vec3 p:part.points)points.push_back(comparison.mapPart(p,part)+origin);
                raster.setObject(12000+std::uint32_t(i),{},0);raster.mesh(points,part.faces,m);raster.setBatchPose(bluePose[i]);
            }
        } else if(blue)comparison.draw(raster,0,12000,&bluePose);
        else if(linesView)lines.draw(raster,{},0,9001,pose);
        else model.buildSourceRestGeometry(raster,9001);
        auto scene=raster.takeGpuScene();
        if(!blue && !linesView) {
            scene.batches.erase(std::remove_if(scene.batches.begin(),scene.batches.end(),[&](const GpuBatch& batch) {
                const int i=int(batch.objectId)-9001;if(i<0||std::size_t(i)>=pose.size())return true;
                const auto& p=model.sourceStructures()[std::size_t(i)];
                return !p.name.ends_with(std::string("_")+side) || !p.name.starts_with("Foot") ||
                    (view==2 && p.name.find(suffix)==std::string::npos);
            }),scene.batches.end());
            for(auto& batch:scene.batches)batch.modelMatrix=pose[batch.objectId-9001];
        }
        scene.revision=std::uint64_t(1+view);scene.covered=false;
        NativeFrame frame;frame.width=camera.width;frame.height=camera.height;frame.wantPixels=true;frame.wantsBgra=true;
        if(!renderVulkanScene(scene,frame)){std::cerr<<"Vulkan render failed\n";return 5;}
        frame.syncToRgb();
        const std::string name=std::string(argv[1])+"/"+argv[2]+"_"+side+"_"+argv[4]+"_"+argv[5]+"_"+std::to_string(view)+".png";
        if(!frame.save(name))return 6;
    }
    shutdownVulkan();
}
