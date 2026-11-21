// The four paired source labels must share one actual rig body without
// preventing the C-room loader or joint controls from working.
#include "client/skeleton_room.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace gtc::native;

static void checkPair(SkeletonModel& model,const char* primary,const char* alias) {
    const int a=model.sourceStructure(primary),b=model.sourceStructure(alias);
    assert(a>=0 && b>=0);
    const auto& map=model.sourceBodyMap();
    assert(map[static_cast<std::size_t>(a)]==map[static_cast<std::size_t>(b)]);
    assert(model.sourceRig()->joint(map[static_cast<std::size_t>(a)]));
}

static void sameTransform(const SkeletonModel& model,const char* a,const char* b) {
    const int ai=model.sourceStructure(a),bi=model.sourceStructure(b);
    const auto pose=model.sourceMatrices({},0);
    assert(ai>=0 && bi>=0 && pose.size()==model.sourceStructures().size());
    for(std::size_t k=0;k<16;++k)
        assert(std::abs(pose[static_cast<std::size_t>(ai)][k]-pose[static_cast<std::size_t>(bi)][k])<1e-12);
}

int main() {
    SkeletonRoom room; std::string error;
    if(!room.load(".",error)) { std::cerr<<"C-room load failed: "<<error<<'\n'; return 1; }
    for(SkeletonModel* model:{&room.male(),&room.female()}) {
        assert(model->sourceStructures().size()==182);
        assert(model->sourceRig()->size()==179);
        checkPair(*model,"Radius_L","Ulna_L");checkPair(*model,"Radius_R","Ulna_R");
        checkPair(*model,"Tibia_L","Fibula_L");checkPair(*model,"Tibia_R","Fibula_R");
        sameTransform(*model,"Radius_L","Ulna_L");sameTransform(*model,"Radius_R","Ulna_R");
        sameTransform(*model,"Tibia_L","Fibula_L");sameTransform(*model,"Tibia_R","Fibula_R");
    }
    assert(room.adjustReferenceGroup('e',5.0));
    assert(room.adjustReferenceGroup('h',5.0));
    assert(room.adjustReferenceGroup('m',5.0));
    for(SkeletonModel* model:{&room.male(),&room.female()}) {
        sameTransform(*model,"Radius_L","Ulna_L");sameTransform(*model,"Radius_R","Ulna_R");
        sameTransform(*model,"Tibia_L","Fibula_L");sameTransform(*model,"Tibia_R","Fibula_R");
    }
    std::cout<<"Merged forearm/shin rig bodies and C-room controls: PASS\n";
}
