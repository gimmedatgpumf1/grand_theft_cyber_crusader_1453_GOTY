#pragma once

#include "client/skeleton_toe_editor.h"
#include "client/skeleton_toe_surface.h"
#include <map>

namespace gtc::native {
// Mesh-coordinate attachments supplied by the user on the unposed CT surface.
// A partial mark never authorises a pose correction.
struct ToeSurfaceMark { std::array<Vec3,2> end{}; unsigned placed=0; };
using ToeSurfaceMarks=std::map<std::string,ToeSurfaceMark>;

inline std::optional<JointRigidTransform> toeSurfaceAlignment(Vec3 parentStart,Vec3 parentEnd,
                                                               Vec3 childStart,Vec3 childEnd) {
    const Vec3 a=parentEnd-parentStart,b=childEnd-childStart;
    if(length(a)<1e-6 || length(b)<1e-6)return std::nullopt;
    const Vec3 target=normalized(a),source=normalized(b);Vec3 axis=cross(source,target);
    const double sine=length(axis),cosine=std::clamp(dot(source,target),-1.0,1.0);
    JointRigidTransform turn;
    if(sine>1e-9)turn=joint_detail::rotate(axis/sine,std::atan2(sine,cosine));
    else if(cosine<0) {
        axis=cross(source,std::abs(source.x)<.8?Vec3{1,0,0}:Vec3{0,1,0});
        turn=joint_detail::rotate(normalized(axis),3.14159265358979323846);
    }
    turn.translation=parentEnd-frameVector(turn,childStart);
    return turn;
}
inline bool applySurfaceToeCalibration(SkeletonModel& model,const ToeSurfaceMarks& marks) {
    for(char side:{'L','R'})for(int digit=1;digit<=5;++digit) {
        ToeEdit e;e.side=side;e.digit=digit;
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;
        for(int s=1;s<count;++s) {
            const auto parentName=toeEditName(e,order[s-1]),childName=toeEditName(e,order[s]);
            const auto a=marks.find(parentName),b=marks.find(childName);
            if(a==marks.end() || b==marks.end() || a->second.placed!=3 || b->second.placed!=3)continue;
            const int parent=model.sourceStructure(parentName),child=model.sourceStructure(childName);
            const auto edge=toeEditLink(model,parentName,childName);
            if(parent<0 || child<0 || edge==model.sourceLinks().size())return false;
            const auto pose=model.sourceMatrices({},0);
            const auto turn=toeSurfaceAlignment(toePoint(pose[parent],a->second.end[0]),toePoint(pose[parent],a->second.end[1]),
                                                 toePoint(pose[child],b->second.end[0]),toePoint(pose[child],b->second.end[1]));
            if(!turn)return false;
            const Vec3 pivot=toePoint(pose[parent],a->second.end[1]);
            if(!model.adjustReferenceToe(edge,*turn) || !model.repositionReferenceToeJoint(edge,pivot))return false;
        }
    }
    return true;
}
} // namespace gtc::native
