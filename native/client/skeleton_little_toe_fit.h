#pragma once

#include "client/skeleton_body.h"
#include "client/skeleton_finger_frame.h"
#include <algorithm>
#include <array>

namespace gtc::native {
// Correct the female little-toe CT's misregistered PIP and DIP pieces as rigid
// segments. Keep each joint and its children together; source vertices stay
// unchanged and restoring the source pose removes these registration offsets.
inline bool fitFemaleLittleToe(SkeletonModel& model) {
    const auto point=[](const std::array<double,16>& m,Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    const auto vertices=[&](int id,const auto& pose) {
        const auto& s=model.sourceStructures()[id];std::vector<Vec3> result;result.reserve(s.pointCount);
        for(std::size_t i=0;i<s.pointCount;++i)result.push_back(point(pose[id],model.bones()[s.group].points[s.firstPoint+i]));
        return result;
    };
    const auto mean=[](const std::vector<Vec3>& p) {Vec3 c{};for(Vec3 v:p)c+=v;return c/double(p.size());};
    const auto findLink=[&](int a,int b) {
        for(std::size_t i=0;i<model.sourceLinks().size();++i) {const auto& e=model.sourceLinks()[i];
            if((e.first==std::size_t(a)&&e.second==std::size_t(b))||(e.second==std::size_t(a)&&e.first==std::size_t(b)))return i;}
        return model.sourceLinks().size();
    };
    for(char side:{'L','R'}) {
        const std::string suffix=std::string("_")+side;
        const int prox=model.sourceStructure("FootProxP5"+suffix),mid=model.sourceStructure("FootMidP5"+suffix),tip=model.sourceStructure("FootDistP5"+suffix);
        if(prox<0 || mid<0 || tip<0)return false;
        const auto ip=findLink(prox,mid),dip=findLink(mid,tip);
        if(ip==model.sourceLinks().size() || dip==model.sourceLinks().size())return false;
        const auto initial=model.sourceMatrices({},0);const auto first=vertices(prox,initial);
        if(first.empty())return false;
        // Follow the proximal-to-middle ray in the ground plane, with a small
        // downward incline instead of curling the distal bones under the foot.
        const auto middle=vertices(mid,initial);if(middle.empty())return false;
        Vec3 ray=mean(middle)-mean(first);ray.y=0;ray=normalized(ray);
        if(length(ray)<1e-8)return false;
        for(auto pair:{std::array<int,3>{int(ip),prox,mid},std::array<int,3>{int(dip),mid,tip}}) {
            const auto pose=model.sourceMatrices({},0);
            auto parent=vertices(pair[1],pose),child=vertices(pair[2],pose);
            if(parent.empty() || child.empty())return false;
            const Vec3 center=mean(child);
            // Keep the individual little toe's ray; the almost round
            // phalanges should follow a modest natural downward angle rather
            // than being forced into a single level direction.
            const double pitch=(pair[2]==mid?-10.0:-15.0)*3.14159265358979323846/180.0;
            const Vec3 target=normalized(ray*std::cos(pitch)+Vec3{0,std::sin(pitch),0});
            Vec3 shaft=FingerReferenceFrames::shaft(child.data(),child.size(),center,target);
            Vec3 crossAxis=cross(shaft,target);const double sine=length(crossAxis);
            JointRigidTransform correction;
            if(sine>1e-9)correction=joint_detail::rotate(crossAxis/sine,std::atan2(sine,std::clamp(dot(shaft,target),-1.0,1.0)));
            // Orient around the child's centre first, then connect it to the
            // preceding phalanx along the same ray with a small articular gap.
            correction.translation=center-frameVector(correction,center);
            double end=-1e100,begin=1e100;
            for(Vec3 p:parent)end=std::max(end,dot(p,ray));
            for(Vec3 p:child)begin=std::min(begin,dot(frameVector(correction,p)+correction.translation,ray));
            const double gap=.0002*std::clamp(length(mean(middle)-mean(first))/.015,.5,2.0);
            correction.translation+=ray*(end+gap-begin);
            if(pair[2]==tip) {
                // Keep the distal phalanx near the middle one's height.
                const Vec3 parentCenter=mean(parent),childCenter=frameVector(correction,center)+correction.translation;
                correction.translation.x+=std::clamp(parentCenter.x-(side=='L'?.0005:-.0005)-childCenter.x,-.006,.006);
                correction.translation.y+=std::clamp(parentCenter.y-.001-childCenter.y,-.004,.004);
            }
            if(!model.adjustReferenceToe(std::size_t(pair[0]),correction))return false;
            // Recenter the hinge on the registered surfaces, preserving the
            // reference pose but keeping future bending and ragdoll anchors
            // attached to the corrected joint rather than its old CT pivot.
            const auto registered=model.sourceMatrices({},0);
            parent=vertices(pair[1],registered);child=vertices(pair[2],registered);
            double nearest=1e100;Vec3 joint{};
            for(Vec3 a:parent)for(Vec3 b:child) {
                const double d=dot(a-b,a-b);
                if(d<nearest){nearest=d;joint=(a+b)*.5;}
            }
            if(!model.repositionReferenceToeJoint(std::size_t(pair[0]),joint))return false;
        }
    }
    return true;
}
} // namespace gtc::native
