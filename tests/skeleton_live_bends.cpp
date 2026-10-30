// Verify the room's default reference bends on the actual source batches.
// Pivots and neutral are estimated; this is not an individual ROM validation.
#include "client/skeleton_room.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
using namespace gtc::native;

static Vec3 transform(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Vec3 center(const SkeletonModel& model, int index) {
    const auto& part=model.sourceStructures()[static_cast<std::size_t>(index)];
    const auto& points=model.bones()[part.group].points; Vec3 sum;
    for (std::size_t i=0;i<part.pointCount;++i) sum+=points[part.firstPoint+i];
    return sum/static_cast<double>(part.pointCount);
}
static void checkArmAlignment(SkeletonModel& model) {
    // Alignment is only a geometric check; collision audits demonstrate this
    // candidate is not yet suitable as the room's default pose.
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& edge=model.sourceLinks()[i]; if (!model.referenceBend(i)) continue;
        if (edge.mobility=="shoulder_ball_socket") assert(model.setReferenceBend(i,90));
        if (edge.mobility=="elbow_hinge") assert(model.setReferenceBend(i,0));
    }
    const auto matrices=model.sourceMatrices({},0);
    for (char side : {'L','R'}) {
        const std::string suffix=std::string("_")+side;
        const int h=model.sourceStructure("Humerus"+suffix), u=model.sourceStructure("Ulna"+suffix);
        const int group=model.bone(side=='L' ? "humerus_l" : "humerus_r");
        assert(h>=0 && u>=0 && group>=0);
        assert(model.hasElbowAxes());
        const Vec3 shoulder=model.bones()[group].pivot;
        const Vec3 elbow=model.elbowAxes()[side=='L' ? 0 : 1].origin;
        const Vec3 upper=normalized(transform(matrices[h],elbow)-transform(matrices[h],shoulder));
        const Vec3 fore=normalized(transform(matrices[u],center(model,u))-transform(matrices[h],elbow));
        const Vec3 target=side=='L' ? Vec3{1,0,0} : Vec3{-1,0,0};
        // An elbow's carrying angle is not perfectly collinear; this is only
        // a coarse alignment check, not a clinical T-pose certificate.
        assert(dot(upper,target)>0.99999 && dot(fore,target)>std::cos(20*3.141592653589793/180));
    }
    assert(model.restoreSourcePose());
}
static std::uint64_t sceneRevision;
static std::vector<Vec3> maleLineVertices;
static bool captureLines(const GpuScene& scene, NativeFrame&) {
    sceneRevision=scene.revision; maleLineVertices.clear();
    for (const auto& batch : scene.batches) if (batch.objectId==9001)
        for (const auto& vertex : batch.vertices) maleLineVertices.push_back({vertex.x,vertex.y,vertex.z});
    return true;
}
static void checkLinePose(SkeletonRoom& room) {
    room.toggleLineView(); setGpuRenderer(&captureLines);
    ViewState view; view.gpu=true; view.width=640; view.height=480; NativeFrame frame;
    room.render(frame,view,0); auto before=maleLineVertices; auto revision=sceneRevision;
    assert(!before.empty()); room.render(frame,view,0); assert(sceneRevision==revision);
    auto& body=room.male(); std::size_t shoulder=0;
    for (;shoulder<body.sourceLinks().size();++shoulder) {
        const auto& e=body.sourceLinks()[shoulder];
        if (e.mobility=="shoulder_ball_socket" && body.sourceStructures()[e.first].name=="Scapula_L") break;
    }
    assert(shoulder<body.sourceLinks().size()); const double old=*body.referenceBend(shoulder);
    assert(body.setReferenceBend(shoulder,45)); room.render(frame,view,0);
    assert(sceneRevision!=revision && maleLineVertices.size()==before.size());
    std::size_t moved=0; for (std::size_t i=0;i<before.size();++i)
        if (length(before[i]-maleLineVertices[i])>1e-5) ++moved;
    assert(moved>100);
    assert(body.setReferenceBend(shoulder,old)); room.render(frame,view,0);
    for (std::size_t i=0;i<before.size();++i) assert(length(before[i]-maleLineVertices[i])<1e-6);
    setGpuRenderer(nullptr); room.toggleLineView();
}

