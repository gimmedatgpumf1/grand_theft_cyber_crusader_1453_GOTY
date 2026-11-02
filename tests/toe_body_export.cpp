// Export the fitted left comparison forefoot and its mapped toe bones for audit.
#include "client/skeleton_room.h"
#include "client/skeleton_toe_body.h"
#include "client/comparison_toe_motion.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
using namespace gtc::native;
static void write(const std::string& path,const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces) {
    std::ofstream f(path); assert(f); f<<std::setprecision(12);
    for (Vec3 p:points) f<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
    for (auto t:faces) f<<"f "<<t[0]+1<<' '<<t[1]+1<<' '<<t[2]+1<<'\n';
}
int main(int argc,char** argv) {
    if (argc<2 || argc>5) return 2;
    const int selected=argc>=4?std::stoi(argv[3]):0, joint=argc==5?std::stoi(argv[4]):0;
    if (selected<0 || selected>5 || joint<0 || joint>2 || (selected==1 && joint==2)) return 2;
    SkeletonRoom room; std::string error; assert(room.load(".",error)); std::filesystem::create_directories(argv[1]);
    for (bool male:{true,false}) {
        const auto& copy=room.comparison(male);
        if (argc>=3) { auto p=copy.profile(); p.height=std::stod(argv[2]); assert(room.setComparison(male,p,error)); }
        auto& model=male?room.male():room.female(); const std::string sex=male?"male":"female";
        ComparisonToeMotion motion; assert(motion.load(model,error));
        std::vector<Vec3> centers(copy.parts().size());
        for (int side=0;side<2;++side) for (int d=0;d<5;++d) for (int j=0;j<3;++j) {
            const int id=motion.parts()[side][d][j]; if (id<0) continue;
            const auto& part=copy.parts()[id]; Vec3 c;
            for (Vec3 p:part.points) c+=copy.mapPart(p,part);
            centers[id]=c/static_cast<double>(part.points.size());
        }
        std::array<std::array<ToeBoneSample,3>,5> bones; std::array<std::size_t,5> meta{};
        for (int d=0;d<5;++d) {
            const auto n=std::to_string(d+1)+"_L"; const int mi=model.sourceStructure("FootMeta"+n); assert(mi>=0); meta[d]=mi;
            const std::array<std::string,3> name={"FootProxP"+n,d==0?"":"FootMidP"+n,"FootDistP"+n};
            for (int j=0;j<3;++j) if (!name[j].empty()) {
                const int id=model.sourceStructure(name[j]); assert(id>=0); const auto& part=copy.parts()[id];
                auto& sample=bones[d][j]; sample.structure=id;
                for (Vec3 p:part.points) sample.points.push_back(copy.mapPart(p,part));
            }
        }
        const auto mesh=buildToeBody(bones,meta,copy.profile().height/copy.defaultHeight()); assert(mesh.valid);
        std::vector<ComparisonToeMotion::Matrix> identity(copy.parts().size(),ComparisonToeMotion::identity());
        SkeletonSkinBinding skinBinding,nailBinding;
        assert(skinBinding.bind(mesh.skin,identity,error) && nailBinding.bind(mesh.nails,identity,error));
        if (selected) {
            const auto suffix=std::to_string(selected)+"_L";
            const auto first=joint==0?"FootMeta"+suffix:joint==1?"FootProxP"+suffix:"FootMidP"+suffix;
            const auto second=joint==0?"FootProxP"+suffix:(joint==1 && selected>1)?"FootMidP"+suffix:"FootDistP"+suffix;
            std::size_t i=0;
            for (;i<model.sourceLinks().size();++i) {
                const auto& e=model.sourceLinks()[i];
                if (model.sourceStructures()[e.first].name==first && model.sourceStructures()[e.second].name==second) break;
            }
            assert(i<model.sourceLinks().size()); const auto angle=model.referenceBend(i); assert(angle);
            const auto& link=model.sourceLinks()[i];
            const auto range=referenceBendRange(link.mobility,first,second); assert(range);
            const double target=*angle+(*angle+10<=range->flexion?10:-10);
            assert(target>=range->extension && target<=range->flexion && model.setReferenceBend(i,target));
            assert(model.referenceBend(i) && std::abs(*model.referenceBend(i)-target)<1e-8);
        }
        const auto pose=motion.pose(model,centers,copy.profile().height/copy.defaultHeight());
        std::vector<Vec3> skin,nails; assert(skinBinding.deform(pose,skin,error)); assert(nailBinding.deform(pose,nails,error));
        for (int d=0;d<5;++d) for (int j=0;j<3;++j) if (!bones[d][j].points.empty()) {
            const int id=static_cast<int>(bones[d][j].structure); std::vector<Vec3> points;
            for (Vec3 p:bones[d][j].points) points.push_back(ComparisonToeMotion::transform(pose[id],p));
            write(std::string(argv[1])+"/"+sex+"_"+copy.parts()[id].name+".obj",points,copy.parts()[id].faces);
        }
        write(std::string(argv[1])+"/"+sex+"_skin.obj",skin,mesh.skinFaces);
        write(std::string(argv[1])+"/"+sex+"_nails.obj",nails,mesh.nailFaces);
    }
}
