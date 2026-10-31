// Verify actual bilateral SC/AC/shoulder pivots before and after pose changes.
#include "client/skeleton_room.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace gtc::native;

static Vec3 point(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
struct Anchor { std::size_t parent,child; Vec3 pivot; };
static void check(SkeletonRoom& room, SkeletonModel& model, const char* label) {
    assert(room.restoreOriginalPose());
    const auto source=model.sourceMatrices({},0);
    const auto& rig=*model.sourceRig(); const auto& mapping=model.sourceBodyMap();
    std::vector<Anchor> anchors;
    for (const auto& edge:model.sourceLinks()) {
        if (edge.mobility!="sternoclavicular" && edge.mobility!="acromioclavicular" && edge.mobility!="shoulder_ball_socket") continue;
        const auto a=mapping[edge.first],b=mapping[edge.second];
        std::size_t parent=edge.first,child=edge.second;
        if (rig.parent(a) && *rig.parent(a)==b) { parent=edge.second; child=edge.first; }
        const auto p=mapping[parent], c=mapping[child];
        assert(rig.parent(c) && *rig.parent(c)==p && rig.joint(c));
        auto world=joint_detail::compose(rig.absoluteTransforms()[p],rig.joint(c)->parentFrame());
        anchors.push_back({parent,child,world.translation});
    }
    assert(anchors.size()==6);
    assert(room.restoreReferencePose());
    const auto posed=model.sourceMatrices({},0);
    for (const auto& a:anchors) {
        // Original skin transforms are identities; pivot is in source space.
        assert(length(point(source[a.parent],a.pivot)-point(source[a.child],a.pivot))<1e-7);
        assert(length(point(posed[a.parent],a.pivot)-point(posed[a.child],a.pivot))<1e-7);
    }
    for (char side:{'L','R'}) {
        const auto suffix=std::string("_")+side;
        const auto clav=model.sourceStructure("Clavicula"+suffix),scap=model.sourceStructure("Scapula"+suffix);
        for (int i=0;i<16;++i) assert(std::abs(posed[clav][i]-posed[scap][i])<1e-7);
    }
    for (const auto& m:posed) {
        for (int col=0;col<3;++col) {
            const Vec3 v{m[col*4],m[col*4+1],m[col*4+2]};
            assert(std::abs(length(v)-1)<1e-7);
            for (int other=0;other<col;++other) {
                const Vec3 w{m[other*4],m[other*4+1],m[other*4+2]}; assert(std::abs(dot(v,w))<1e-7);
            }
        }
    }
    assert(room.restoreOriginalPose()); const auto restored=model.sourceMatrices({},0);
    for (std::size_t i=0;i<source.size();++i) for (int j=0;j<16;++j) assert(std::abs(source[i][j]-restored[i][j])<1e-7);
    assert(room.restoreReferencePose()); const auto repeated=model.sourceMatrices({},0);
    for (std::size_t i=0;i<posed.size();++i) for (int j=0;j<16;++j) assert(std::abs(posed[i][j]-repeated[i][j])<1e-7);
    std::printf("%s shoulder girdle joint anchors, rigidity and restoration PASS\n",label);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    check(room,room.male(),"male"); check(room,room.female(),"female");
}