static std::size_t namedLink(const SkeletonModel& model, const std::string& first,
                             const std::string& second) {
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& link=model.sourceLinks()[i];
        if (model.sourceStructures()[link.first].name==first &&
            model.sourceStructures()[link.second].name==second) return i;
    }
    assert(false); return 0;
}
static void checkGroupedWrist(SkeletonModel& model) {
    assert(model.restoreSourcePose());
    for (const char side : {'L','R'}) {
        const auto suffix=std::string("_")+side;
        const auto sc=namedLink(model,"Radius"+suffix,"HandScaphoid"+suffix);
        const auto lu=namedLink(model,"Radius"+suffix,"HandLuna"+suffix);
        for (std::size_t link : {sc,lu}) {
            assert(model.setReferenceBend(link,45));
            assert(std::abs(*model.referenceBend(sc)-45)<1e-8);
            assert(std::abs(*model.referenceBend(lu)-45)<1e-8);
            const auto matrices=model.sourceMatrices({},0);
            const int palm=model.sourceStructure("HandLuna"+suffix);
            const auto& expected=matrices[static_cast<std::size_t>(palm)];
            // All merged palm pieces, thumb and fingertips follow one wrist
            // transform; radius and ulna remain at the forearm's source pose.
            for (const auto& name : {"HandScaphoid","HandLuna","HandTriquetrum","HandPisiform",
                                     "HandTrapezium","HandTrapezoideum","HandCapitatum","HandHamata",
                                     "HandMeta1","HandMeta2","HandMeta3","HandMeta4","HandMeta5",
                                     "HandDistP1","HandDistP2","HandDistP3","HandDistP4","HandDistP5"}) {
                const int part=model.sourceStructure(std::string(name)+suffix); assert(part>=0);
                for (int j=0;j<16;++j) assert(std::abs(matrices[part][j]-expected[j])<1e-7);
            }
            const int radius=model.sourceStructure("Radius"+suffix), ulna=model.sourceStructure("Ulna"+suffix);
            const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            for (int j=0;j<16;++j) {
                assert(std::abs(matrices[radius][j]-identity[j])<1e-7);
                assert(std::abs(matrices[ulna][j]-identity[j])<1e-7);
            }
            // An individual finger still bends without dragging its neighbour.
            const auto finger=namedLink(model,"HandProxP2"+suffix,"HandMidP2"+suffix);
            const auto beforeFinger=model.referenceBend(finger); assert(beforeFinger);
            const double target=*beforeFinger<50 ? 100 : 0;
            assert(model.setReferenceBend(finger,target));
            const auto moved=model.sourceMatrices({},0);
            const auto index=model.sourceStructure("HandDistP2"+suffix);
            const auto middle=model.sourceStructure("HandDistP3"+suffix);
            bool indexMoved=false;
            for (int j=0;j<16;++j) {
                if (std::abs(moved[index][j]-matrices[index][j])>1e-7) indexMoved=true;
                assert(std::abs(moved[middle][j]-matrices[middle][j])<1e-7);
            }
            assert(indexMoved && model.restoreSourcePose());
        }
    }
}

