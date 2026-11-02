// Export comparison toe surfaces and underlying phalanges for geometry audits.
#include "client/skeleton_room.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
using namespace gtc::native;
static void write(const std::filesystem::path& path,const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces) {
    std::ofstream out(path); assert(out); out<<std::setprecision(12);
    for (Vec3 p:points) out<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
    for (auto f:faces) out<<"f "<<f[0]+1<<' '<<f[1]+1<<' '<<f[2]+1<<'\n';
    assert(out);
}
int main(int argc,char** argv) {
    if (argc!=2 && argc!=3 && argc!=5) return 2;
    SkeletonRoom room; std::string error; assert(room.load(".",error)); std::filesystem::create_directories(argv[1]);
    for (bool male:{true,false}) {
        const auto& copy=room.comparison(male); const std::string sex=male?"male":"female";
        if (argc>=3) {
            auto p=copy.profile(); p.height=std::stod(argv[2]);
            if (argc==5) {
                const std::string selection=argv[3], endpoint=argv[4];
                assert(endpoint=="lower" || endpoint=="upper");
                for (int dim=SkeletonComparison::Head;dim<SkeletonComparison::Count;++dim) {
                    if (selection!="combined" && std::stoi(selection)!=dim) continue;
                    const auto limits=copy.dimensionLimits(dim,p.height); assert(limits);
                    p.multiplier[dim]=(endpoint=="lower"?limits->minimum:limits->maximum)/limits->center;
                }
            }
            assert(room.setComparison(male,p,error));
        }
        const auto mesh=copy.toeSurface();
        for (int side=0;side<2;++side) for (int digit=0;digit<5;++digit) {
            const int start=(side*5+digit)*80, first=(side*5+digit)*156;
            std::vector<Vec3> vertices(mesh.skin.begin()+start,mesh.skin.begin()+start+80);
            std::vector<std::array<int,3>> faces;
            for (int i=first;i<first+156;++i) {
                auto f=mesh.skinFaces[i]; for (int& v:f) v-=start; faces.push_back(f);
            }
            const auto tag=std::string(side?"_R":"_L"), n=std::to_string(digit+1);
            const auto stem=std::filesystem::path(argv[1])/(sex+"_"+n+tag);
            write(stem.string()+"_skin.obj",vertices,faces);
            const int nailStart=(side*5+digit)*6, nailFirst=(side*5+digit)*4;
            std::vector<Vec3> nail(mesh.nails.begin()+nailStart,mesh.nails.begin()+nailStart+6);
            std::vector<std::array<int,3>> nailFaces;
            for (int i=nailFirst;i<nailFirst+4;++i) {
                auto f=mesh.nailFaces[i]; for (int& v:f) v-=nailStart; nailFaces.push_back(f);
            }
            write(stem.string()+"_nail.obj",nail,nailFaces);
            for (const std::string& segment:{std::string("FootProxP"),std::string("FootMidP"),std::string("FootDistP")}) {
                const std::string name=segment+n+tag; const SkeletonComparison::Part* found=nullptr;
                for (const auto& part:copy.parts()) if (part.name==name) { found=&part; break; }
                if (!found) { assert(digit==0 && segment=="FootMidP"); continue; }
                std::vector<Vec3> bone; for (Vec3 p:found->points) bone.push_back(copy.mapPart(p,*found));
                write(stem.string()+"_"+segment+".obj",bone,found->faces);
            }
        }
    }
}
