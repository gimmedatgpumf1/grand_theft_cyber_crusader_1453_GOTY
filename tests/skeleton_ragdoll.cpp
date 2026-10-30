// Real source figures: fall, floor support, finite transforms and joint anchors.
#include "client/skeleton_room.h"
#include "client/skeleton_limb_frame.h"
#include <algorithm>
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
static Vec3 inversePoint(const std::array<double,16>& m, Vec3 p) {
    p=p-Vec3{m[12],m[13],m[14]};
    return {m[0]*p.x+m[1]*p.y+m[2]*p.z,m[4]*p.x+m[5]*p.y+m[6]*p.z,
            m[8]*p.x+m[9]*p.y+m[10]*p.z};
}
struct Anchor { std::size_t a,b; Vec3 pa,pb; };
static std::vector<Anchor> anchors(SkeletonModel& model) {
    const auto initial=model.sourceMatrices({},0); const auto& rig=*model.sourceRig();
    std::vector<int> index(rig.size(),-1); const auto& ids=model.sourceBodyMap();
    for (std::size_t i=0;i<ids.size();++i) index[ids[i]]=int(i);
    std::vector<Anchor> result;
    for (std::size_t n=1;n<rig.size();++n) {
        const auto parent=*rig.parent(n); const auto* joint=rig.joint(n);
        if (!parent || !joint) continue;
        const auto frame=joint_detail::compose(rig.absoluteTransforms()[parent],joint->parentFrame());
        const auto a=std::size_t(index[parent]),b=std::size_t(index[n]);
        result.push_back({a,b,inversePoint(initial[a],frame.translation),inversePoint(initial[b],frame.translation)});
    }
    return result;
}
// Source graph defines these anatomical links; nearest sampled mesh points
// estimate their attachment locations, not the existence of a relationship.
static std::vector<Anchor> pairedAnchors(const SkeletonModel& model, bool forearm) {
    const auto pose=model.sourceMatrices({},0); std::vector<Anchor> result;
    for (const auto& link:model.sourceLinks()) {
        if (forearm ? link.mobility!="radioulnar_pivot" : link.kind!="cartilage") continue;
        const auto a=link.first,b=link.second;
        Vec3 pivot{};
        if (forearm) {
            const auto& name=model.sourceStructures()[a].name;
            const int side=name.size()>2 && name.substr(name.size()-2)=="_L" ? 0 : 1;
            const auto wrist=sourceWristFrame(model,side,{1,0,0}); assert(wrist);
            pivot=(point(pose[a],wrist->wrist)+point(pose[b],wrist->wrist))*0.5;
        } else {
            const auto& sa=model.sourceStructures()[a]; const auto& sb=model.sourceStructures()[b];
            const auto& va=model.bones()[sa.group].points; const auto& vb=model.bones()[sb.group].points;
            const auto as=std::max<std::size_t>(1,sa.pointCount/250),bs=std::max<std::size_t>(1,sb.pointCount/250);
            double best=1e9;
            for (std::size_t i=0;i<sa.pointCount;i+=as) {
                const auto p=point(pose[a],va[sa.firstPoint+i]);
                for (std::size_t j=0;j<sb.pointCount;j+=bs) {
                    const auto q=point(pose[b],vb[sb.firstPoint+j]); const double d=length(p-q);
                    if (d<best) { best=d; pivot=(p+q)*0.5; }
                }
            }
        }
        result.push_back({a,b,inversePoint(pose[a],pivot),inversePoint(pose[b],pivot)});
    }
    return result;
}
static std::pair<double,double> extent(const SkeletonModel& model) {
    const auto m=model.sourceMatrices({},0); double low=100,high=-100;
    for (std::size_t i=0;i<m.size();++i) {
        for (double n:m[i]) assert(std::isfinite(n));
        const auto& s=model.sourceStructures()[i]; const auto& v=model.bones()[s.group].points;
        for (std::size_t j=0;j<s.pointCount;++j) {
            const auto p=point(m[i],v[s.firstPoint+j]); low=std::min(low,p.y); high=std::max(high,p.y);
        }
    }
    return {low,high};
}
static double maxGap(const SkeletonModel& model,const std::vector<Anchor>& list) {
    const auto m=model.sourceMatrices({},0); double gap=0;
    for (const auto& a:list) gap=std::max(gap,length(point(m[a.a],a.pa)-point(m[a.b],a.pb)));
    return gap;
}