static void checkPatellaCoupling(SkeletonModel& model) {
    assert(model.restoreSourcePose());
    for (char side : {'L','R'}) {
        const auto suffix=std::string("_")+side;
        const auto knee=namedLink(model,"Femur"+suffix,"Tibia"+suffix);
        const auto original=model.referenceBend(knee); assert(original);
        const double target=*original>45 ? 0 : 60;
        assert(model.setReferenceBend(knee,target));
        const auto matrices=model.sourceMatrices({},0);
        const auto& fit=model.kneeAxes()[side=='L' ? 0 : 1];
        const Vec3 axis=fit.medialDirection*(side=='L' ? -1.0 : 1.0);
        JointRigidTransform frame; frame.translation=fit.origin;
        const double delta=0.7*(target-*original)*3.141592653589793/180;
        const auto expected=joint_detail::compose(joint_detail::compose(frame,joint_detail::rotate(axis,delta)),
                                                   joint_detail::inverse(frame));
        const auto& patella=matrices[static_cast<std::size_t>(model.sourceStructure("Patella"+suffix))];
        for (int row=0;row<3;++row) for (int col=0;col<3;++col)
            assert(std::abs(patella[col*4+row]-expected.rotation[row*3+col])<1e-7);
        assert(length(Vec3{patella[12],patella[13],patella[14]}-expected.translation)<1e-7);
        const auto& tibia=matrices[static_cast<std::size_t>(model.sourceStructure("Tibia"+suffix))];
        const auto& fibula=matrices[static_cast<std::size_t>(model.sourceStructure("Fibula"+suffix))];
        for (int j=0;j<16;++j) assert(std::abs(tibia[j]-fibula[j])<1e-7);
        assert(model.restoreSourcePose());
    }
}

