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
static Vec3 contact(const SkeletonModel& model, int first, int second) {
    const auto& a=model.sourceStructures()[first]; const auto& b=model.sourceStructures()[second];
    const auto& p=model.bones()[a.group].points; const auto& q=model.bones()[b.group].points;
    double best=1e9; Vec3 result;
    for (std::size_t i=0;i<a.pointCount;i+=std::max<std::size_t>(1,a.pointCount/192))
        for (std::size_t j=0;j<b.pointCount;j+=std::max<std::size_t>(1,b.pointCount/192)) {
            Vec3 x=p[a.firstPoint+i],y=q[b.firstPoint+j]; double d=length(x-y);
            if (d<best) { best=d; result=(x+y)*0.5; }
        }
    return result;
}
static void checkTPose(SkeletonModel& model) {
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
        const Vec3 shoulder=model.bones()[group].pivot, elbow=contact(model,h,u);
        const Vec3 upper=normalized(transform(matrices[h],elbow)-transform(matrices[h],shoulder));
        const Vec3 fore=normalized(transform(matrices[u],center(model,u))-transform(matrices[h],elbow));
        const Vec3 target=side=='L' ? Vec3{1,0,0} : Vec3{-1,0,0};
        assert(dot(upper,target)>0.99999 && dot(fore,target)>0.99999);
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
    checkTPose(room.male()); checkTPose(room.female());
    checkLinePose(room);
    verify(room.male(),"male"); verify(room.female(),"female");
    assert(room.male().restoreSourcePose() && room.female().restoreSourcePose());
    for (SkeletonModel* model : {&room.male(),&room.female()})
        for (const auto& m : model->sourceMatrices({},0)) {
            const std::array<double,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (std::size_t i=0;i<16;++i) assert(std::abs(m[i]-identity[i])<1e-7);
        }
}
