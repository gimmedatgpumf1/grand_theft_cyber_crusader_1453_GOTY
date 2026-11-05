// Standalone visualizer for the unintegrated fitting and reduction work.
#include "client/skeleton_room.h"
#include "tools/foot/experimental-fit.h"
#include <filesystem>
#include <string>
using namespace gtc::native;

int main(int argc,char** argv) {
    const std::string output=argc>1 ? argv[1] : "foot-progress";
    const bool reduced=argc>2 && std::string(argv[2])=="--reduced";
    std::filesystem::create_directories(output);
    SkeletonRoom room; std::string error;
    if (!room.load(".",error)) return 1;
    for (bool male:{true,false}) {
        auto& c=room.comparison(male);
        auto profile=c.profile(); profile.height=male ? 172.2 : 159.4;
        if (!room.setComparison(male,profile,error)) return 2;
        for (bool bent:{false,true}) {
            room.restoreReferencePose();
            if (bent) {
                for (int k='6';k<='9';++k) room.adjustReferenceGroup(k,20,1);
                room.adjustReferenceGroup('0',20,1);
            }
            const auto pose=c.toePose((male ? room.male() : room.female()).sourceMatrices({},0),{0,.1,0});
            const auto transform=[&](std::size_t i,Vec3 p) {
                const auto& m=pose[i];
                return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
            };
            for (const std::string side:{"_L","_R"}) {
                Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
                for (const auto& part:c.parts()) if (part.name.ends_with(side) &&
                    (part.name.starts_with("Cal_") || part.name.starts_with("FootDistP"))) {
                    for (Vec3 p:part.points) {
                        p=c.mapPart(p,part)+Vec3{0,.1,0};
                        lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
                        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
                    }
                }
                const Vec3 target=(lo+hi)*.5;const double size=hi.z-lo.z;
                auto foot=reduced ? makeHumanFootReduced(c,male,side,{0,.1,0})
                                  : makeHumanFoot(c,male,side,{0,.1,0});
                if (foot.points.empty()) return 3;
                const std::string tag=std::string(male?"m":"f")+"0"+side+(bent?"-bend":"-rest");
                for (int view=0;view<4;++view) {
                    Camera camera;camera.width=1000;camera.height=700;camera.fovDegrees=30;camera.target=target;
                    if (view==0) camera.position=target+Vec3{size*2.7,size*.25,0};
                    if (view==1) camera.position=target+Vec3{size*2,size*1.2,size*1.7};
                    if (view==2) {camera.position=target+Vec3{0,-size*2.7,0};camera.up={0,0,1};}
                    if (view==3) {camera.position=target+Vec3{0,size*2.7,0};camera.up={0,0,-1};}
                    Rasterizer r(camera.width,camera.height,colorHex("#222d30"),camera.focal());r.setCamera(camera);
                    Material blue;blue.color=colorHex("#358fff");
                    Material flesh;flesh.color=colorHex("#e9bd9f");
                    Material nail;nail.color=colorHex("#eacabe");
                    for (std::size_t i=0;i<c.parts().size();++i) {
                        const auto& part=c.parts()[i];if (!part.name.ends_with(side)) continue;
                        const bool include=part.name.starts_with("Foot") || part.name.starts_with("Cal_") ||
                            part.name.starts_with("Tal_") || part.name.starts_with("Nav_") ||
                            part.name.starts_with("Cub_") || part.name.find("Cun_")!=std::string::npos;
                        if (!include) continue;
                        std::vector<Vec3> points;points.reserve(part.points.size());
                        for (Vec3 p:part.points) points.push_back(transform(i,c.mapPart(p,part)+Vec3{0,.1,0}));
                        r.mesh(points,part.faces,blue);
                    }
                    auto points=foot.points;
                    for (std::size_t i=0;i<points.size();++i) {
                        const Vec3 p=points[i];
                        points[i]=transform(foot.owners[i],p);
                        points[i]=points[i]*foot.ownerWeights[i]+
                            transform(foot.secondaryOwners[i],p)*(1-foot.ownerWeights[i]);
                    }
                    r.mesh(points,foot.faces,flesh);
                    for (std::size_t i=0;i<c.parts().size();++i) {
                        if (!c.parts()[i].name.ends_with(side) || !c.parts()[i].name.starts_with("FootDistP")) continue;
                        auto toeNail=makeHumanNail(foot,static_cast<int>(i));
                        for (Vec3& p:toeNail.nailPoints) p=transform(i,p);
                        r.mesh(toeNail.nailPoints,toeNail.nailFaces,nail);
                    }
                    if (!r.finish().save(output+"/"+tag+"-"+std::to_string(view)+".png")) return 4;
                }
            }
        }
    }
}
