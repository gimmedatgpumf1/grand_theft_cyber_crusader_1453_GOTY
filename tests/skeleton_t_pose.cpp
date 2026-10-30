// Actual room skeletons: reference pose, controls, joint direction and rigid geometry.
#include "client/skeleton_room.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace gtc::native;

static Vec3 point(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Vec3 center(const SkeletonModel& model, const std::string& name) {
    int i=model.sourceStructure(name); assert(i>=0);
    const auto& part=model.sourceStructures()[i]; const auto& points=model.bones()[part.group].points;
    Vec3 sum; for (std::size_t j=0;j<part.pointCount;++j) sum+=points[part.firstPoint+j];
    return sum/static_cast<double>(part.pointCount);
}
static Vec3 posed(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, const std::string& name) {
    return point(m[model.sourceStructure(name)],center(model,name));
}
static std::size_t link(const SkeletonModel& model, const std::string& child, const std::string& kind) {
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& e=model.sourceLinks()[i];
        if (e.mobility==kind && (model.sourceStructures()[e.first].name==child ||
                                 model.sourceStructures()[e.second].name==child) && model.referenceBend(i)) return i;
    }
    assert(false); return 0;
}
static void verifyPose(SkeletonModel& model) {
    auto m=model.sourceMatrices({},0);
    for (char side : {'L','R'}) {
        const auto tag=std::string("_")+side;
        for (const auto& pair : {std::pair<const char*,const char*>{"Humerus","shoulder_ball_socket"},
                                 {"Ulna","elbow_hinge"},{"Tibia","knee_modified_hinge"},
                                 {"Tal","ankle_hinge"},{"HandScaphoid","wrist_condyloid"}}) {
            const auto i=link(model,std::string(pair.first)+tag,pair.second);
            assert(std::abs(*model.referenceBend(i)-(std::string(pair.second)=="shoulder_ball_socket" ? 90 : 0))<1e-7);
        }
        const int h=model.sourceStructure("Humerus"+tag);
        const int b=model.bone(side=='L' ? "humerus_l" : "humerus_r");
        const Vec3 shoulder=point(m[h],model.bones()[b].pivot);
        const Vec3 elbow=point(m[h],model.elbowAxes()[side=='L' ? 0 : 1].origin);
        const Vec3 upper=normalized(elbow-shoulder), target=side=='L' ? Vec3{1,0,0}:Vec3{-1,0,0};
        assert(dot(upper,target)>0.99999);
        const Vec3 fore=normalized(posed(model,m,"Ulna"+tag)-elbow);
        assert(dot(fore,target)>std::cos(20*3.141592653589793/180));
        const Vec3 palm=normalized(cross(posed(model,m,"HandMeta2"+tag)-posed(model,m,"HandMeta5"+tag),
                                         posed(model,m,"HandMeta3"+tag)-posed(model,m,"HandLuna"+tag)))*(side=='L' ? -1.0 : 1.0);
        assert(palm.y < -0.93); // both palms face down in the reference pose
        const Vec3 foot=normalized(posed(model,m,"FootMeta3"+tag)-posed(model,m,"Cal"+tag));
        const Vec3 shin=normalized(posed(model,m,"Tibia"+tag)-posed(model,m,"Cal"+tag));
        assert(std::abs(dot(foot,shin))<0.03 && foot.z>0.9);
        const auto hip=link(model,"Femur"+tag,"hip_ball_socket");
        assert(model.referenceHipRotation(hip) && std::abs(*model.referenceHipRotation(hip))<=45.00001);
    }
    for (const auto& matrix : m) for (int col=0;col<3;++col)
        assert(std::abs(std::sqrt(matrix[col*4]*matrix[col*4]+matrix[col*4+1]*matrix[col*4+1]+
                                  matrix[col*4+2]*matrix[col*4+2])-1)<1e-8);
}
static Vec3 palmNormal(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, char side) {
    const auto tag=std::string("_")+side;
    return normalized(cross(posed(model,m,"HandMeta2"+tag)-posed(model,m,"HandMeta5"+tag),
                            posed(model,m,"HandMeta3"+tag)-posed(model,m,"HandLuna"+tag)))*(side=='L'?-1.0:1.0);
}
// Mirroring a rotation across the sagittal plane conjugates its matrix by
// diag(-1,1,1). Check the real forearm matrices, not just stored angles.
static void verifyMirroredElbows(SkeletonRoom& room, SkeletonModel& model) {
    for (double angle : {10.0,45.0,90.0,135.0,150.0}) {
        std::array<std::array<double,9>,2> delta{};
        for (int side=0;side<2;++side) {
            assert(room.restoreReferencePose());
            const auto suffix=side==0 ? "_L" : "_R";
            const auto index=model.sourceStructure(std::string("Ulna")+suffix);
            const auto radius=model.sourceStructure(std::string("Radius")+suffix);
            const auto before=model.sourceMatrices({},0);
            assert(model.setReferenceBend(link(model,std::string("Ulna")+suffix,"elbow_hinge"),angle));
            const auto after=model.sourceMatrices({},0);
            for (int i=0;i<16;++i) assert(std::abs(after[index][i]-after[radius][i])<1e-7);
            for (int r=0;r<3;++r) for (int c=0;c<3;++c)
                for (int k=0;k<3;++k) delta[side][r*3+c]+=after[index][k*4+r]*before[index][k*4+c];
        }
        const double mirror[]={-1,1,1};
        for (int r=0;r<3;++r) for (int c=0;c<3;++c)
            assert(std::abs(delta[1][r*3+c]-mirror[r]*delta[0][r*3+c]*mirror[c])<1e-7);
    }
    assert(room.restoreReferencePose());
}
static void verifyMirroredHips(SkeletonRoom& room, SkeletonModel& model) {
    for (double angle : {-20.0,30.0,90.0,120.0}) {
        std::array<std::array<double,9>,2> delta{};
        for (int side=0;side<2;++side) {
            assert(room.restoreReferencePose());
            const auto name=std::string("Femur_")+(side==0 ? "L" : "R");
            const auto index=model.sourceStructure(name); const auto edge=link(model,name,"hip_ball_socket");
            const auto before=model.sourceMatrices({},0);
            assert(model.setReferenceBend(edge,angle));
            const auto after=model.sourceMatrices({},0);
            for (int r=0;r<3;++r) for (int c=0;c<3;++c)
                for (int k=0;k<3;++k) delta[side][r*3+c]+=after[index][k*4+r]*before[index][k*4+c];
        }
        const double mirror[]={-1,1,1};
        for (int r=0;r<3;++r) for (int c=0;c<3;++c)
            assert(std::abs(delta[1][r*3+c]-mirror[r]*delta[0][r*3+c]*mirror[c])<1e-7);
    }
    assert(room.restoreReferencePose());
}
static void verifyMirroredForearms(SkeletonRoom& room, SkeletonModel& model) {
    for (double angle : {-80.0,-40.0,40.0,80.0,200.0}) {
        std::array<std::array<double,9>,2> delta{};
        for (int side=0;side<2;++side) {
            assert(room.restoreReferencePose());
            const auto suffix=side==0 ? "_L" : "_R";
            const auto ulna=model.sourceStructure(std::string("Ulna")+suffix);
            const auto radius=model.sourceStructure(std::string("Radius")+suffix);
            const auto edge=link(model,std::string("Radius")+suffix,"elbow_hinge");
            assert(model.setReferenceForearmRotation(edge,0));
            const auto before=model.sourceMatrices({},0);
            assert(model.setReferenceForearmRotation(edge,angle));
            assert(std::abs(*model.referenceForearmRotation(edge)-std::clamp(angle,-80.0,80.0))<1e-7);
            const auto after=model.sourceMatrices({},0);
            for (int i=0;i<16;++i) assert(std::abs(after[ulna][i]-after[radius][i])<1e-7);
            for (int r=0;r<3;++r) for (int c=0;c<3;++c)
                for (int k=0;k<3;++k) delta[side][r*3+c]+=after[ulna][k*4+r]*before[ulna][k*4+c];
        }
        const double mirror[]={-1,1,1};
        for (int r=0;r<3;++r) for (int c=0;c<3;++c)
            assert(std::abs(delta[1][r*3+c]-mirror[r]*delta[0][r*3+c]*mirror[c])<1e-7);
    }
    assert(room.restoreReferencePose());
}
static void verifyDirections(SkeletonRoom& room, SkeletonModel& model, const char* subject) {
    for (char side : {'L','R'}) {
        const auto tag=std::string("_")+side;
        for (const auto& spec : {std::array<const char*,3>{"Femur","hip_ball_socket","Tibia"},
                                 {"Ulna","elbow_hinge","HandLuna"}, {"Tibia","knee_modified_hinge","Cal"},
                                 {"Tal","ankle_hinge","FootMeta3"}}) {
            assert(room.restoreReferencePose());
            const auto before=model.sourceMatrices({},0);
            const auto joint=link(model,std::string(spec[0])+tag,spec[1]);
            const Vec3 start=posed(model,before,std::string(spec[2])+tag);
            assert(model.setReferenceBend(joint,30));
            const Vec3 delta=posed(model,model.sourceMatrices({},0),std::string(spec[2])+tag)-start;
            if (std::string(spec[1])=="hip_ball_socket") assert(delta.z>0.1);
            else if (std::string(spec[1])=="elbow_hinge") {
                assert(delta.z>0.02 && delta.x*(side=='L' ? -1 : 1)>0.02);
            } else if (std::string(spec[1])=="knee_modified_hinge") assert(delta.z< -0.1);
            else assert(delta.y< -0.02);
        }
        assert(room.restoreReferencePose());
        auto before=model.sourceMatrices({},0); Vec3 normal=palmNormal(model,before,side);
        auto wrist=link(model,"HandScaphoid"+tag,"wrist_condyloid");
        Vec3 start=posed(model,before,"HandDistP3"+tag);
        assert(model.setReferenceBend(wrist,30));
        assert(dot(posed(model,model.sourceMatrices({},0),"HandDistP3"+tag)-start,normal)>0.01);
        for (int digit=1;digit<=5;++digit) {
            assert(room.restoreReferencePose()); before=model.sourceMatrices({},0); normal=palmNormal(model,before,side);
            const std::string token="P"+std::to_string(digit)+tag, tip="HandDist"+token;
            start=posed(model,before,tip); int joints=0;
            for (std::size_t i=0;i<model.sourceLinks().size();++i) {
                const auto& edge=model.sourceLinks()[i];
                const auto& a=model.sourceStructures()[edge.first].name; const auto& b=model.sourceStructures()[edge.second].name;
                if ((a.find(token)!=std::string::npos || b.find(token)!=std::string::npos) && model.referenceBend(i) &&
                    (edge.mobility=="thumb_mcp" || edge.mobility=="thumb_ip_hinge" ||
                     edge.mobility=="finger_mcp_condyloid" || edge.mobility=="finger_pip_hinge" || edge.mobility=="finger_dip_hinge")) {
                    assert(model.setReferenceBend(i,10)); ++joints;
                }
            }
            assert(joints==(digit==1 ? 2 : 3));
            assert(dot(posed(model,model.sourceMatrices({},0),tip)-start,normal)>0.0005);
        }
        for (int digit=1;digit<=5;++digit) {
            assert(room.restoreReferencePose()); before=model.sourceMatrices({},0);
            const std::string tip="FootDistP"+std::to_string(digit)+tag;
            start=posed(model,before,tip);
            assert(room.adjustReferenceGroup(digit==5 ? '0' : '5'+digit,25));
            assert(posed(model,model.sourceMatrices({},0),tip).y-start.y< -0.001);
        }
        assert(room.restoreReferencePose());
        const auto hip=link(model,"Femur"+tag,"hip_ball_socket");
        assert(model.setReferenceHipRotation(hip,100)); assert(std::abs(*model.referenceHipRotation(hip)-45)<1e-8);
        assert(model.setReferenceHipRotation(hip,-100)); assert(std::abs(*model.referenceHipRotation(hip)+45)<1e-8);
    }
    std::printf("%s bilateral elbows, hips, knees, ankles, wrists, fingers and toes: directions PASS\n",subject);
}
static double lowMale,lowFemale;
static bool capture(const GpuScene& scene, NativeFrame&) {
    lowMale=lowFemale=1e9;
    for (const auto& batch:scene.batches) {
        if (batch.objectId<8001 || batch.objectId>=9183) continue;
        double& low=batch.objectId>=9001 ? lowMale : lowFemale;
        for (const auto& v:batch.vertices) low=std::min(low,point(batch.modelMatrix,{v.x,v.y,v.z}).y);
    }
    return true;
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    verifyPose(room.male()); verifyPose(room.female());
    setGpuRenderer(&capture); ViewState view; view.gpu=true; view.width=640; view.height=480;
    NativeFrame frame; room.render(frame,view,0); setGpuRenderer(nullptr);
    assert(std::abs(lowMale)<1e-5 && std::abs(lowFemale)<1e-5);
    const auto male=room.male().sourceMatrices({},0), female=room.female().sourceMatrices({},0);
    verifyMirroredElbows(room,room.male()); verifyMirroredElbows(room,room.female());
    verifyMirroredForearms(room,room.male()); verifyMirroredForearms(room,room.female());
    verifyMirroredHips(room,room.male()); verifyMirroredHips(room,room.female());
    verifyDirections(room,room.male(),"male"); verifyDirections(room,room.female(),"female");
    assert(room.restoreOriginalPose());
    for (const auto* model : {&room.male(),&room.female()}) for (const auto& m:model->sourceMatrices({},0)) {
        const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (int i=0;i<16;++i) assert(std::abs(m[i]-identity[i])<1e-7);
    }
    assert(room.restoreReferencePose());
    for (const auto& pair : {std::pair<const SkeletonModel*,const std::vector<std::array<double,16>>*>{&room.male(),&male},
                             {&room.female(),&female}}) {
        const auto actual=pair.first->sourceMatrices({},0);
        for (std::size_t i=0;i<actual.size();++i) for (int j=0;j<16;++j)
            assert(std::abs(actual[i][j]-(*pair.second)[i][j])<1e-7);
    }
    std::puts("Room default pose, mirrored elbows/forearms/hips, floor and unscaled restoration: PASS");
}
