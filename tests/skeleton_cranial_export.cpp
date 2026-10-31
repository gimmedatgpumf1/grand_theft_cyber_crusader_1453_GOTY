// Export the actual neutral source and comparison surfaces for the FCL audit.
#include "client/skeleton_room.h"
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>
using namespace gtc::native;

static Vec3 transform(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
int main(int argc,char** argv) {
    if (argc!=2 && argc!=3 && argc!=5) return 2;
    const bool all=argc==3 && std::string(argv[2])=="--all";
    SkeletonRoom room; std::string error;
    if (!room.load(".",error)) return 1;
    std::filesystem::create_directories(argv[1]);
    if (argc>=3 && !all) for (bool male:{true,false}) {
        auto profile=room.comparison(male).profile(); profile.height=std::atof(argv[2]);
        if (argc==5) {
            const int dimension=std::atoi(argv[3]);
            if (dimension<=SkeletonComparison::Height || dimension>=SkeletonComparison::Count) return 2;
            profile.multiplier[dimension]=std::atof(argv[4]);
        }
        if (!room.setComparison(male,profile,error)) { std::fprintf(stderr,"%s\n",error.c_str()); return 1; }
    }
    for (bool male:{true,false}) {
        const auto& model=male?room.male():room.female();
        const auto& comparison=room.comparison(male);
        const auto matrices=model.sourceMatrices({},0);
        std::vector<std::string> names;
        if (all) {
            for (const auto& source:model.sourceStructures()) {
                const auto& name=source.name;
                if (name=="Sacrum" || name=="Sternum" ||
                    (name.size()>2 && name.substr(name.size()-2)=="_L")) names.push_back(name);
            }
        } else names={"Head","Jaw","C1","C2","C7","Pelvis_L","Pelvis_R","Sacrum","Sternum",
                      "Femur_L","Femur_R","L5","Tibia_L","Tibia_R","Tal_L","Tal_R",
                      "Clavicula_L","Clavicula_R","Scapula_L","Scapula_R","Humerus_L","Humerus_R","Radius_L","Radius_R"};
        for (const std::string& name:names) {
            const int index=model.sourceStructure(name); if (index<0) return 1;
            const auto& part=model.sourceStructures()[index]; const auto& group=model.bones()[part.group];
            for (bool blue:{false,true}) {
                const auto path=std::filesystem::path(argv[1])/
                    (std::string(male?"male_":"female_")+(blue?"blue_":"white_")+name+".obj");
                std::ofstream file(path); if (!file) return 1;
                file<<std::setprecision(12);
                if (blue) {
                    const auto& actual=comparison.parts()[static_cast<std::size_t>(index)];
                    for (Vec3 p:actual.points) {
                        p=comparison.map(p,actual.armSide,actual.cranial,actual.girdle,actual.humerus);
                        file<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
                    }
                    for (const auto& face:actual.faces)
                        file<<"f "<<face[0]+1<<' '<<face[1]+1<<' '<<face[2]+1<<'\n';
                } else {
                    for (std::size_t i=part.firstPoint;i<part.firstPoint+part.pointCount;++i) {
                        Vec3 p=transform(matrices[index],group.points[i]);
                        file<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
                    }
                    for (std::size_t i=part.firstTriangle;i<part.firstTriangle+part.triangleCount;++i) {
                        const auto& face=group.triangles[i];
                        file<<"f "<<face[0]-int(part.firstPoint)+1<<' '<<face[1]-int(part.firstPoint)+1
                            <<' '<<face[2]-int(part.firstPoint)+1<<'\n';
                    }
                }
                if (!file) return 1;
            }
        }
    }
}
