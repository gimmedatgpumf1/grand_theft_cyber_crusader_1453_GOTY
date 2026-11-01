// Exercise both 3-axis girdle joints on both actual room figures.
#include "client/skeleton_room.h"
#include "client/skeleton_limb_frame.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace gtc::native;
static Vec3 point(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Vec3 inversePoint(const std::array<double,16>& m, Vec3 p) {
    p=p-Vec3{m[12],m[13],m[14]};
    return {m[0]*p.x+m[1]*p.y+m[2]*p.z,m[4]*p.x+m[5]*p.y+m[6]*p.z,m[8]*p.x+m[9]*p.y+m[10]*p.z};
}
static void check(SkeletonModel& m) {
    const auto posed=m.sourceMatrices({},0); const auto& rig=*m.sourceRig();
    int count=0;
    for (std::size_t i=0;i<m.sourceLinks().size();++i) {
        const auto& e=m.sourceLinks()[i];
        if (e.mobility!="sternoclavicular" && e.mobility!="acromioclavicular") continue;
        m.sourceMatrices({},0); // evaluate after restoring the previous joint
        const auto parent=m.sourceBodyMap()[e.first], child=m.sourceBodyMap()[e.second];
        assert(rig.parent(child)==parent); const auto* joint=rig.joint(child); assert(joint && joint->axes().size()==4);
        const Vec3 anchor=joint_detail::compose(rig.absoluteTransforms()[parent],joint->parentFrame()).translation;
        const auto a=inversePoint(posed[e.first],anchor),b=inversePoint(posed[e.second],anchor);
        for (std::size_t c=0;c<3;++c) {
            assert(m.referenceGirdleCoordinate(i,c) && std::abs(*m.referenceGirdleCoordinate(i,c))<1e-8);
            assert(m.setReferenceGirdleCoordinate(i,c,8));
            const auto moved=m.sourceMatrices({},0);
            const double gap=length(point(moved[e.first],a)-point(moved[e.second],b));
            if (gap>=1e-8) std::fprintf(stderr,"joint %s %s c%zu gap %.9f\n",m.sourceStructures()[e.first].name.c_str(),m.sourceStructures()[e.second].name.c_str(),c,gap);
            assert(gap<1e-8);
            bool changed=false;
            for (int j=0;j<16;++j) { assert(std::abs(moved[e.first][j]-posed[e.first][j])<1e-8); changed|=std::abs(moved[e.second][j]-posed[e.second][j])>1e-7; }
            assert(changed);
            assert(m.setReferenceGirdleCoordinate(i,c,1000));
            assert(std::abs(*m.referenceGirdleCoordinate(i,c)-joint->axes()[c].maximum*180/3.141592653589793)<1e-7);
            assert(m.setReferenceGirdleCoordinate(i,c,-1000));
            assert(std::abs(*m.referenceGirdleCoordinate(i,c)-joint->axes()[c].minimum*180/3.141592653589793)<1e-7);
            assert(!m.setReferenceGirdleCoordinate(i,c,std::numeric_limits<double>::quiet_NaN()));
            assert(m.setReferenceGirdleCoordinate(i,c,0));
        }
        assert(!m.referenceGirdleCoordinate(i,3) && !m.setReferenceGirdleCoordinate(i,3,0));
        ++count;
    }
    assert(count==4);
    const auto restored=m.sourceMatrices({},0);
    for (std::size_t i=0;i<posed.size();++i) for (int j=0;j<16;++j) assert(std::abs(posed[i][j]-restored[i][j])<1e-8);
}
struct RotationTrack { SkeletonModel* model; std::size_t a,b; Vec3 axis[3]; double ref[9], max[3]{}; };
static void relative(const std::array<double,16>& a,const std::array<double,16>& b,double* r) {
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        r[i*3+j]=0; for (int k=0;k<3;++k) r[i*3+j]+=a[i*4+k]*b[j*4+k];
    }
}
static void trackFigure(SkeletonModel& m, std::vector<RotationTrack>& result) {
    const auto matrices=m.sourceMatrices({},0); const auto& rig=*m.sourceRig();
    for (const auto& e:m.sourceLinks()) {
        if (e.mobility!="sternoclavicular" && e.mobility!="acromioclavicular") continue;
        RotationTrack t{}; t.model=&m; t.a=e.first; t.b=e.second;
        const auto* joint=rig.joint(m.sourceBodyMap()[e.second]); assert(joint);
        for (int i=0;i<3;++i) t.axis[i]=joint->axes()[i].direction;
        relative(matrices[t.a],matrices[t.b],t.ref); result.push_back(t);
    }
}
static void trackMotion(RotationTrack& t) {
    const auto m=t.model->sourceMatrices({},0); double r[9],delta[9]; relative(m[t.a],m[t.b],r);
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) { delta[i*3+j]=0;
        for (int k=0;k<3;++k) delta[i*3+j]+=r[i*3+k]*t.ref[j*3+k]; }
    const Vec3 spin{(delta[7]-delta[5])*0.5,(delta[2]-delta[6])*0.5,(delta[3]-delta[1])*0.5};
    for (int i=0;i<3;++i) t.max[i]=std::max(t.max[i],std::abs(dot(spin,t.axis[i])));
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    check(room.male()); check(room.female());
    assert(room.restoreOriginalPose());
    for (const auto* m:{&room.male(),&room.female()}) for (const auto& a:m->sourceMatrices({},0))
        for (int j=0;j<16;++j) assert(std::abs(a[j]-(j%5==0 ? 1.0 : 0.0))<1e-8);
    assert(room.restoreReferencePose()); check(room.male()); check(room.female());
    std::vector<RotationTrack> tracks; trackFigure(room.male(),tracks); trackFigure(room.female(),tracks);
    assert(tracks.size()==8 && room.toggleRagdoll());
    for (int i=0;i<600;++i) { room.advanceRagdoll(1.0/120); for (auto& t:tracks) trackMotion(t); }
    for (const auto& t:tracks) {
        std::printf("%s %s dynamics %.3f %.3f %.3f\n",t.model->name().c_str(),t.model->sourceStructures()[t.b].name.c_str(),t.max[0],t.max[1],t.max[2]);
        assert(t.max[0]>.02 && t.max[1]>.02 && t.max[2]>.02);
    }
    std::puts("SC/AC three-axis controls, limits, rigid anchors and source/reference restore: PASS");
}
