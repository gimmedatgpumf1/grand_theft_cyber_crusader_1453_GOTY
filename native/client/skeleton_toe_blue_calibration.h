#pragma once

#include "client/skeleton_toe_calibration.h"
#include <set>

namespace gtc::native {
// Transfer each user-picked CT point to the actual averaged blue surface.
// The registered CT frame is only a correspondence; the final point is on a
// blue triangle, never a point inside its volume.
inline ToeSurfaceMarks mapBlueToeMarks(const SkeletonComparison& c,const SkeletonModel& model,const ToeSurfaceMarks& source) {
    ToeSurfaceMarks result;
    for(const auto& [name,mark]:source) {
        if(mark.placed!=3)continue;
        const int n=model.sourceStructure(name);if(n<0 || std::size_t(n)>=c.parts().size())continue;
        const auto i=std::size_t(n);const auto& raw=model.sourceStructures()[i];const auto& part=c.parts()[i];
        if(!raw.pointCount || part.points.empty())continue;
        Vec3 rawCenter{},blueCenter{};
        for(std::size_t j=0;j<raw.pointCount;++j)rawCenter+=model.bones()[raw.group].points[raw.firstPoint+j];
        for(Vec3 p:part.points)blueCenter+=p;
        rawCenter=rawCenter/double(raw.pointCount);blueCenter=blueCenter/double(part.points.size());
        const auto& ref=c.littleToeSkinReference()[i];const auto& reg=c.metatarsalFrame(i);
        const auto registered=[&](Vec3 p){return frameVector(reg,toePoint(ref,p))+reg.translation;};
        const Vec3 offset=blueCenter-registered(rawCenter);
        std::vector<Vec3> vertices;vertices.reserve(part.points.size());
        for(Vec3 p:part.points)vertices.push_back(c.mapPart(p,part));
        ToeSurfaceMark mapped{};mapped.placed=3;
        for(int j=0;j<2;++j) {
            const Vec3 guess=c.mapPart(registered(mark.end[j])+offset,part);
            const auto closest=toeClosestSurface(guess,vertices,part.faces);
            if(!closest){mapped.placed=0;break;}mapped.end[j]=*closest;
        }
        if(mapped.placed)result[name]=mapped;
    }
    return result;
}
inline std::array<double,16> toeMatrix(const JointRigidTransform& t) {
    const auto& r=t.rotation;
    return {r[0],r[3],r[6],0,r[1],r[4],r[7],0,r[2],r[5],r[8],0,t.translation.x,t.translation.y,t.translation.z,1};
}
inline JointRigidTransform toeMirrorTransform(JointRigidTransform t,Vec3 origin) {
    constexpr double s[3]={-1,1,1};
    for(int a=0;a<3;++a)for(int b=0;b<3;++b)t.rotation[a*3+b]*=s[a]*s[b];
    t.translation.x=-t.translation.x;
    const Vec3 offset{2*origin.x,0,0};t.translation+=offset-frameVector(t,offset);
    return t;
}
inline Vec3 toeMirrorPoint(Vec3 p) {return {-p.x,p.y,p.z};}
struct BlueToeJoint {std::size_t parent,child;Vec3 parentPoint,childPoint;};
inline char blueToeMarkedSide(const ToeSurfaceMarks& marks,int digit) {
    const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
    const int count=digit==1?3:4;
    const auto score=[&](char side) {
        ToeEdit e;e.digit=digit;e.side=side;int bones=0,joints=0;bool previous=false;
        for(int i=0;i<count;++i) {
            const bool present=marks.contains(toeEditName(e,order[i]));
            if(present)++bones;
            if(present && previous)++joints;
            previous=present;
        }
        return joints*10+bones;
    };
    return score('R')>score('L')?'R':'L';
}
// Build a neutral pose from the marked side, then mirror its fixed correction.
// Both local attachment points lie on their respective averaged bone surfaces.
inline std::vector<BlueToeJoint> calibrateBlueToeNeutral(const SkeletonComparison& c,const ToeSurfaceMarks& marks,
                                                          Vec3 origin,std::vector<std::array<double,16>>& pose) {
    std::vector<BlueToeJoint> joints;
    const auto index=[&](const std::string& name) {
        for(std::size_t i=0;i<c.parts().size();++i)if(c.parts()[i].name==name)return i;
        return c.parts().size();
    };
    for(int digit=1;digit<=5;++digit) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;
        // Use the more complete side for each toe; one side drives both.
        ToeEdit e;e.digit=digit;e.side=blueToeMarkedSide(marks,digit);
        for(int step=1;step<count;++step) {
            const auto parentName=toeEditName(e,order[step-1]),childName=toeEditName(e,order[step]);
            const auto a=marks.find(parentName),b=marks.find(childName);
            if(a==marks.end() || b==marks.end() || a->second.placed!=3 || b->second.placed!=3)continue;
            const auto parent=index(parentName),child=index(childName);
            if(parent>=pose.size() || child>=pose.size())continue;
            const Vec3 ap=a->second.end[0]+origin,aq=a->second.end[1]+origin;
            const Vec3 bp=b->second.end[0]+origin,bq=b->second.end[1]+origin;
            const auto correction=toeSurfaceAlignment(toePoint(pose[parent],ap),toePoint(pose[parent],aq),
                                                       toePoint(pose[child],bp),toePoint(pose[child],bq));
            if(!correction)continue;
            pose[child]=toeMatrix(joint_detail::compose(*correction,toeRigid(pose[child])));
            joints.push_back({parent,child,aq,bp});
            ToeEdit other=e;other.side=e.side=='L'?'R':'L';
            const auto parentMirror=index(toeEditName(other,order[step-1])),childMirror=index(toeEditName(other,order[step]));
            if(parentMirror>=pose.size() || childMirror>=pose.size())continue;
            pose[childMirror]=toeMatrix(toeMirrorTransform(toeRigid(pose[child]),origin));
            joints.push_back({parentMirror,childMirror,toeMirrorPoint(a->second.end[1])+origin,toeMirrorPoint(b->second.end[0])+origin});
        }
    }
    return joints;
}
// Preserve independent per-side animation after applying the calibrated rest
// frame, then close each joint at the two user-marked surface points.
inline void animateBlueToeCalibration(const std::vector<std::array<double,16>>& base,
    const std::vector<std::array<double,16>>& corrected,std::vector<std::array<double,16>>& current,
    const std::vector<BlueToeJoint>& joints) {
    std::set<std::size_t> children;
    for(const auto& joint:joints)children.insert(joint.child);
    for(std::size_t i:children)if(i<base.size() && i<current.size() && i<corrected.size()) {
        const auto motion=joint_detail::compose(toeRigid(current[i]),joint_detail::inverse(toeRigid(base[i])));
        current[i]=toeMatrix(joint_detail::compose(motion,toeRigid(corrected[i])));
    }
    for(const auto& joint:joints) {
        const Vec3 target=toePoint(current[joint.parent],joint.parentPoint);
        const Vec3 actual=toePoint(current[joint.child],joint.childPoint),shift=target-actual;
        current[joint.child][12]+=shift.x;current[joint.child][13]+=shift.y;current[joint.child][14]+=shift.z;
    }
}
} // namespace gtc::native
