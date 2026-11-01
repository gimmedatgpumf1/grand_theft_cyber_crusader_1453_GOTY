// Output actual room source matrices for an optional source-triangle audit.
#include "client/skeleton_room.h"
#include <iomanip>
#include <iostream>
#include <string>
using namespace gtc::native;
int main() {
    SkeletonRoom room; std::string error;
    if (!room.load(".",error)) { std::cerr << error << '\n'; return 1; }
    std::cout << std::setprecision(17);
    for (auto item : {std::pair<const char*,SkeletonModel*>{"male",&room.male()},
                      {"female",&room.female()}}) {
        auto& model=*item.second;
        for (std::size_t i=0;i<model.sourceLinks().size();++i) {
            const auto& link=model.sourceLinks()[i];
            if (!model.referenceBend(i)) continue;
            if (link.mobility=="shoulder_ball_socket" && !model.setReferenceBend(i,90)) return 1;
            if (link.mobility=="elbow_hinge" && !model.setReferenceBend(i,0)) return 1;
            if (link.mobility=="knee_modified_hinge" && !model.setReferenceBend(i,0)) return 1;
        }
        const auto matrices=model.sourceMatrices({},0);
        for (std::size_t i=0;i<matrices.size();++i) {
            std::cout << item.first << ' ' << model.sourceStructures()[i].name;
            for (double value : matrices[i]) std::cout << ' ' << value;
            std::cout << '\n';
        }
    }
}
