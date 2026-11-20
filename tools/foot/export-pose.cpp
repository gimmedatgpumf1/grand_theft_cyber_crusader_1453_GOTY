// Export posed experimental meshes for offline surface-clearance evaluation.
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
static Vec3 apply(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
int main(int argc,char** argv) {
    const std::string output=argc>1 ? argv[1] : "foot-pose-export";
    bool reduced=false,baseOnly=false,sweep=false;
    for(int i=2;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--reduced")reduced=true;
        else if(option=="--base-only")baseOnly=true;
        else if(option=="--sweep")sweep=true;
        else return 4;
    }
    struct Pose {int bend=0,key=0,joint=1;std::string name;};
    std::vector<Pose> poses{{0,0,1,"rest"},{20,0,1,"bend"},{-20,0,1,"reverse"}};
    if(sweep)for(int toe=1;toe<=5;++toe)for(int joint=1;joint<=(toe==1?2:3);++joint)
        for(int angle:{20,-20})poses.push_back({angle,toe==5?'0':'5'+toe,joint,
            "toe"+std::to_string(toe)+"-joint"+std::to_string(joint)+(angle>0?"-positive":"-negative")});
    SkeletonRoom room;std::string error;if (!room.load(".",error)) return 1;
    for (bool male:{true,false}) for (int height:{50,0,250}) {
        auto& c=room.comparison(male);auto profile=c.profile();
        profile.height=height ? height : (male ? 172.2 : 159.4);
        if(!room.setComparison(male,profile,error))return 2;
        for (const Pose& sample:poses) {
            if(sweep && height!=50 && sample.key)continue;
            room.restoreReferencePose();
            if(sample.key)room.adjustReferenceGroup(sample.key,sample.bend,sample.joint);
            else if(sample.bend) {
                for(int key='6';key<='9';++key) room.adjustReferenceGroup(key,sample.bend,1);
                room.adjustReferenceGroup('0',sample.bend,1);
            }
            const auto pose=c.toePose((male ? room.male() : room.female()).sourceMatrices({},0),{0,.1,0});
            for (const std::string side:{"_L","_R"}) {
                const std::string dir=output+"/"+(male?"m":"f")+std::to_string(height)+side+"-"+sample.name;
                std::filesystem::create_directories(dir);
                auto foot=reduced?makeHumanFootReduced(c,male,side,{0,.1,0},!baseOnly)
                                 :makeHumanFoot(c,male,side,{0,.1,0},true,!baseOnly);
                if(foot.points.empty())return 3;
                std::ofstream matrices(dir+"/skin-matrices.txt");matrices.precision(15);
                matrices<<foot.length<<'\n';
                for(std::size_t i=0;i<foot.points.size();++i) {
                    Vec3 p=foot.points[i];const double w=foot.ownerWeights[i];
                    const auto& a=pose[foot.owners[i]];const auto& b=pose[foot.secondaryOwners[i]];
                    for(int j:{0,4,8,1,5,9,2,6,10}) matrices<<a[j]*w+b[j]*(1-w)<<' ';
                    matrices<<'\n';
                    foot.points[i]=apply(a,p)*w+apply(b,p)*(1-w);
                }
                save(dir+"/skin.obj",foot.points,foot.faces);
                for (std::size_t i=0;i<c.parts().size();++i) {
                    const auto& part=c.parts()[i];if(!part.name.ends_with(side))continue;
                    const bool limb=part.name.starts_with("Tibia_") || part.name.starts_with("Fibula_");
                    const bool use=limb ||part.name.starts_with("Foot") || part.name.starts_with("Cal_") ||
                        part.name.starts_with("Tal_") || part.name.starts_with("Nav_") ||
                        part.name.starts_with("Cub_") || part.name.find("Cun_")!=std::string::npos;
                    if(!use)continue;
                    std::vector<Vec3> points;points.reserve(part.points.size());
                    for(Vec3 p:part.points)points.push_back(apply(pose[i],c.mapPart(p,part)+Vec3{0,.1,0}));
                    const std::string target=limb?dir+"/ankle-bones":dir;
                    if(limb)std::filesystem::create_directories(target);
                    save(target+"/"+part.name+".obj",points,part.faces);
                }
            }
        }
    }
}
