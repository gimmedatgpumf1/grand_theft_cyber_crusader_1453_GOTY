// Test bone-relative weighted deformation using the actual toe joints. These
// points are probes, not a finished foot or a claim about anatomical shape.
#include "client/skeleton_room.h"
#include "client/skeleton_skinning.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
using namespace gtc::native;
using Matrix=SkeletonSkinBinding::Matrix;
static Vec3 point(const Matrix& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Vec3 center(const SkeletonModel& model,int index) {
    const auto& part=model.sourceStructures()[index]; Vec3 p;
    for (std::size_t i=0;i<part.pointCount;++i) p+=model.bones()[part.group].points[part.firstPoint+i];
    assert(part.pointCount); return p/static_cast<double>(part.pointCount);
}
static std::size_t link(const SkeletonModel& model,const std::string& a,const std::string& b) {
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& e=model.sourceLinks()[i];
        if ((model.sourceStructures()[e.first].name==a && model.sourceStructures()[e.second].name==b) ||
            (model.sourceStructures()[e.first].name==b && model.sourceStructures()[e.second].name==a)) return i;
    }
    assert(false); return 0;
}
static SkinVertex single(Vec3 p,int bone) {
    SkinVertex v; v.position=p; v.count=1; v.influences[0]={static_cast<std::size_t>(bone),1}; return v;
}
static void check(SkeletonRoom& room,bool male) {
    SkeletonModel& model=male?room.male():room.female(); std::string error;
    int joints=0;
    for (char side:{'L','R'}) for (int digit=1;digit<=5;++digit) for (int joint=0;joint<(digit==1?2:3);++joint) {
        assert(room.restoreReferencePose());
        const auto tag=std::to_string(digit)+"_"+side;
        const std::string meta="FootMeta"+tag,prox="FootProxP"+tag;
        const std::string mid="FootMidP"+tag,dist="FootDistP"+tag;
        const std::string parent=joint==0?meta:(joint==1?prox:mid);
        const std::string child=joint==0?prox:(joint==1 && digit!=1?mid:dist);
        const auto edge=link(model,parent,child); const auto angle=model.referenceBend(edge); assert(angle);
        const int ip=model.sourceStructure(prox),it=model.sourceStructure(dist);
        const int in=model.sourceStructure("FootDistP"+std::to_string(digit==5?4:digit+1)+"_"+side);
        assert(ip>=0 && it>=0 && in>=0);
        const auto before=model.sourceMatrices({},0);
        // Probe at the real bone, plus a point off its dorsal surface for a
        // future nail. The same binding must work for either kind of mesh.
        const Vec3 tip=point(before[it],center(model,it));
        const Vec3 base=point(before[ip],center(model,ip));
        const Vec3 other=point(before[in],center(model,in));
        const Vec3 nail=tip+Vec3{0,.003,0};
        SkinVertex blend; blend.position=(base+tip)*.5; blend.count=2;
        blend.influences[0]={static_cast<std::size_t>(ip),.5};
        blend.influences[1]={static_cast<std::size_t>(it),.5};
        std::vector<SkinVertex> probes={single(base,ip),single(tip,it),single(other,in),single(nail,it),blend};
        SkeletonSkinBinding binding; assert(binding.bind(probes,before,error));
        std::vector<Vec3> original; assert(binding.deform(before,original,error));
        for (std::size_t i=0;i<probes.size();++i) assert(length(original[i]-probes[i].position)<1e-9);
        const auto& e=model.sourceLinks()[edge];
        const auto limits=referenceBendRange(e.mobility,parent,child); assert(limits);
        const double delta=*angle+5<=limits->flexion?5:-5;
        assert(model.setReferenceBend(edge,*angle+delta)); const auto after=model.sourceMatrices({},0);
        std::vector<Vec3> moved; assert(binding.deform(after,moved,error));
        const Vec3 expectedTip=point(after[it],center(model,it));
        assert(length(moved[1]-expectedTip)<1e-9 && length(moved[1]-original[1])>1e-6);
        assert(length(moved[2]-original[2])<1e-8); // another toe does not follow
        if (joint>0) assert(length(moved[0]-original[0])<1e-8); // proximal segment remains attached
        // Blended points are exactly the weighted sum of the two individual bone transforms.
        SkeletonSkinBinding separately;
        assert(separately.bind({single(blend.position,ip),single(blend.position,it)},before,error));
        std::vector<Vec3> components; assert(separately.deform(after,components,error));
        assert(length(moved[4]-(components[0]+components[1])*.5)<1e-9);
        assert(length(moved[3]-original[3])>1e-6); ++joints;
    }
    assert(joints==28); assert(room.restoreReferencePose());
    std::printf("%s: real toe bones, independent joints and weighted attachment: %d PASS\n",male?"male":"female",joints);
}
static void ragdoll(SkeletonRoom& room) {
    std::string error; assert(room.restoreReferencePose());
    std::array<SkeletonSkinBinding,2> bindings;
    std::array<std::vector<SkinVertex>,2> probes;
    std::array<std::vector<int>,2> bones;
    std::array<std::vector<Vec3>,2> local;
    for (int sex=0;sex<2;++sex) {
        auto& model=sex==0?room.male():room.female(); const auto before=model.sourceMatrices({},0);
        for (char side:{'L','R'}) for (int digit=1;digit<=5;++digit) {
            const int index=model.sourceStructure("FootDistP"+std::to_string(digit)+"_"+side); assert(index>=0);
            const Vec3 p=center(model,index)+Vec3{0,.003,0};
            local[sex].push_back(p); bones[sex].push_back(index); probes[sex].push_back(single(point(before[index],p),index));
        }
        assert(bindings[sex].bind(probes[sex],before,error));
    }
    assert(room.toggleRagdoll()); room.advanceRagdoll(.1);
    for (int sex=0;sex<2;++sex) {
        const auto& model=sex==0?room.male():room.female();
        const auto after=model.sourceMatrices({},0); std::vector<Vec3> moved;
        assert(bindings[sex].deform(after,moved,error)); bool motion=false;
        for (std::size_t i=0;i<moved.size();++i) {
            assert(length(moved[i]-point(after[bones[sex][i]],local[sex][i]))<1e-8);
            motion=motion || length(moved[i]-probes[sex][i].position)>1e-5;
        }
        assert(motion);
    }
    assert(room.toggleRagdoll());
    std::puts("Actual male/female ragdoll toe matrices and bound probes: PASS");
}
int main() {
    std::string error; SkeletonRoom room; assert(room.load(".",error));
    check(room,true); check(room,false); ragdoll(room);
    // Invalid weights and matrices fail without replacing the prior binding or output.
    auto pose=room.male().sourceMatrices({},0); SkeletonSkinBinding binding;
    SkinVertex v=single({0,0,0},0); assert(binding.bind({v},pose,error));
    v.influences[0].weight=.5; assert(!binding.bind({v},pose,error) && binding.size()==1);
    v=single({0,0,0},pose.size()); assert(!binding.bind({v},pose,error));
    v=single({0,0,0},0); v.influences[0].weight=std::numeric_limits<double>::quiet_NaN(); assert(!binding.bind({v},pose,error));
    pose[0][0]=2; std::vector<Vec3> output={{1,2,3}};
    assert(!binding.deform(pose,output,error)); assert(output.size()==1 && output[0].x==1);
    std::puts("Bone skin attachment and invalid-input checks: PASS (no rendered foot implied)");
}