static void verify(SkeletonModel& model, const char* sex) {
    std::map<std::string,int> active;
    const auto original = model.sourceMatrices({},0);
    assert(original.size() == 182);
    for (std::size_t i=0; i<model.sourceLinks().size(); ++i) {
        const auto& edge = model.sourceLinks()[i];
        const auto range = referenceBendRange(edge.mobility,
            model.sourceStructures()[edge.first].name, model.sourceStructures()[edge.second].name);
        if (!range) continue;
        const auto initial = model.referenceBend(i);
        if (!initial) continue; // a redundant relationship, not a second parent
        const double target = *initial < (range->extension+range->flexion)/2
                            ? range->flexion : range->extension;
        assert(model.setReferenceBend(i,target));
        const auto posed = model.sourceMatrices({},0);
        assert(posed.size() == original.size());
        bool changed = false;
        for (std::size_t k=0; k<posed.size(); ++k) {
            for (int j=0; j<16; ++j) if (std::abs(posed[k][j]-original[k][j])>1e-7) changed=true;
            // Every matrix remains a rigid transform: no bone rescaling.
            for (int c=0;c<3;++c) {
                const double size=std::sqrt(posed[k][c*4]*posed[k][c*4]+posed[k][c*4+1]*posed[k][c*4+1]+posed[k][c*4+2]*posed[k][c*4+2]);
                assert(std::abs(size-1) < 1e-8);
            }
        }
        assert(changed);
        assert(model.setReferenceBend(i,*initial));
        const auto restored = model.sourceMatrices({},0);
        for (std::size_t k=0; k<restored.size(); ++k) for (int j=0; j<16; ++j)
            assert(std::abs(restored[k][j]-original[k][j]) < 1e-7);
        ++active[edge.mobility];
    }
    for (const auto* kind : {"hip_ball_socket","knee_modified_hinge","elbow_hinge",
                             "shoulder_ball_socket","wrist_condyloid","finger_mcp_condyloid",
                             "finger_pip_hinge","finger_dip_hinge","thumb_mcp",
                             "thumb_ip_hinge","great_toe_mtp","lesser_toe_mtp","toe_ip_hinge"}) {
        assert(active[kind] >= 2);
    }
    std::cout << sex << " reference-bend controls:";
    int total=0; for (auto& item:active) total+=item.second;
    std::cout << ' ' << total << " working links (estimated source neutral)\n";
}
int main() {
    SkeletonRoom room; std::string error;
    if (!room.load(".",error)) { std::cerr << error << '\n'; return 1; }
    checkArmAlignment(room.male()); checkArmAlignment(room.female());
    checkLinePose(room);
    // Room-level controls reach both subjects and restore their source pose.
    const auto shoulder=namedLink(room.male(),"Scapula_L","Humerus_L");
    const double maleBefore=*room.male().referenceBend(shoulder);
    const double femaleBefore=*room.female().referenceBend(shoulder);
    assert(room.adjustReferenceGroup('q',5));
    assert(std::abs(*room.male().referenceBend(shoulder)-maleBefore-5)<1e-7);
    assert(std::abs(*room.female().referenceBend(shoulder)-femaleBefore-5)<1e-7);
    assert(room.restoreReferencePose());
    // A paired group receives one step, not two steps through its two graph
    // links. Both source skeletons and both anatomical sides are tested.
    for (char key : {'e','f'}) {
        double initial[2][2]{}; int subject=0;
        for (SkeletonModel* model : {&room.male(),&room.female()}) {
            int which=0;
            for (char side : {'L','R'}) {
                const auto suffix=std::string("_")+side;
                const auto first=key=='e' ? namedLink(*model,"Humerus"+suffix,"Radius"+suffix) :
                                           namedLink(*model,"Radius"+suffix,"HandScaphoid"+suffix);
                const auto second=key=='e' ? namedLink(*model,"Humerus"+suffix,"Ulna"+suffix) :
                                            namedLink(*model,"Radius"+suffix,"HandLuna"+suffix);
                assert(std::abs(*model->referenceBend(first)-*model->referenceBend(second))<1e-8);
                initial[subject][which++]=*model->referenceBend(first);
            }
            ++subject;
        }
        assert(room.adjustReferenceGroup(key,5)); subject=0;
        for (SkeletonModel* model : {&room.male(),&room.female()}) {
            int which=0;
            for (char side : {'L','R'}) {
                const auto suffix=std::string("_")+side;
                const auto first=key=='e' ? namedLink(*model,"Humerus"+suffix,"Radius"+suffix) :
                                           namedLink(*model,"Radius"+suffix,"HandScaphoid"+suffix);
                const auto second=key=='e' ? namedLink(*model,"Humerus"+suffix,"Ulna"+suffix) :
                                            namedLink(*model,"Radius"+suffix,"HandLuna"+suffix);
                assert(std::abs(*model->referenceBend(first)-*model->referenceBend(second))<1e-8);
                assert(std::abs(*model->referenceBend(first)-initial[subject][which++]-5)<1e-8);
            }
            ++subject;
        }
        assert(room.restoreReferencePose());
    }
    assert(!room.adjustReferenceGroup('v',5));
    for (char key : {'1','2','3','4','5','6','7','8','9','0','q','e','f','g','h','j','k','l','z','x'}) {
        assert(room.adjustReferenceGroup(key,5));
        assert(room.restoreReferencePose());
    }
    assert(room.adjustReferenceGroup('2',5));
    for (SkeletonModel* model : {&room.male(), &room.female()}) {
        const auto matrices=model->sourceMatrices({},0);
        for (char side : {'L','R'}) {
            const auto suffix=std::string("_")+side;
            const int index=model->sourceStructure("HandDistP2"+suffix);
            const int middle=model->sourceStructure("HandDistP3"+suffix);
            const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            bool moved=false;
            for (int j=0;j<16;++j) {
                if (std::abs(matrices[index][j]-identity[j])>1e-7) moved=true;
                assert(std::abs(matrices[middle][j]-identity[j])<1e-7);
            }
            assert(moved);
        }
    }
    assert(room.restoreReferencePose());
    checkGroupedWrist(room.male()); checkGroupedWrist(room.female());
    checkPatellaCoupling(room.male()); checkPatellaCoupling(room.female());
    verify(room.male(),"male"); verify(room.female(),"female");
    assert(room.male().restoreSourcePose() && room.female().restoreSourcePose());
    for (SkeletonModel* model : {&room.male(),&room.female()})
        for (const auto& m : model->sourceMatrices({},0)) {
            const std::array<double,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (std::size_t i=0;i<16;++i) assert(std::abs(m[i]-identity[i])<1e-7);
        }
}
