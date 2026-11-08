#pragma once

#include "client/skeleton_body.h"
#include "client/skeleton_lines.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace gtc::native {
// The V-view bones are measured from the source surfaces. The earlier toe
// pose aligned the *centres* of consecutive phalanges but did not align the
// individual shafts: in particular, some middle bones ended up sideways.
// Rotate outlying IP segments and connect their source shaft endpoints,
// carrying each segment’s children. Aim yaw along that toe’s original centre
// ray and pitch relative to its proximal shaft. CT vertices are unchanged.
inline bool fitReferenceToeShafts(SkeletonModel& model,const SkeletonLines& lines,bool female=false) {
    constexpr double pi=3.14159265358979323846;
    const auto vector=[](const std::array<double,16>& m,Vec3 v) {
        return Vec3{m[0]*v.x+m[4]*v.y+m[8]*v.z,m[1]*v.x+m[5]*v.y+m[9]*v.z,m[2]*v.x+m[6]*v.y+m[10]*v.z};
    };
    const auto point=[&](const std::array<double,16>& m,Vec3 v) { return vector(m,v)+Vec3{m[12],m[13],m[14]}; };
    const auto link=[&](int a,int b) {
        for(std::size_t i=0;i<model.sourceLinks().size();++i) {
            const auto& e=model.sourceLinks()[i];
            if((e.first==std::size_t(a)&&e.second==std::size_t(b)) || (e.first==std::size_t(b)&&e.second==std::size_t(a)))return i;
        }
        return model.sourceLinks().size();
    };
    for(char side:{'L','R'})for(int digit=1;digit<=5;++digit) {
        const std::string suffix=std::to_string(digit)+"_"+side;
        const int proximal=model.sourceStructure("FootProxP"+suffix);
        const int middle=digit==1?-1:model.sourceStructure("FootMidP"+suffix);
        const int distal=model.sourceStructure("FootDistP"+suffix);
        if(proximal<0 || distal<0 || (digit!=1 && middle<0))return false;
        const std::array<int,3> chain={proximal,middle,distal};
        const auto baseline=model.sourceMatrices({},0);
        const auto center=[&](int index) {
            const auto& part=model.sourceStructures()[std::size_t(index)];Vec3 sum{};
            const auto& vertices=model.bones()[part.group].points;
            for(std::size_t i=0;i<part.pointCount;++i)sum+=point(baseline[std::size_t(index)],vertices[part.firstPoint+i]);
            return sum/double(part.pointCount);
        };
        const Vec3 centerRay=center(distal)-center(proximal);
        const double toeYaw=std::atan2(centerRay.x,centerRay.z);
        bool parentChanged=false;
        for(int k=digit==1?2:1;k<3;++k) {
            const int parent=k==1 || digit==1?proximal:middle,child=chain[k];
            const auto source=lines.sourceBoneDirection(model.sourceStructures()[std::size_t(child)].name);
            const auto guide=lines.sourceBoneDirection(model.sourceStructures()[std::size_t(proximal)].name);
            const auto edge=link(parent,child);if(!source || !guide || edge==model.sourceLinks().size())return false;
            const auto pose=model.sourceMatrices({},0);
            Vec3 forward=normalized(vector(pose[std::size_t(proximal)],*guide));
            Vec3 direction=normalized(vector(pose[std::size_t(child)],*source));
            if(dot(forward,Vec3{0,0,1})<0)forward=forward*-1;
            if(dot(direction,forward)<0)direction=direction*-1;
            const double yaw=std::atan2(direction.x,direction.z),baseYaw=toeYaw;
            const double pitch=std::atan2(direction.y,std::hypot(direction.x,direction.z));
            const double basePitch=std::atan2(forward.y,std::hypot(forward.x,forward.z));
            const double targetYaw=baseYaw+std::clamp(std::remainder(yaw-baseYaw,2*pi),-5*pi/180,5*pi/180);
            const double targetPitch=basePitch+std::clamp(pitch-basePitch,-20*pi/180,20*pi/180);
            const Vec3 target{std::sin(targetYaw)*std::cos(targetPitch),std::sin(targetPitch),std::cos(targetYaw)*std::cos(targetPitch)};
            const Vec3 axis=cross(direction,target);const double sine=length(axis);
            const double yawError=std::abs(std::remainder(yaw-baseYaw,2*pi));
            const bool sideways=yawError>15*pi/180 || std::abs(pitch-basePitch)>25*pi/180;
            if(!sideways && !parentChanged) {
                const auto& a=model.sourceStructures()[std::size_t(parent)];
                const auto& b=model.sourceStructures()[std::size_t(child)];
                double best=1e100;Vec3 nearestA{},nearestB{};
                for(std::size_t i=0;i<a.pointCount;++i) {
                    const Vec3 x=point(pose[std::size_t(parent)],model.bones()[a.group].points[a.firstPoint+i]);
                    for(std::size_t j=0;j<b.pointCount;++j) {
                        const Vec3 y=point(pose[std::size_t(child)],model.bones()[b.group].points[b.firstPoint+j]);
                        const double d=dot(x-y,x-y);if(d<best){best=d;nearestA=x;nearestB=y;}
                    }
                }
                const double gap=std::sqrt(best);
                if(gap>.0012) {
                    JointRigidTransform shift;shift.translation=(nearestA-nearestB)*((gap-.0004)/gap);
                    if(!model.adjustReferenceToe(edge,shift) || !model.repositionReferenceToeJoint(edge,nearestA+(nearestB+shift.translation-nearestA)*.5))return false;
                    parentChanged=true;
                }
                continue;
            }
            const auto parentEnds=lines.sourceBoneEndpoints(model.sourceStructures()[std::size_t(parent)].name);
            const auto childEnds=lines.sourceBoneEndpoints(model.sourceStructures()[std::size_t(child)].name);
            if(!parentEnds || !childEnds)return false;
            auto turn=sine>1e-9?joint_detail::rotate(axis/sine,std::atan2(sine,std::clamp(dot(direction,target),-1.0,1.0))):JointRigidTransform{};
            Vec3 pa=point(pose[std::size_t(parent)],(*parentEnds)[0]),pb=point(pose[std::size_t(parent)],(*parentEnds)[1]);
            Vec3 ca=point(pose[std::size_t(child)],(*childEnds)[0]),cb=point(pose[std::size_t(child)],(*childEnds)[1]);
            const Vec3 end=dot(pa,forward)>dot(pb,forward)?pa:pb;
            const Vec3 start=dot(ca,forward)<dot(cb,forward)?ca:cb;
            // Bring the opposing shaft ends together; turning around a bad
            // nearest-surface pivot alone leaves the bone beside its parent.
            turn.translation=end+forward*.0005-frameVector(turn,start);
            if(!model.adjustReferenceToe(edge,turn))return false;
            if(!model.repositionReferenceToeJoint(edge,end+forward*.00025))return false;
            parentChanged=true;
        }
    }
    // The left female third and fourth rays converge in the CT. A small
    // medial MTP turn keeps those adjacent toes apart without displacing
    // individual phalanges or changing the metatarsal.
    if(female) {
        const int meta=model.sourceStructure("FootMeta3_L"),prox=model.sourceStructure("FootProxP3_L");
        if(meta<0 || prox<0)return false;
        const auto edge=link(meta,prox);
        const auto axis=lines.sourceBoneDirection("FootProxP3_L");
        if(edge==model.sourceLinks().size() || !axis || !model.referenceToeYaw(edge))return false;
        const auto yaw=[&]() {const auto pose=model.sourceMatrices({},0);Vec3 d=vector(pose[std::size_t(prox)],*axis);
            if(d.z<0)d=d*-1;return std::atan2(d.x,d.z)*180/pi;};
        const double current=*model.referenceToeYaw(edge),before=yaw();
        if(!model.setReferenceToeYaw(edge,current+1))return false;
        const double slope=std::remainder(yaw()-before,360.0);
        if(std::abs(slope)<1e-5 || !model.setReferenceToeYaw(edge,current-5.0/slope))return false;
    }
    return true;
}
} // namespace gtc::native
