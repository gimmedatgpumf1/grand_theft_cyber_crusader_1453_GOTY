// Check the actual hip, knee and ankle centres in both registered subjects.
#include "client/skeleton_room.h"
#include "client/skeleton_leg_frame.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace gtc::native;

static Vec3 point(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Vec3 center(const SkeletonModel& model, int i) {
    const auto& s=model.sourceStructures()[i]; Vec3 sum;
    for (std::size_t j=0;j<s.pointCount;++j) sum+=model.bones()[s.group].points[s.firstPoint+j];
    return sum/static_cast<double>(s.pointCount);
}
static std::size_t hipLink(const SkeletonModel& model, const std::string& name) {
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& e=model.sourceLinks()[i];
        if (e.mobility=="hip_ball_socket" && (model.sourceStructures()[e.first].name==name ||
                                               model.sourceStructures()[e.second].name==name)) return i;
    }
    assert(false); return 0;
}
static double partBottom(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, const std::string& name) {
    const int i=model.sourceStructure(name); assert(i>=0);
    const auto& s=model.sourceStructures()[i]; double y=1e9;
    for (std::size_t j=0;j<s.pointCount;++j) y=std::min(y,point(m[i],model.bones()[s.group].points[s.firstPoint+j]).y);
    return y;
}
static void checkFootVolumes(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, const char* sex) {
    const std::string path=std::string("tools/skeleton-source/skeleton-")+sex+".lines";
    SkeletonLines lines; std::string error; assert(lines.load(path,error) && lines.bind(model,error));
    Rasterizer out(640,480,colorHex("#000000"),400,true); lines.draw(out,{},0,9001,m);
    const auto scene=out.takeGpuScene(); assert(!scene.batches.empty());
    const auto& vertices=scene.batches.back().vertices;
    std::ifstream file(path); assert(file); std::string row; std::size_t offset=0; int feet=0;
    while (std::getline(file,row)) {
        const bool foot=row.rfind("F ",0)==0; if (!foot && row.rfind("M ",0)!=0) continue;
        std::istringstream in(row); char type; std::string name; in >> type >> name;
        int count=10, faces=0, edges=0; double thickness=0;
        if (!foot) in >> thickness >> count >> faces >> edges;
        if (foot) {
            assert(offset+10<=vertices.size()); const double y=vertices[offset].y;
            for (int i : {1,4,5}) assert(std::abs(vertices[offset+i].y-y)<1e-6);
            const double heel=partBottom(model,m,std::string("Cal_")+name.back());
            assert(std::abs(y-heel)<1e-6); ++feet;
        }
        offset+=foot ? 10 : 2*count;
    }
    assert(feet==2);
}
static double footBottom(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, char side) {
    const std::string suffix=std::string("_")+side; double low=1e9;
    for (std::size_t i=0;i<model.sourceStructures().size();++i) {
        const auto& s=model.sourceStructures()[i];
        if (s.name.size()<2 || s.name.substr(s.name.size()-2)!=suffix) continue;
        if (s.name.rfind("Foot",0)!=0 && s.name.rfind("Cal_",0)!=0 && s.name.rfind("Tal_",0)!=0 &&
            s.name.rfind("Nav_",0)!=0 && s.name.rfind("Cub_",0)!=0 && s.name.rfind("Cune",0)!=0) continue;
        for (std::size_t j=0;j<s.pointCount;++j)
            low=std::min(low,point(m[i],model.bones()[s.group].points[s.firstPoint+j]).y);
    }
    assert(low<1e8); return low;
}
static void check(SkeletonRoom& room,SkeletonModel& model,const char* name) {
    assert(room.restoreOriginalPose()); auto source=model.sourceMatrices({},0);
    assert(room.restoreReferencePose()); auto posed=model.sourceMatrices({},0);
    double bottoms[2];
    for (int side=0;side<2;++side) {
        const char letter=side==0 ? 'L' : 'R'; const std::string tag=std::string("_")+letter;
        const int femur=model.sourceStructure("Femur"+tag),tibia=model.sourceStructure("Tibia"+tag);
        const int heel=model.sourceStructure("Cal"+tag),met=model.sourceStructure("FootMeta3"+tag);
        const auto frame=sourceLegFrame(model,side); assert(frame);
        const Vec3 hip=point(posed[femur],frame->hip), knee=point(posed[femur],frame->knee);
        const Vec3 kneeOnShin=point(posed[tibia],frame->knee), ankle=point(posed[tibia],frame->ankle);
        const Vec3 upper=normalized(knee-hip), lower=normalized(ankle-knee);
        assert(length(knee-kneeOnShin)<1e-7); // both pieces remain at the same joint
        assert(dot(upper,{0,-1,0})>0.999999999 && dot(lower,{0,-1,0})>0.999999999);
        assert(std::abs(hip.x-ankle.x)<1e-7 && std::abs(hip.z-ankle.z)<1e-7);
        const Vec3 foot=normalized(point(posed[met],center(model,met))-point(posed[heel],center(model,heel)));
        assert(std::abs(foot.x)<1e-7 && foot.z>0.98);
        const Vec3 sourceUpper=normalized(point(source[femur],frame->knee)-point(source[femur],frame->hip));
        const Vec3 sourceLower=normalized(point(source[tibia],frame->ankle)-point(source[tibia],frame->knee));
        std::printf("%s %c source thigh/shin off vertical: %.2f/%.2f degrees; reference: %.2f/%.2f\n",name,letter,
            std::acos(std::clamp(dot(sourceUpper,{0,-1,0}),-1.0,1.0))*180/3.141592653589793,
            std::acos(std::clamp(dot(sourceLower,{0,-1,0}),-1.0,1.0))*180/3.141592653589793,
            std::acos(std::clamp(dot(upper,{0,-1,0}),-1.0,1.0))*180/3.141592653589793,
            std::acos(std::clamp(dot(lower,{0,-1,0}),-1.0,1.0))*180/3.141592653589793);
        const double heelBottom=partBottom(model,posed,"Cal"+tag);
        const double outerBottom=partBottom(model,posed,"FootMeta5"+tag);
        const double innerBottom=std::min(partBottom(model,posed,"FootSesaMed"+tag),partBottom(model,posed,"FootSesaLat"+tag));
        assert(std::abs(heelBottom-outerBottom)<1e-5 && std::abs(heelBottom-innerBottom)<1e-5);
        bottoms[side]=footBottom(model,posed,letter);
        const auto hipEdge=hipLink(model,"Femur"+tag);
        assert(model.referenceHipAbduction(hipEdge) && std::abs(*model.referenceHipAbduction(hipEdge))<1e-8);
        assert(model.setReferenceHipAbduction(hipEdge,15)); auto moved=model.sourceMatrices({},0);
        const Vec3 movedHip=point(moved[femur],frame->hip), movedKnee=point(moved[femur],frame->knee);
        assert(length(movedHip-hip)<1e-8);
        assert((movedKnee.x-knee.x)*(side==0 ? 1 : -1)>0.05); // abduct away from midline
        assert(length(point(moved[tibia],frame->knee)-movedKnee)<1e-7);
        const int other=model.sourceStructure(std::string("Femur_")+(side==0 ? "R" : "L"));
        for (int i=0;i<16;++i) assert(std::abs(moved[other][i]-posed[other][i])<1e-8);
        assert(model.setReferenceHipAbduction(hipEdge,100)); assert(std::abs(*model.referenceHipAbduction(hipEdge)-45)<1e-8);
        assert(model.setReferenceHipAbduction(hipEdge,-100)); assert(std::abs(*model.referenceHipAbduction(hipEdge)+30)<1e-8);
        assert(room.restoreReferencePose()); posed=model.sourceMatrices({},0);
    }
    checkFootVolumes(model,posed,name);
    std::printf("%s ungrounded foot bottoms L/R: %.4f/%.4f m, difference %.1f mm\n",name,bottoms[0],bottoms[1],std::abs(bottoms[0]-bottoms[1])*1000);
    assert(std::abs(bottoms[0]-bottoms[1])<0.003); // measured source asymmetry, no scaling
    // The actual left/right hip rotations mirror in the coronal plane.
    for (double angle : {-30.0,15.0,45.0}) {
        std::array<std::array<double,9>,2> delta{};
        for (int side=0;side<2;++side) {
            assert(room.restoreReferencePose());
            const std::string femur=std::string("Femur_")+(side==0 ? "L" : "R");
            const int i=model.sourceStructure(femur); const auto before=model.sourceMatrices({},0);
            assert(model.setReferenceHipAbduction(hipLink(model,femur),angle));
            const auto after=model.sourceMatrices({},0);
            for (int r=0;r<3;++r) for (int c=0;c<3;++c) for (int k=0;k<3;++k)
                delta[side][r*3+c]+=after[i][k*4+r]*before[i][k*4+c];
        }
        const double mirror[]={-1,1,1};
        for (int r=0;r<3;++r) for (int c=0;c<3;++c)
            assert(std::abs(delta[1][r*3+c]-mirror[r]*delta[0][r*3+c]*mirror[c])<1e-7);
    }
    assert(room.restoreOriginalPose());
    const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for (const auto& m:model.sourceMatrices({},0)) for (int i=0;i<16;++i) assert(std::abs(m[i]-identity[i])<1e-7);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    check(room,room.male(),"male"); check(room,room.female(),"female");
    std::puts("Straight bilateral joint-centre legs and hip abduction: PASS");
}
