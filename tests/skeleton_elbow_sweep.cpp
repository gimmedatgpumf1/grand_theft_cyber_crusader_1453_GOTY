// Emit actual room-rig matrices for the optional source-triangle elbow audit.
#include "client/skeleton_room.h"
#include <iomanip>
#include <iostream>
#include <string>
using namespace gtc::native;
int main() {
    SkeletonRoom room; std::string error;
    if (!room.load(".",error)) { std::cerr << error << '\n'; return 1; }
    std::cout << std::setprecision(17);
    for (auto entry : {std::pair<const char*,SkeletonModel*>{"male",&room.male()},
                       {"female",&room.female()}}) {
        auto& model=*entry.second;
        for (char side : {'L','R'}) {
            const auto suffix=std::string("_")+side;
            const auto h=model.sourceStructure("Humerus"+suffix);
            const auto u=model.sourceStructure("Ulna"+suffix);
            const auto r=model.sourceStructure("Radius"+suffix);
            if (h<0 || u<0 || r<0) return 1;
            std::size_t link=0;
            for (;link<model.sourceLinks().size();++link)
                if (model.sourceLinks()[link].first==static_cast<std::size_t>(h) &&
                    model.sourceLinks()[link].second==static_cast<std::size_t>(u)) break;
            if (link==model.sourceLinks().size()) return 1;
            for (int angle=0;angle<=150;++angle) {
                if (!model.setReferenceBend(link,angle)) return 1;
                const auto matrices=model.sourceMatrices({},0);
                for (int index : {h,u,r}) {
                    std::cout << entry.first << ' ' << side << ' ' << angle << ' '
                              << model.sourceStructures()[index].name;
                    for (double x : matrices[index]) std::cout << ' ' << x;
                    std::cout << '\n';
                }
            }
            if (!model.restoreSourcePose()) return 1;
        }
    }
}
