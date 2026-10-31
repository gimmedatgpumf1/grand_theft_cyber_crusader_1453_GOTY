// Compare actual T-pose source vertices and the GPU scene, not just the slider state.
#include "client/skeleton_comparison_ui.h"
#include "client/ui_layer.h"
#include "client/room_freecam.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace gtc::native;

static Vec3 transformed(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static std::vector<Vec3> vertices(const SkeletonModel& model,const SkeletonComparison& comp,const char* name) {
    const int i=model.sourceStructure(name); assert(i>=0);
    const auto& part=model.sourceStructures()[i]; const auto& group=model.bones()[part.group];
    const auto matrix=model.sourceMatrices({},0)[i]; std::vector<Vec3> out;
    int side=0; std::string n=name;
    if (n.rfind("Humerus_",0)==0 || n.rfind("Hand",0)==0 || n.rfind("Radius_",0)==0 || n.rfind("Ulna_",0)==0)
        side=n.substr(n.size()-2)=="_L"?1:-1;
    for (std::size_t j=part.firstPoint;j<part.firstPoint+part.pointCount;++j)
        out.push_back(comp.map(transformed(matrix,group.points[j]),side));
    return out;
}
static double extent(const std::vector<Vec3>& a,const std::vector<Vec3>& b,int axis) {
    double lo=1e30,hi=-1e30;
    for (const auto* list:{&a,&b}) for (Vec3 p:*list) {
        const double v=axis?p.y:p.x; lo=std::min(lo,v); hi=std::max(hi,v);
    }
    return (hi-lo)*100;
}
static double lowY(const std::vector<Vec3>& p) { double y=1e30; for (Vec3 q:p) y=std::min(y,q.y); return y; }
static double highY(const std::vector<Vec3>& p) { double y=-1e30; for (Vec3 q:p) y=std::max(y,q.y); return y; }
static Vec3 extreme(const std::vector<Vec3>& p,int side) { Vec3 q=p.front(); for (Vec3 v:p) if (side*v.x>side*q.x) q=v; return q; }
static void close(double a,double b,double epsilon=0.015) { if (std::abs(a-b)>epsilon) { std::printf("%.6f != %.6f\n",a,b); std::abort(); } }
static void check(const SkeletonRoom& room, bool male) {
    const auto& model=male?room.male():room.female(); const auto& c=room.comparison(male);
    auto head=vertices(model,c,"Head"), jaw=vertices(model,c,"Jaw");
    auto pl=vertices(model,c,"Pelvis_L"), pr=vertices(model,c,"Pelvis_R");
    auto sl=vertices(model,c,"Scapula_L"), sr=vertices(model,c,"Scapula_R");
    close((highY(head)-lowY(jaw))*100,c.targets()[SkeletonComparison::Head]);
    close(extent(pl,pr,0),c.targets()[SkeletonComparison::Hip]);
    close(extent(sl,sr,0),c.targets()[SkeletonComparison::Shoulder]);
    // C7 and C1 contact points are the same stored geometric landmarks used
    // by the anthropometry audit, transformed by the actual T-pose matrices.
    std::ifstream audit("tools/skeleton-source/anthropometry/SKELETON_NECK_SKULL_CONTACT.tsv");
    std::string row; std::getline(audit,row); double c7=0, neck=0; int count=0;
    const auto matrices=model.sourceMatrices({},0);
    // Every actual skull, jaw and C1 vertex must share the same similarity
    // transform. This catches the former kink through the skull, vertical-only
    // stretching of the jaw, and any shift of the jaw relative to the skull.
    const int headId=model.sourceStructure("Head"), jawId=model.sourceStructure("Jaw");
    const auto& sourceHead=model.sourceStructures()[headId];
    const auto& sourceJaw=model.sourceStructures()[jawId];
    const auto& headGroup=model.bones()[sourceHead.group];
    const auto& jawGroup=model.bones()[sourceJaw.group];
    double originalTop=-1e30, originalJaw=1e30;
    for (std::size_t i=sourceHead.firstPoint;i<sourceHead.firstPoint+sourceHead.pointCount;++i)
        originalTop=std::max(originalTop,transformed(matrices[headId],headGroup.points[i]).y);
    for (std::size_t i=sourceJaw.firstPoint;i<sourceJaw.firstPoint+sourceJaw.pointCount;++i)
        originalJaw=std::min(originalJaw,transformed(matrices[jawId],jawGroup.points[i]).y);
    const double headScale=c.targets()[SkeletonComparison::Head]/(100*(originalTop-originalJaw));
    const Vec3 reference=transformed(matrices[headId],headGroup.points[sourceHead.firstPoint]);
    const Vec3 mappedReference=c.map(reference);
    double cranialError=0;
    for (const char* name:{"Head","Jaw","C1"}) {
        const int id=model.sourceStructure(name); assert(id>=0);
        const auto& part=model.sourceStructures()[id]; const auto& group=model.bones()[part.group];
        for (std::size_t i=part.firstPoint;i<part.firstPoint+part.pointCount;++i) {
            const Vec3 point=transformed(matrices[id],group.points[i]);
            const Vec3 expected=mappedReference+(point-reference)*headScale;
            cranialError=std::max(cranialError,length(c.map(point)-expected));
        }
    }
    assert(cranialError<1e-8*std::max(1.0,headScale));
    while (std::getline(audit,row)) {
        std::istringstream fields(row); std::vector<std::string> cols; std::string field;
        while (std::getline(fields,field,'\t')) cols.push_back(field);
        if (cols.size()<15 || cols[1]!=(male?"male":"female")) continue;
        Vec3 a{std::stod(cols[3]),std::stod(cols[4]),std::stod(cols[5])};
        Vec3 b{std::stod(cols[12]),std::stod(cols[13]),std::stod(cols[14])};
        c7=c.map(transformed(matrices[model.sourceStructure("C7")],a)).y;
        neck+=c.map(transformed(matrices[model.sourceStructure("C1")],b)).y; ++count;
    }
    assert(count==2); neck/=count;
    close((neck-c7)*100,c.targets()[SkeletonComparison::Neck]);
    close((c7-std::min(lowY(pl),lowY(pr)))*100,c.targets()[SkeletonComparison::Torso]);
    std::ifstream hips(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".hips");
    while (std::getline(hips,row) && (row.empty() || row[0]=='#')) {}
    hips.clear(); hips.seekg(0); double hipY=0;
    while (hips.peek()=='#') std::getline(hips,row);
    for (int side=0;side<2;++side) {
        char ch; Vec3 q; double radius; assert(hips>>ch>>q.x>>q.y>>q.z>>radius);
        hipY+=c.map(transformed(matrices[model.sourceStructure(std::string("Femur_")+ch)],q)).y;
    }
    close(hipY*50,c.targets()[SkeletonComparison::Leg]);
    auto hl=vertices(model,c,"Humerus_L"), hr=vertices(model,c,"Humerus_R");
    auto fl=vertices(model,c,"HandDistP3_L"), fr=vertices(model,c,"HandDistP3_R");
    const double arms=50*(length(extreme(fl,1)-extreme(hl,-1))+length(extreme(fr,-1)-extreme(hr,1)));
    close(arms,c.targets()[SkeletonComparison::Arm],0.05);
    // Blue height uses the visible height of the original T-pose, not the
    // different recorded cadaver length.
    close(highY(head)*100,c.profile().height);
    std::printf("%s h %.2f head %.2f arms %.2f hips %.2f shoulders %.2f cm\n",male?"male":"female",
                c.profile().height,(highY(head)-lowY(jaw))*100,arms,extent(pl,pr,0),extent(sl,sr,0));
}
static int blue=0,ui=0,white=0; static std::uint64_t revision=0; static float sample=0; static double whiteX=0; static std::uint64_t blueStamp=0;
static const SkeletonRoom* observedRoom=nullptr;
static std::vector<std::array<double,16>> originalMatrices[2];
static bool gpu(const GpuScene& scene,NativeFrame& frame) {
    blue=ui=white=0; revision=scene.revision;
    for (const auto& b:scene.batches) {
        if (b.isUi) ++ui;
        if (b.objectId>=12000 && b.objectId<15000) { ++blue; if (b.objectId==12000 && !b.vertices.empty()) { sample=b.vertices[0].y; blueStamp=b.revision; } }
        if (b.objectId>=8001 && b.objectId<10000) { ++white; if (b.objectId==9001) whiteX=b.modelMatrix[12]; }
    }
    // The submitted indexed meshes must use the tested T-pose deformation,
    // even after a slider, line-view or ragdoll update of the originals.
    if (observedRoom) for (int sex=0;sex<2;++sex) {
        const auto& model=sex==0?observedRoom->male():observedRoom->female();
        const auto& copy=observedRoom->comparison(sex==0);
        const auto& matrices=originalMatrices[sex];
        for (const char* name:{"Head","Jaw","C1"}) {
            const int index=model.sourceStructure(name); assert(index>=0);
            const auto& part=model.sourceStructures()[index]; const auto& group=model.bones()[part.group];
            const std::uint32_t id=(sex==0?12000u:14000u)+static_cast<unsigned>(index);
            const auto batch=std::find_if(scene.batches.begin(),scene.batches.end(),[&](const GpuBatch& b) { return b.objectId==id; });
            assert(batch!=scene.batches.end() && batch->vertices.size()==part.pointCount);
            for (std::size_t i=0;i<part.pointCount;++i) {
                Vec3 expected=copy.map(transformed(matrices[index],group.points[part.firstPoint+i]));
                expected.x+=sex==0?-2.85:2.85;
                const auto& v=batch->vertices[i];
                assert(length(Vec3{v.x,v.y,v.z}-expected)<1e-5);
            }
        }
    }
    frame.presented=true; return true;
}
static WindowEvent mouse(WindowEvent::Type type,double x,double y) { WindowEvent e{type}; e.button=1;e.x=x;e.y=y;return e; }
int main() {
    std::string error; SkeletonRoom room; assert(room.load(".",error));
    const auto maleOriginal=room.male().sourceMatrices({},0), femaleOriginal=room.female().sourceMatrices({},0);
    observedRoom=&room; originalMatrices[0]=maleOriginal; originalMatrices[1]=femaleOriginal;
    assert(room.comparison(true).exact() && room.comparison(false).exact());
    check(room,true); check(room,false);
    for (bool male:{true,false}) {
        auto p=room.comparison(male).profile();
        for (double h:{1.,5.,10.,50.,100.,140.,170.,200.,250.,400.,1000.}) {
            p.height=h; if (!room.setComparison(male,p,error)) { std::printf("height %g failed: %s\n",h,error.c_str()); std::abort(); } check(room,male);
        }
        room.resetComparison(male); close(room.comparison(male).profile().height,room.comparison(male).initialHeight());
        for (int d=SkeletonComparison::Head;d<SkeletonComparison::Count;++d) {
            p=room.comparison(male).profile(); p.multiplier[d]=1.02;
            assert(room.setComparison(male,p,error)); check(room,male); room.resetComparison(male);
        }
        const auto old=room.comparison(male).targets(); const auto rev=room.comparison(male).revision();
        p=room.comparison(male).profile();p.height=-1;
        assert(!room.setComparison(male,p,error));
        assert(room.comparison(male).targets()==old && room.comparison(male).revision()==rev);
        p=room.comparison(male).profile();p.multiplier[SkeletonComparison::Head]=10;
        assert(room.setComparison(male,p,error)); assert(!room.comparison(male).exact());
        check(room,male); room.resetComparison(male);
        // No visible jump at the child/adult and infant bridge boundaries.
        for (double boundary:{8.,9.,10.,11.,12.,50.,100.,male?160.:145.,male?175.:160.}) {
            p=room.comparison(male).profile();p.height=boundary-0.00001;assert(room.setComparison(male,p,error));
            const std::array<Vec3,3> points{{{0.1,1.4,0.02},{0.1,1.6,0.02},{0.1,1.7,0.02}}};
            std::array<Vec3,3> before;
            for (std::size_t i=0;i<points.size();++i) before[i]=room.comparison(male).map(points[i]);
            p.height=boundary+0.00001;assert(room.setComparison(male,p,error));
            for (std::size_t i=0;i<points.size();++i)
                assert(length(room.comparison(male).map(points[i])-before[i])<0.001);
        }
        // Audit the extreme-height fallback, including its entry and exit.
        Vec3 previous{}; bool first=true;
        for (double h=1;h<=20;h+=0.1) {
            p=room.comparison(male).profile(); p.height=h; assert(room.setComparison(male,p,error));
            const Vec3 point=room.comparison(male).map({0.1,1.4,0.02});
            if (!first) assert(length(point-previous)<0.001);
            previous=point; first=false;
        }
        // Locate the partial-to-exact transition and inspect the skull on
        // both sides of it, not only at the published curve knot heights.
        p=room.comparison(male).profile(); p.height=20; assert(room.setComparison(male,p,error));
        bool wasExact=room.comparison(male).exact(); int transitions=0;
        for (double h=20.25;h<=105;h+=0.25) {
            p.height=h; assert(room.setComparison(male,p,error));
            const bool nowExact=room.comparison(male).exact();
            if (nowExact!=wasExact) {
                double low=h-0.25, high=h;
                for (int i=0;i<25;++i) {
                    p.height=(low+high)*0.5; assert(room.setComparison(male,p,error));
                    if (room.comparison(male).exact()==wasExact) low=p.height; else high=p.height;
                }
                p.height=low; assert(room.setComparison(male,p,error));
                const Vec3 before=room.comparison(male).map({0.1,1.7,0.1});
                p.height=high; assert(room.setComparison(male,p,error));
                assert(length(room.comparison(male).map({0.1,1.7,0.1})-before)<0.001);
                ++transitions; p.height=h; assert(room.setComparison(male,p,error));
            }
            wasExact=nowExact;
        }
        assert(transitions>0);
        room.resetComparison(male);
    }
    // Captured cursor cannot drag; released cursor can drag/reset/select sex.
    SkeletonComparisonUi control; const int w=1000,h=800, x=(w-940)/2,y=h-195-12;
    assert(control.handle(WindowEvent{WindowEvent::Type::KeyDown,'p'},true,w,h,room));
    auto e=mouse(WindowEvent::Type::MouseDown,x+200,y+60);
    assert(!control.handle(e,true,w,h,room));
    assert(control.handle(e,false,w,h,room));
    assert(room.comparison(true).profile().height!=room.comparison(true).initialHeight());
    auto move=mouse(WindowEvent::Type::MouseMove,x+250,y+60);
    assert(control.handle(move,false,w,h,room));
    auto up=mouse(WindowEvent::Type::MouseUp,x+600,y-20); assert(control.handle(up,false,w,h,room));
    assert(control.handle(mouse(WindowEvent::Type::MouseDown,x+880,y+32),false,w,h,room));
    close(room.comparison(true).profile().height,room.comparison(true).initialHeight());
    assert(control.handle(mouse(WindowEvent::Type::MouseDown,x+120,y+32),false,w,h,room));
    double femaleBefore=room.comparison(false).profile().height;
    assert(control.handle(mouse(WindowEvent::Type::MouseDown,x+380,y+60),false,w,h,room));
    close(room.comparison(false).profile().height,femaleBefore+1);
    assert(!control.handle(mouse(WindowEvent::Type::MouseDown,0,0),false,w,h,room));
    room.resetComparison(false);
    // At a compact window size the second set of four controls remains reachable.
    SkeletonComparisonUi compact; constexpr int sw=320, sh=240, sx=8, sy=33;
    assert(compact.handle(WindowEvent{WindowEvent::Type::KeyDown,'p'},true,sw,sh,room));
    assert(compact.handle(mouse(WindowEvent::Type::MouseDown,sx+30,sy+180),false,sw,sh,room));
    const auto armBefore=room.comparison(true).profile().multiplier[SkeletonComparison::Arm];
    assert(compact.handle(mouse(WindowEvent::Type::MouseDown,sx+212,sy+60),false,sw,sh,room));
    assert(room.comparison(true).profile().multiplier[SkeletonComparison::Arm]>armBefore);
    room.resetComparison(true);
    assert(room.male().sourceMatrices({},0)==maleOriginal && room.female().sourceMatrices({},0)==femaleOriginal);
    ViewState view; view.width=640;view.height=480;view.gpu=true;
    NativeFrame frame; UiLayer layer; layer.clear(view.width,view.height); frame.width=view.width;frame.height=view.height;frame.ui=&layer;
    control.draw(frame,room);
    bool glyph=false; for (const auto& quad:layer.quads()) if (quad.texture) glyph=true;
    assert(glyph); // the shipped atlas only contains 20-48 px glyphs
    frame.ui=nullptr;frame.uiLayer=&layer;
    setGpuRenderer(gpu); room.render(frame,view,0); assert(blue==364 && white==364 && ui>0 && frame.interfaceInFrame);
    const auto first=revision, originalBlueStamp=blueStamp; const float orig=sample;
    room.render(frame,view,0); assert(revision==first && sample==orig && ui>0);
    auto p=room.comparison(true).profile();p.multiplier[SkeletonComparison::Head]=1.02;
    assert(room.setComparison(true,p,error)); room.render(frame,view,0);
    assert(revision==first && blueStamp!=originalBlueStamp && blue==364 && white==364 && ui>0 && std::abs(whiteX)>0.5);
    const float blueBeforeRagdoll=sample; assert(room.toggleRagdoll()); room.advanceRagdoll(0.05);
    room.render(frame,view,0); assert(sample==blueBeforeRagdoll && room.ragdollActive());
    assert(room.toggleRagdoll());
    room.toggleLineView();room.render(frame,view,0);assert(blue==364 && ui>0);
    setGpuRenderer(nullptr);
    // Both supported software scene paths contain actual blue pixels too.
    view.gpu=false; view.width=320; view.height=240; RoomFreecam camera;
    frame.uiLayer=nullptr;
    for (int i=0;i<2;++i) {
        room.render(frame,view,0,&camera); assert(frame.rgb.size()==320u*240u*3u);
        int bluePixels=0;
        for (std::size_t j=0;j<frame.rgb.size();j+=3) {
            const int r=frame.rgb[j],g=frame.rgb[j+1],b=frame.rgb[j+2];
            if (b>r*1.4 && b>g*1.2 && b>45) ++bluePixels;
        }
        assert(bluePixels>30); room.toggleLineView();
    }
    std::puts("comparison geometry, constraints, UI and GPU cache: PASS");
}
