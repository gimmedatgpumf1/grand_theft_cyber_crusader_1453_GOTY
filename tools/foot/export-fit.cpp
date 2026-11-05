// Export the unsimplified, uncorrected fit and its comparison bones for the
// offline surface adjustment. No runtime dependency on this helper.
#include "client/skeleton_room.h"
#include "tools/foot/experimental-fit.h"
#include <filesystem>
#include <fstream>
using namespace gtc::native;

static void save(const std::string& file,const std::vector<Vec3>& points,
                 const std::vector<std::array<int,3>>& faces) {
    std::ofstream out(file);out.precision(15);
    for (Vec3 p:points) out<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
    for (auto f:faces) out<<"f "<<f[0]+1<<' '<<f[1]+1<<' '<<f[2]+1<<'\n';
}
int main(int argc,char** argv) {
    const std::string output=argc>1 ? argv[1] : "foot-fit-export";
    SkeletonRoom room;std::string error;if (!room.load(".",error)) return 1;
    for (bool male:{true,false}) {
        const auto& c=room.comparison(male);
        for (const std::string side:{"_L","_R"}) {
            const std::string dir=output+"/"+std::string(male?"m":"f")+side;
            std::filesystem::create_directories(dir);
            const auto foot=makeHumanFoot(c,male,side,{0,.1,0},false);
            if (foot.points.empty()) return 2;
            save(dir+"/skin.obj",foot.points,foot.faces);
            for (const auto& part:c.parts()) {
                if (!part.name.ends_with(side)) continue;
                const bool use=part.name.starts_with("Foot") || part.name.starts_with("Cal_") ||
                    part.name.starts_with("Tal_") || part.name.starts_with("Nav_") ||
                    part.name.starts_with("Cub_") || part.name.find("Cun_")!=std::string::npos;
                if (!use) continue;
                std::vector<Vec3> points;points.reserve(part.points.size());
                for (Vec3 p:part.points) points.push_back(c.mapPart(p,part)+Vec3{0,.1,0});
                save(dir+"/"+part.name+".obj",points,part.faces);
            }
        }
    }
}