struct HandAnchors { std::vector<Anchor> carpal, digits; };
static HandAnchors handAnchors(const SkeletonModel& model, const std::string& side) {
    const auto pose=model.sourceMatrices({},0); const auto& rig=*model.sourceRig();
    std::vector<int> index(rig.size(),-1); const auto& ids=model.sourceBodyMap();
    for (std::size_t i=0;i<ids.size();++i) index[ids[i]]=int(i);
    HandAnchors result;
    for (const auto& link:model.sourceLinks()) {
        const auto& name=model.sourceStructures()[link.first].name;
        if (link.mobility!="carpal_glide" || name.size()<2 || name.substr(name.size()-2)!=side) continue;
        result.carpal.push_back({link.first,link.second,inversePoint(pose[link.first],{}),inversePoint(pose[link.second],{})});
    }
    for (std::size_t n=1;n<rig.size();++n) {
        const auto parent=*rig.parent(n); const auto* joint=rig.joint(n);
        if (!parent || !joint) continue;
        const auto a=std::size_t(index[parent]), b=std::size_t(index[n]);
        const auto& name=model.sourceStructures()[b].name;
        if (name.size()<2 || name.substr(name.size()-2)!=side) continue;
        if (name.rfind("HandProxP",0)!=0 && name.rfind("HandMidP",0)!=0 &&
            name.rfind("HandDistP",0)!=0 && name.rfind("HandMeta1",0)!=0) continue;
        const auto frame=joint_detail::compose(rig.absoluteTransforms()[parent],joint->parentFrame());
        result.digits.push_back({a,b,inversePoint(pose[a],frame.translation),inversePoint(pose[b],frame.translation)});
    }
    return result;
}
static Vec3 relativeDirection(const std::vector<std::array<double,16>>& pose, const Anchor& anchor) {
    const auto& a=pose[anchor.a]; const auto& b=pose[anchor.b];
    const Vec3 axis=point(b,{1,0,0})-point(b,{});
    return inversePoint(a,Vec3{a[12],a[13],a[14]}+axis);
}
static double handLow(const SkeletonModel& model) {
    const auto pose=model.sourceMatrices({},0); double low=100;
    for (std::size_t i=0;i<pose.size();++i) {
        const auto& part=model.sourceStructures()[i];
        if (part.name.rfind("Hand",0)!=0) continue;
        const auto& vertices=model.bones()[part.group].points;
        for (std::size_t j=0;j<part.pointCount;++j) low=std::min(low,point(pose[i],vertices[part.firstPoint+j]).y);
    }
    return low;
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    const auto maleAnchors=anchors(room.male()), femaleAnchors=anchors(room.female());
    const auto maleForearms=pairedAnchors(room.male(),true), femaleForearms=pairedAnchors(room.female(),true);
    const auto maleCartilage=pairedAnchors(room.male(),false), femaleCartilage=pairedAnchors(room.female(),false);
    assert(maleForearms.size()==2 && femaleForearms.size()==2);
    assert(maleCartilage.size()==20 && femaleCartilage.size()==20);
    const auto maleStart=extent(room.male()), femaleStart=extent(room.female());
    const HandAnchors hands[]={handAnchors(room.male(),"_L"),handAnchors(room.male(),"_R"),
                               handAnchors(room.female(),"_L"),handAnchors(room.female(),"_R")};
    std::vector<std::vector<Vec3>> initialDirections(4);
    for (int h=0;h<4;++h) {
        assert(!hands[h].carpal.empty() && hands[h].digits.size()>=14);
        const auto pose=(h<2 ? room.male() : room.female()).sourceMatrices({},0);
        for (const auto& digit:hands[h].digits) initialDirections[h].push_back(relativeDirection(pose,digit));
    }
    double handGap[4]={},palmGap[4]={},motion[4]={},handFloor[2]={};
    const auto malePose=room.male().sourceMatrices({},0),femalePose=room.female().sourceMatrices({},0);
    assert(maleAnchors.size()>90 && femaleAnchors.size()>90);
    // Rendering can run faster than the fixed physics rate. Do not invalidate
    // either pose until accumulated time actually produces a physics step.
    assert(room.toggleRagdoll() && room.ragdollActive());
    const auto maleRevision=room.male().sourcePoseRevision(),femaleRevision=room.female().sourcePoseRevision();
    const auto beforeMale=room.male().sourceMatrices({},0),beforeFemale=room.female().sourceMatrices({},0);
    for (int i=0;i<3;++i) room.advanceRagdoll(0.001);
    assert(room.male().sourcePoseRevision()==maleRevision && room.female().sourcePoseRevision()==femaleRevision);
    assert(room.male().sourceMatrices({},0)==beforeMale && room.female().sourceMatrices({},0)==beforeFemale);
    room.advanceRagdoll(0.006);
    assert(room.male().sourcePoseRevision()>maleRevision && room.female().sourcePoseRevision()>femaleRevision);
    assert(room.toggleRagdoll() && !room.ragdollActive());
    assert(room.toggleRagdoll() && room.ragdollActive());
    for (const auto& pair : {std::make_pair(malePose,room.male().sourceMatrices({},0)),
                             std::make_pair(femalePose,room.female().sourceMatrices({},0))})
        for (std::size_t i=0;i<pair.first.size();++i) for (int j=0;j<16;++j)
            assert(std::abs(pair.first[i][j]-pair.second[i][j])<1e-5);
    int selfContacts=0, floorContacts=0;
    double maxSeparation=0, maxForearm=0, maxCartilage=0, lowestMale=maleStart.first, lowestFemale=femaleStart.first;
    for (int i=0;i<600;++i) {
        room.advanceRagdoll(1.0/120.0);
        for (int h=0;h<4;++h) {
            const auto& model=h<2 ? room.male() : room.female();
            handGap[h]=std::max(handGap[h],maxGap(model,hands[h].digits));
            palmGap[h]=std::max(palmGap[h],maxGap(model,hands[h].carpal));
            const auto pose=model.sourceMatrices({},0);
            for (std::size_t d=0;d<hands[h].digits.size();++d)
                motion[h]=std::max(motion[h],length(relativeDirection(pose,hands[h].digits[d])-initialDirections[h][d]));
        }
        handFloor[0]=std::min(handFloor[0],handLow(room.male())-maleStart.first);
        handFloor[1]=std::min(handFloor[1],handLow(room.female())-femaleStart.first);
        const auto contacts=room.ragdoll()->contacts();
        selfContacts+=contacts.sameFigure; floorContacts+=contacts.floor;
        if ((i+1)%60==0) {
            maxSeparation=std::max({maxSeparation,maxGap(room.male(),maleAnchors),maxGap(room.female(),femaleAnchors)});
            maxForearm=std::max({maxForearm,maxGap(room.male(),maleForearms),maxGap(room.female(),femaleForearms)});
            maxCartilage=std::max({maxCartilage,maxGap(room.male(),maleCartilage),maxGap(room.female(),femaleCartilage)});
            lowestMale=std::min(lowestMale,extent(room.male()).first);
            lowestFemale=std::min(lowestFemale,extent(room.female()).first);
        }
    }
    std::printf("contacts: floor %d, non-neighbour bones %d; max joint gap %.4f; penetration %.4f / %.4f m\n",
        floorContacts,selfContacts,maxSeparation,maleStart.first-lowestMale,femaleStart.first-lowestFemale);
    std::printf("max forearm separation %.6f m; cartilage displacement %.4f m\n",maxForearm,maxCartilage);
    for (int h=0;h<4;++h) {
        std::printf("hand %d: palm %.6f m, digit %.4f m, motion %.3f\n",h,palmGap[h],handGap[h],motion[h]);
        assert(palmGap[h]<0.0001 && handGap[h]<0.0001 && motion[h]>0.05);
    }
    assert(handFloor[0]>-0.02 && handFloor[1]>-0.02);
    assert(maxForearm<0.0001 && maxCartilage<0.012);
    assert(maxSeparation<0.03 && lowestMale>maleStart.first-0.03 && lowestFemale>femaleStart.first-0.03);
    assert(floorContacts>0 && selfContacts>0);
    const auto male=extent(room.male()),female=extent(room.female());
    const double mg=maxGap(room.male(),maleAnchors),fg=maxGap(room.female(),femaleAnchors);
    std::printf("ragdoll heights male %.3f -> %.3f, female %.3f -> %.3f; joint gaps %.4f / %.4f m\n",
        maleStart.second-maleStart.first,male.second-male.first,femaleStart.second-femaleStart.first,
        female.second-female.first,mg,fg);
    assert(male.second-male.first<1.0 && female.second-female.first<1.0);
    assert(male.first>maleStart.first-0.03 && female.first>femaleStart.first-0.03);
    assert(mg<0.01 && fg<0.01);
    assert(room.toggleRagdoll() && !room.ragdollActive());
    assert(std::abs(extent(room.male()).second-maleStart.second)<1e-5);
    {
        SkeletonRagdoll closeFigures;
        assert(closeFigures.create(room.male(),{-0.45,-maleStart.first,0},room.female(),{0.45,-femaleStart.first,0}));
        int crossContacts=0;
        for (int i=0;i<360;++i) { closeFigures.advance(1.0/120.0); crossContacts+=closeFigures.contacts().otherFigure; }
        std::printf("inter-figure bone contacts %d\n",crossContacts);
        assert(crossContacts>0);
    }
    assert(room.restoreOriginalPose());
    const auto originalMale=room.male().sourceMatrices({},0),originalFemale=room.female().sourceMatrices({},0);
    assert(room.toggleRagdoll());
    for (const auto& pair : {std::make_pair(originalMale,room.male().sourceMatrices({},0)),
                             std::make_pair(originalFemale,room.female().sourceMatrices({},0))})
        for (std::size_t i=0;i<pair.first.size();++i) for (int j=0;j<16;++j)
            assert(std::abs(pair.first[i][j]-pair.second[i][j])<1e-5);
    const auto sourceMale=pairedAnchors(room.male(),true), sourceFemale=pairedAnchors(room.female(),true);
    for (int i=0;i<120;++i) room.advanceRagdoll(1.0/120.0);
    assert(maxGap(room.male(),sourceMale)<0.0001 && maxGap(room.female(),sourceFemale)<0.0001);
    assert(room.restoreOriginalPose() && !room.ragdollActive());
    assert(room.restoreReferencePose());
    assert(room.adjustReferenceGroup('e',45) && room.adjustReferenceGroup('m',30));
    const auto bentMale=pairedAnchors(room.male(),true),bentFemale=pairedAnchors(room.female(),true);
    assert(room.toggleRagdoll());
    for (int i=0;i<120;++i) room.advanceRagdoll(1.0/120.0);
    assert(maxGap(room.male(),bentMale)<0.0001 && maxGap(room.female(),bentFemale)<0.0001);
}
