// Export the exact per-structure transforms from the room's real T-pose.
// Geometry and joint fits are loaded through SkeletonRoom; no posing or
// scaling is duplicated in this measurement utility.
#include "client/skeleton_room.h"
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
using namespace gtc::native;
int main(int argc, char** argv) {
    const bool source=argc==2 && std::string(argv[1])=="--source";
    if (argc>2 || (argc==2 && !source)) { std::cerr << "usage: pose-export [--source]\n"; return 1; }
    SkeletonRoom room; std::string error;
    if (!room.load(".",error) || !(source ? room.restoreOriginalPose() : room.restoreReferencePose())) {
        std::cerr << "Unable to load requested pose: " << error << '\n'; return 1;
    }
    std::cout << std::setprecision(17);
    for (const auto& item : {std::pair<const char*,SkeletonModel*>{"male",&room.male()},
                             {"female",&room.female()}}) {
        const auto& model=*item.second;
        const auto matrices=model.sourceMatrices({},0);
        if (matrices.size()!=model.sourceStructures().size()) return 1;
        for (std::size_t i=0;i<matrices.size();++i) {
            std::cout << item.first << '\t' << model.sourceStructures()[i].name;
            for (double value : matrices[i]) std::cout << '\t' << value;
            std::cout << '\n';
        }
    }
}
