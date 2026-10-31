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
static std::vector<Vec3> vertices(const SkeletonModel&,const SkeletonComparison& comp,const char* name) {
    for (const auto& part:comp.parts()) if (part.name==name) {
        std::vector<Vec3> out; out.reserve(part.points.size());
        for (Vec3 p:part.points) out.push_back(comp.map(p,part.armSide,part.cranial,part.girdle,part.humerus));
        return out;
    }
    assert(false); return {};
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
    const Vec3 mappedReference=c.map(reference,0,true);
    double cranialError=0;
    for (const char* name:{"Head","Jaw","C1"}) {
        const int id=model.sourceStructure(name); assert(id>=0);
        const auto& part=model.sourceStructures()[id]; const auto& group=model.bones()[part.group];
        for (std::size_t i=part.firstPoint;i<part.firstPoint+part.pointCount;++i) {
            const Vec3 point=transformed(matrices[id],group.points[i]);
            const Vec3 expected=mappedReference+(point-reference)*headScale;
            cranialError=std::max(cranialError,length(c.map(point,0,true)-expected));
            if (std::string(name)=="C1") assert(length(c.map(point)-c.map(point,0,true))<1e-8);
        }
    }
    assert(cranialError<1e-8*std::max(1.0,headScale));
    // The averaged pelvic ring remains rigid under each slider profile.
    const SkeletonComparison::Part *sacrum=nullptr, *sternum=nullptr, *pelvisL=nullptr, *pelvisR=nullptr;
    for (const auto& part:c.parts()) {
        if (part.name=="Sacrum") sacrum=&part;
        if (part.name=="Sternum") sternum=&part;
        if (part.name=="Pelvis_L") pelvisL=&part;
        if (part.name=="Pelvis_R") pelvisR=&part;
    }
    assert(sacrum && sternum && pelvisL && pelvisR);
    // The sacrum and sternum are axial structures and must be reflected too,
    // rather than preserving off-centre source surfaces in the blue copy.
    const double symmetryAxis=(c.hipCenters()[0].x+c.hipCenters()[1].x)*0.5;
    for (const auto* axial:{sacrum,sternum}) {
        bilateral::PointTree tree(axial->points);
        for (Vec3 p:axial->points) {
            Vec3 mirror{2*symmetryAxis-p.x,p.y,p.z};
            assert(length(tree.nearest(mirror)-mirror)<1e-10);
            const Vec3 a=c.map(p),b=c.map(mirror);
            assert(length(Vec3{-a.x,a.y,a.z}-b)<1e-8*std::max(1.0,c.profile().height/100));
        }
    }
    const double pelvicScale=c.targets()[SkeletonComparison::Hip]/extent(pelvisL->points,pelvisR->points,0);
    double pelvicBottom=1e30,pelvicTop=-1e30;
    for (const auto* part:{pelvisL,pelvisR}) for (Vec3 p:part->points) {
        pelvicBottom=std::min(pelvicBottom,p.y); pelvicTop=std::max(pelvicTop,p.y);
    }
    for (double boundary:{pelvicBottom,pelvicTop}) {
        const Vec3 left=c.map({0.1,boundary-1e-8,0.02});
        const Vec3 right=c.map({0.1,boundary+1e-8,0.02});
        assert(length(right-left)<1e-5*std::max(1.0,pelvicScale));
    }
    const Vec3 pelvicReference=sacrum->points.front();
    const Vec3 pelvicMapped=c.map(pelvicReference);
    for (const auto* part:{pelvisL,pelvisR,sacrum}) for (Vec3 p:part->points)
        assert(length(c.map(p)-(pelvicMapped+(p-pelvicReference)*pelvicScale))<1e-8*std::max(1.0,pelvicScale));
    // Check every actual paired part, not merely the source reference joints.
    double leftFloor=1e30,rightFloor=1e30;
    for (const auto& left:c.parts()) {
        if (left.name.size()<2 || left.name.substr(left.name.size()-2)!="_L") continue;
        const std::string name=left.name.substr(0,left.name.size()-2)+"_R";
        const auto it=std::find_if(c.parts().begin(),c.parts().end(),[&](const auto& part){return part.name==name;});
        assert(it!=c.parts().end() && left.points.size()==it->points.size() && left.faces.size()==it->faces.size());
        for (std::size_t i=0;i<left.points.size();++i) {
            const Vec3 a=c.map(left.points[i],left.armSide,left.cranial,left.girdle,left.humerus), b=c.map(it->points[i],it->armSide,it->cranial,it->girdle,it->humerus);
            assert(length(Vec3{-a.x,a.y,a.z}-b)<1e-8*std::max(1.0,c.profile().height/100));
            if (left.name.rfind("Foot",0)==0 || left.name.rfind("Cal_",0)==0 || left.name.rfind("Tal_",0)==0 ||
                left.name.rfind("Nav_",0)==0 || left.name.rfind("Cub_",0)==0 || left.name.rfind("MedCun_",0)==0 ||
                left.name.rfind("InterCun_",0)==0 || left.name.rfind("LatCun_",0)==0) {
                leftFloor=std::min(leftFloor,a.y); rightFloor=std::min(rightFloor,b.y);
            }
        }
        const int sourceIndex=model.sourceStructure(left.name); assert(sourceIndex>=0);
        const auto& sourcePart=model.sourceStructures()[sourceIndex];
        const auto& sourceBone=model.bones()[sourcePart.group];
        assert(left.points.size()==sourcePart.pointCount);
        for (std::size_t i=0;i<left.faces.size();++i) {
            const auto& f=left.faces[i];
            assert(f[0]==it->faces[i][0] && f[1]==it->faces[i][2] && f[2]==it->faces[i][1]);
            const auto source=[&](int n) { return transformed(matrices[sourceIndex],sourceBone.points[sourcePart.firstPoint+n]); };
            const Vec3 oldNormal=cross(source(f[1])-source(f[0]),source(f[2])-source(f[0]));
            const Vec3 newNormal=cross(left.points[f[1]]-left.points[f[0]],left.points[f[2]]-left.points[f[0]]);
            if (dot(oldNormal,oldNormal)>1e-24) assert(dot(oldNormal,newNormal)>0);
        }
    }
    assert(std::abs(leftFloor)<1e-8 && std::abs(rightFloor)<1e-8);
    // The common raw hip-to-sole length is the arithmetic mean of both
    // original posed sides, not a copy of either one.
    std::array<double,2> sourceSoles{1e30,1e30};
    for (std::size_t index=0;index<model.sourceStructures().size();++index) {
        const auto& part=model.sourceStructures()[index]; const auto& name=part.name;
        if (name.size()<2) continue;
        const std::string suffix=name.substr(name.size()-2);
        if (suffix!="_L" && suffix!="_R") continue;
        if (name.rfind("Foot",0)!=0 && name.rfind("Cal_",0)!=0 && name.rfind("Tal_",0)!=0 &&
            name.rfind("Nav_",0)!=0 && name.rfind("Cub_",0)!=0 && name.rfind("MedCun_",0)!=0 &&
            name.rfind("InterCun_",0)!=0 && name.rfind("LatCun_",0)!=0) continue;
        const int side=suffix=="_L"?0:1; const auto& bone=model.bones()[part.group];
        for (std::size_t j=part.firstPoint;j<part.firstPoint+part.pointCount;++j)
            sourceSoles[side]=std::min(sourceSoles[side],transformed(matrices[index],bone.points[j]).y);
    }
    std::ifstream rawHips(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".hips");
    std::array<double,2> rawHipY{}; std::string hipRow;
    while (std::getline(rawHips,hipRow)) {
        if (hipRow.empty() || hipRow[0]=='#') continue;
        std::istringstream in(hipRow); char side; Vec3 p; double radius;
        assert(in>>side>>p.x>>p.y>>p.z>>radius);
        rawHipY[side=='L'?0:1]=transformed(matrices[model.sourceStructure(std::string("Femur_")+side)],p).y;
    }
    double rawSole=1e30;
    for (const auto& part:c.parts()) for (Vec3 p:part.points) rawSole=std::min(rawSole,p.y);
    const double expectedMean=((rawHipY[0]-sourceSoles[0])+(rawHipY[1]-sourceSoles[1]))*0.5;
    assert(std::abs((c.hipCenters()[0].y-rawSole)-expectedMean)<1e-8);
    while (std::getline(audit,row)) {
        std::istringstream fields(row); std::vector<std::string> cols; std::string field;
        while (std::getline(fields,field,'\t')) cols.push_back(field);
        if (cols.size()<15 || cols[1]!=(male?"male":"female")) continue;
        Vec3 a{std::stod(cols[3]),std::stod(cols[4]),std::stod(cols[5])};
        Vec3 b{std::stod(cols[12]),std::stod(cols[13]),std::stod(cols[14])};
        c7=c.map(transformed(matrices[model.sourceStructure("C7")],a)).y;
        neck+=c.map(transformed(matrices[model.sourceStructure("C1")],b),0,true).y; ++count;
    }
    assert(count==2); neck/=count;
    close((neck-c7)*100,c.targets()[SkeletonComparison::Neck]);
    close((c7-std::min(lowY(pl),lowY(pr)))*100,c.targets()[SkeletonComparison::Torso]);
    double hipY=0; const auto& hips=c.hipCenters();
    const double sourceHipY=(hips[0].y+hips[1].y)*0.5;
    const Vec3 hipL=c.map(hips[0]),hipR=c.map(hips[1]);
    hipY=hipL.y+hipR.y;
    close(hipY*50,c.targets()[SkeletonComparison::Leg]);
    assert(std::abs(hipL.y-hipR.y)<1e-8*std::max(1.0,c.profile().height/100));
    assert(length(Vec3{-hipL.x,hipL.y,hipL.z}-hipR)<1e-8);
    // The below-pelvis field must not taper the left/right positions or
    // depth with height: a straight joint-centre line stays vertical.
    const double low=rawSole+0.1*(pelvicBottom-rawSole);
    const double high=pelvicBottom+0.5*(hips[0].y-pelvicBottom);
    const Vec3 lower=c.map({hips[0].x,low,hips[0].z});
    const Vec3 upper=c.map({hips[0].x,high,hips[0].z});
    assert(std::abs(lower.x-upper.x)<1e-10 && std::abs(lower.z-upper.z)<1e-10);
    // The old piecewise-linear field made an abrupt bend through both pelvic
    // bones at this hip knot. Its vertical derivative must now be continuous.
    const double delta=1e-6;
    const auto y=[&](double at) { return c.map({0,at,0}).y; };
    const double below=(y(sourceHipY)-y(sourceHipY-delta))/delta;
    const double above=(y(sourceHipY+delta)-y(sourceHipY))/delta;
    assert(below>0 && above>0 && std::abs(below-above)<1e-3*std::max({1.0,below,above}));
    // No reversed vertical mapping around the pelvic region.
    double previous=y(sourceHipY-0.15);
    for (int i=1;i<=300;++i) {
        const double next=y(sourceHipY-0.15+0.001*i);
        assert(next>previous); previous=next;
    }
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
        for (const char* name:{"Head","Jaw","C1","Pelvis_L","Pelvis_R","Sacrum"}) {
            const int index=model.sourceStructure(name); assert(index>=0);
            const auto& part=copy.parts()[static_cast<std::size_t>(index)];
            const std::uint32_t id=(sex==0?12000u:14000u)+static_cast<unsigned>(index);
            const auto batch=std::find_if(scene.batches.begin(),scene.batches.end(),[&](const GpuBatch& b) { return b.objectId==id; });
            assert(batch!=scene.batches.end() && batch->vertices.size()==part.points.size());
            for (std::size_t i=0;i<part.points.size();++i) {
                Vec3 expected=copy.map(part.points[i],part.armSide,part.cranial,part.girdle,part.humerus);
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
    check(room,true); check(room,false);
    for (bool male:{true,false}) {
        auto p=room.comparison(male).profile();
        for (double h:{1.,5.,10.,50.,100.,140.,170.,200.,250.,400.,1000.}) {
            p.height=h; if (!room.setComparison(male,p,error)) { std::printf("height %g failed: %s\n",h,error.c_str()); std::abort(); }
            check(room,male);
        }
        p=room.comparison(male).profile(); p.height=50;
        assert(room.setComparison(male,p,error));
        const auto& fitted50=room.comparison(male).targets();
        const auto& requested50=room.comparison(male).requestedTargets();
        assert(!room.comparison(male).exact());
        for (int d:{SkeletonComparison::Head,SkeletonComparison::Neck,SkeletonComparison::Arm,
                    SkeletonComparison::Hip,SkeletonComparison::Shoulder})
            assert(std::abs(fitted50[d]-requested50[d])<1e-8);
        close(fitted50[SkeletonComparison::Torso]-requested50[SkeletonComparison::Torso],
              fitted50[SkeletonComparison::Leg]-requested50[SkeletonComparison::Leg],1e-8);
        room.resetComparison(male); close(room.comparison(male).profile().height,room.comparison(male).initialHeight());
        for (int d=SkeletonComparison::Head;d<SkeletonComparison::Count;++d) {
            p=room.comparison(male).profile(); p.multiplier[d]=1.02;
            assert(room.setComparison(male,p,error)); check(room,male); room.resetComparison(male);
        }
        // Both controls must move the rigid pelvic assembly, even when the
        // independently requested proxies cannot both be matched exactly.
        for (int d:{SkeletonComparison::Torso,SkeletonComparison::Leg}) {
            const auto before=room.comparison(male).targets();
            p=room.comparison(male).profile(); p.multiplier[d]=1.1;
            assert(room.setComparison(male,p,error));
            assert(std::abs(room.comparison(male).targets()[SkeletonComparison::Leg]-before[SkeletonComparison::Leg])>0.01);
            assert(std::abs(room.comparison(male).targets()[SkeletonComparison::Torso]-before[SkeletonComparison::Torso])>0.01);
            room.resetComparison(male);
        }
        for (double height:{50.,170.}) for (int d:{SkeletonComparison::Arm,SkeletonComparison::Hip,SkeletonComparison::Shoulder})
            for (double multiplier:{0.5,1.5}) {
                p=room.comparison(male).profile(); p.height=height; p.multiplier[d]=multiplier;
                assert(room.setComparison(male,p,error)); check(room,male); room.resetComparison(male);
            }
        // The arm-shaft fallback and shoulder frame must remain continuous as
        // the user drags the slider, including around very short arms.
        {
            p=room.comparison(male).profile(); p.height=50;
            std::array<Vec3,3> previous{}; bool first=true;
            for (int step=0;step<=1000;++step) {
                p.multiplier[SkeletonComparison::Arm]=0.5+step*0.001;
                assert(room.setComparison(male,p,error));
                const auto& copy=room.comparison(male); int slot=0;
                for (const char* name:{"Humerus_L","Radius_L","HandDistP3_L"}) {
                    const auto it=std::find_if(copy.parts().begin(),copy.parts().end(),[&](const auto& part){return part.name==name;});
                    assert(it!=copy.parts().end() && !it->points.empty());
                    const Vec3 q=copy.map(it->points.front(),it->armSide,it->cranial,it->girdle,it->humerus);
                    if (!first) assert(length(q-previous[slot])<0.001);
                    previous[slot++]=q;
                }
                first=false;
            }
            room.resetComparison(male);
        }
        const auto old=room.comparison(male).targets(); const auto rev=room.comparison(male).revision();
        p=room.comparison(male).profile();p.height=-1;
        assert(!room.setComparison(male,p,error));
        assert(room.comparison(male).targets()==old && room.comparison(male).revision()==rev);
        p=room.comparison(male).profile();p.multiplier[SkeletonComparison::Head]=10;
        assert(room.setComparison(male,p,error)); assert(!room.comparison(male).exact());
        check(room,male); room.resetComparison(male);
        // Check the displayed pelvic ring itself during the whole height drag,
        // not merely that the curve is continuous at the formula boundaries.
        {
            p=room.comparison(male).profile();
            const auto& parts=room.comparison(male).parts();
            const auto it=std::find_if(parts.begin(),parts.end(),[](const auto& part){return part.name=="Pelvis_L";});
            assert(it!=parts.end() && !it->points.empty());
            const Vec3 sample=it->points.front(); Vec3 previousPoint{};
            double previousWidth=0;
            for (int step=0;step<=1000;++step) {
                p.height=100+step*0.1; assert(room.setComparison(male,p,error));
                const auto& copy=room.comparison(male);
                const auto left=vertices(male?room.male():room.female(),copy,"Pelvis_L");
                const auto right=vertices(male?room.male():room.female(),copy,"Pelvis_R");
                const double width=extent(left,right,0), target=copy.targets()[SkeletonComparison::Hip];
                assert(std::abs(width-target)<1e-8);
                const Vec3 point=copy.map(sample);
                if (step) {
                    assert(width>=previousWidth && width-previousWidth<(male?0.029:0.035));
                    assert(length(point-previousPoint)<0.002);
                }
                previousWidth=width; previousPoint=point;
            }
            room.resetComparison(male);
        }
        // No visible jump at the child/adult and infant bridge boundaries.
        for (double boundary:{8.,9.,10.,11.,12.,50.,100.,male?160.:145.,male?175.:160.,male?190.:175.}) {
            p=room.comparison(male).profile();p.height=boundary-0.00001;assert(room.setComparison(male,p,error));
            const std::array<Vec3,3> points{{{0.1,1.4,0.02},{0.1,1.6,0.02},{0.1,1.7,0.02}}};
            std::array<Vec3,3> before, cranialBefore;
            for (std::size_t i=0;i<points.size();++i) {
                before[i]=room.comparison(male).map(points[i]);
                cranialBefore[i]=room.comparison(male).map(points[i],0,true);
            }
            const auto targetsBefore=room.comparison(male).targets();
            p.height=boundary+0.00001;assert(room.setComparison(male,p,error));
            for (std::size_t i=0;i<points.size();++i) {
                assert(length(room.comparison(male).map(points[i])-before[i])<0.001);
                assert(length(room.comparison(male).map(points[i],0,true)-cranialBefore[i])<0.001);
            }
            for (int dimension=0;dimension<SkeletonComparison::Count;++dimension)
                assert(std::abs(room.comparison(male).targets()[dimension]-targetsBefore[dimension])<0.001);
        }
        // Audit the extreme-height fallback, including its entry and exit.
        Vec3 previous{}; bool first=true;
        for (double h=1;h<=20;h+=0.1) {
            p=room.comparison(male).profile(); p.height=h; assert(room.setComparison(male,p,error));
            const Vec3 point=room.comparison(male).map({0.1,1.4,0.02});
            if (!first) assert(length(point-previous)<0.001);
            previous=point; first=false;
        }
        // Formula requests remain unchanged and continuous throughout the
        // full 50–1000 cm sweep. Fitted skeletal proxies are reported separately.
        BodyProportionCurves curves;
        const std::string dir="tools/skeleton-source/anthropometry/";
        assert(curves.load(dir+"BODY_PART_CURVE_KNOTS.tsv",error));
        assert(curves.loadSelectedModels(dir+"BODY_PART_SELECTED_MODELS.tsv",error));
        const auto sex=male?BodyReferenceSex::male:BodyReferenceSex::female;
        for (double h=50;h<=1000;h+=0.5) {
            p=room.comparison(male).profile(); p.height=h;
            assert(room.setComparison(male,p,error));
            const auto ref=estimateHeightBodyReference(sex,h); assert(ref);
            const auto hip=curves.blendedNhanesDimensionCm(male?"male":"female","pelvis_surface_breadth",h);
            const auto shoulder=curves.blendedNhanesDimensionCm(male?"male":"female","shoulder_breadth",h);
            assert(hip && shoulder);
            const auto& t=room.comparison(male).requestedTargets();
            const std::array<double,8> wanted{h,ref->headCm,ref->neckCm,ref->torsoCm,ref->armCm,ref->legCm,*hip,*shoulder};
            for (std::size_t i=0;i<t.size();++i) assert(std::abs(t[i]-wanted[i])<1e-8);
        }
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
