#pragma once

#include "client/skeleton_toe_manual_reference.h"

namespace gtc::native {
// A marked surface point remains in the corresponding blue bone's local mesh
// frame. Both points in each joint are carried independently during animation.
struct MarkedBlueToeJoint { std::size_t parent,child; Vec3 parentPoint,childPoint; };
struct MarkedBlueToeReference {
    std::vector<std::array<double,16>> base,corrected;
    std::vector<std::size_t> bones;
    std::vector<MarkedBlueToeJoint> joints;
};
inline JointRigidTransform markedShaftTurn(Vec3 from,Vec3 to) {
    from=normalized(from);to=normalized(to);
    Vec3 axis=cross(from,to);const double sine=length(axis),cosine=std::clamp(dot(from,to),-1.0,1.0);
    if(sine>1e-9)return joint_detail::rotate(axis/sine,std::atan2(sine,cosine));
    if(cosine<0) {
        axis=cross(from,std::abs(from.x)<.8?Vec3{1,0,0}:Vec3{0,1,0});
        return joint_detail::rotate(normalized(axis),3.14159265358979323846);
    }
    return {};
}
// The two hand-marked B shafts are transported through the same source-to-blue
// registration and profile scaling used for their bones. Their reflected mean
// defines the blue shaft; it does not force neighbouring toes to be parallel.
inline std::optional<Vec3> markedBlueDirection(const SkeletonComparison& c,const SkeletonModel& model,
    const ToeSurfaceMarks& source,const std::vector<std::array<double,16>>& white,int digit,int segment) {
    Vec3 sum{};int count=0;
    for(char side:{'L','R'}) {
        ToeEdit e;e.digit=digit;e.segment=segment;e.side=side;
        const auto name=toeEditName(e,segment);const auto m=source.find(name);
        const int n=model.sourceStructure(name);
        if(m==source.end() || m->second.placed!=3 || n<0 || std::size_t(n)>=white.size() || std::size_t(n)>=c.parts().size())continue;
        const auto i=std::size_t(n);const auto& part=c.parts()[i];const auto& reg=c.metatarsalFrame(i);
        const auto map=[&](Vec3 p) {return c.mapPart(frameVector(reg,toePoint(white[i],p))+reg.translation,part);};
        Vec3 direction=map(m->second.end[1])-map(m->second.end[0]);
        if(length(direction)<1e-8)continue;
        direction=normalized(direction);if(side=='R')direction.x=-direction.x;
        sum+=direction;++count;
    }
    if(!count || length(sum)<1e-8)return std::nullopt;
    return normalized(sum);
}
inline MarkedBlueToeReference buildMarkedBlueToeReference(const SkeletonComparison& c,const SkeletonModel& model,
    const ToeSurfaceMarks& source,const ToeSurfaceMarks& mapped,const std::vector<std::array<double,16>>& white,Vec3 origin) {
    MarkedBlueToeReference result;result.base=c.toePose(white,origin);result.corrected=result.base;
    for(int digit=1;digit<=5;++digit) {
        ToeEdit e;e.digit=digit;e.side='L';
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;
        for(int s=0;s<count;++s) {
            const auto name=toeEditName(e,order[s]);const auto mark=mapped.find(name);
            const int left=model.sourceStructure(name);
            e.side='R';const int right=model.sourceStructure(toeEditName(e,order[s]));e.side='L';
            const auto target=markedBlueDirection(c,model,source,white,digit,order[s]);
            if(mark==mapped.end() || mark->second.placed!=3 || !target || left<0 || right<0 ||
               std::size_t(left)>=result.corrected.size() || std::size_t(right)>=result.corrected.size())continue;
            const auto i=std::size_t(left),j=std::size_t(right);
            const Vec3 start=mark->second.end[0]+origin,end=mark->second.end[1]+origin;
            const Vec3 posedStart=toePoint(result.corrected[i],start);
            const Vec3 shaft=toePoint(result.corrected[i],end)-posedStart;
            if(length(shaft)<1e-8)continue;
            auto turn=markedShaftTurn(shaft,*target);
            Vec3 attachment=posedStart;
            if(s>0) {
                const auto previous=mapped.find(toeEditName(e,order[s-1]));
                const int parent=model.sourceStructure(toeEditName(e,order[s-1]));
                if(previous==mapped.end() || previous->second.placed!=3 || parent<0)continue;
                attachment=toePoint(result.corrected[std::size_t(parent)],previous->second.end[1]+origin);
                result.joints.push_back({std::size_t(parent),i,previous->second.end[1]+origin,start});
                e.side='R';const int parentRight=model.sourceStructure(toeEditName(e,order[s-1]));e.side='L';
                const Vec3 a=toeMirrorPoint(previous->second.end[1])+origin,b=toeMirrorPoint(mark->second.end[0])+origin;
                if(parentRight>=0)result.joints.push_back({std::size_t(parentRight),j,a,b});
            }
            turn.translation=attachment-frameVector(turn,posedStart);
            result.corrected[i]=toeMatrix(joint_detail::compose(turn,toeRigid(result.corrected[i])));
            result.corrected[j]=toeMatrix(toeMirrorTransform(toeRigid(result.corrected[i]),origin));
            result.bones.push_back(i);result.bones.push_back(j);
        }
    }
    return result;
}
inline void animateMarkedBlueToes(const MarkedBlueToeReference& ref,std::vector<std::array<double,16>>& pose) {
    for(const auto i:ref.bones)if(i<pose.size() && i<ref.base.size() && i<ref.corrected.size()) {
        const auto motion=joint_detail::compose(toeRigid(pose[i]),joint_detail::inverse(toeRigid(ref.base[i])));
        pose[i]=toeMatrix(joint_detail::compose(motion,toeRigid(ref.corrected[i])));
    }
    for(const auto& joint:ref.joints)if(joint.parent<pose.size() && joint.child<pose.size()) {
        const Vec3 shift=toePoint(pose[joint.parent],joint.parentPoint)-toePoint(pose[joint.child],joint.childPoint);
        pose[joint.child][12]+=shift.x;pose[joint.child][13]+=shift.y;pose[joint.child][14]+=shift.z;
    }
}
} // namespace gtc::native
