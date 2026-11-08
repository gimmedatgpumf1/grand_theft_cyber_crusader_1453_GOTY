#pragma once
#include "client/skeleton_toe_blue_calibration.h"
#include <algorithm>

namespace gtc::native {
// The user's B-pose edits are stored relative to the unmodified CT bone frame.
// Replaying those exact local transforms in the standing frame makes editing a
// source bone meaningful outside the editor without changing the CT vertices.
struct ManualToeReference {
    std::vector<JointRigidTransform> local;
    std::vector<bool> edited;
};
inline bool captureManualToeReference(SkeletonModel& model,const std::vector<ToeEdit>& edits,bool male,
                                      ManualToeReference& reference) {
    if(!model.restoreSourcePose())return false;
    const auto base=model.sourceMatrices({},0);
    reference.local.assign(base.size(),{});reference.edited.assign(base.size(),false);
    for(const auto& edit:edits)if(edit.original && !edit.blue && edit.male==male) {
        const int i=model.sourceStructure(toeEditName(edit,edit.segment));
        if(i<0 || !applyWhiteToeEdit(model,edit))return false;
        reference.edited[std::size_t(i)]=true;
    }
    const auto changed=model.sourceMatrices({},0);
    if(changed.size()!=base.size())return false;
    for(std::size_t i=0;i<base.size();++i)if(reference.edited[i])
        reference.local[i]=joint_detail::compose(joint_detail::inverse(toeRigid(base[i])),toeRigid(changed[i]));
    return true;
}
inline bool applyManualToeReference(SkeletonModel& model,const ManualToeReference& reference,bool male) {
    // Counter-move descendants for each bone; apply from proximal to distal.
    for(int segment=-1;segment<=2;++segment)for(char side:{'L','R'})for(int digit=1;digit<=5;++digit) {
        ToeEdit edit;edit.male=male;edit.side=side;edit.digit=digit;edit.segment=segment;
        if(!validToeEdit(edit))continue;
        const int i=model.sourceStructure(toeEditName(edit,segment));
        if(i<0 || std::size_t(i)>=reference.edited.size() || !reference.edited[std::size_t(i)])continue;
        const auto pose=model.sourceMatrices({},0);const auto frame=toeRigid(pose[std::size_t(i)]);
        const auto turn=joint_detail::compose(joint_detail::compose(frame,reference.local[std::size_t(i)]),joint_detail::inverse(frame));
        if(!applyWhiteToeTransform(model,edit,turn))return false;
    }
    return true;
}
inline bool hasManualToe(const SkeletonModel& model,const ManualToeReference& ref,int digit) {
    for(char side:{'L','R'})for(int segment=-1;segment<3;++segment) {
        ToeEdit e;e.side=side;e.digit=digit;e.segment=segment;if(!validToeEdit(e))continue;
        const int i=model.sourceStructure(toeEditName(e,segment));
        if(i>=0 && std::size_t(i)<ref.edited.size() && ref.edited[std::size_t(i)])return true;
    }
    return false;
}
// Place the kinematic pivot on the marked parent surface without moving the
// bones. Ragdoll anchors retain both independently marked surface points.
inline bool setMarkedToeSurfacePivots(SkeletonModel& model,const ToeSurfaceMarks& marks) {
    for(int digit=1;digit<=5;++digit)for(char side:{'L','R'}) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;ToeEdit e;e.side=side;e.digit=digit;
        for(int s=1;s<count;++s) {
            const auto parent=toeEditName(e,order[s-1]),child=toeEditName(e,order[s]);
            const auto a=marks.find(parent),b=marks.find(child);
            if(a==marks.end() || b==marks.end() || !(a->second.placed&2) || !(b->second.placed&1))continue;
            const int i=model.sourceStructure(parent),j=model.sourceStructure(child);if(i<0 || j<0)continue;
            const auto pose=model.sourceMatrices({},0);
            const Vec3 p=toePoint(pose[std::size_t(i)],a->second.end[1]);
            const auto link=toeEditLink(model,parent,child);
            if(link>=model.sourceLinks().size() || !model.repositionReferenceToeJoint(link,p))return false;
        }
    }
    return true;
}
inline std::vector<ComparisonToeAttachment> whiteToeSurfaceAnchors(const SkeletonModel& model,const ToeSurfaceMarks& marks,Vec3 origin) {
    std::vector<ComparisonToeAttachment> result;const auto pose=model.sourceMatrices(origin,0);
    for(int digit=1;digit<=5;++digit)for(char side:{'L','R'}) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;ToeEdit e;e.side=side;e.digit=digit;
        for(int s=1;s<count;++s) {
            const auto parent=toeEditName(e,order[s-1]),child=toeEditName(e,order[s]);
            const auto a=marks.find(parent),b=marks.find(child);
            if(a==marks.end() || b==marks.end() || !(a->second.placed&2) || !(b->second.placed&1))continue;
            const int i=model.sourceStructure(parent),j=model.sourceStructure(child);if(i<0 || j<0)continue;
            result.push_back({std::size_t(i),std::size_t(j),toePoint(pose[std::size_t(i)],a->second.end[1]),
                              toePoint(pose[std::size_t(j)],b->second.end[0]),true});
        }
    }
    return result;
}
inline void blueToeSurfaceAnchors(const SkeletonModel& model,const ToeSurfaceMarks& marks,Vec3 origin,
    const std::vector<std::array<double,16>>& pose,std::vector<ComparisonToeAttachment>& result) {
    for(int digit=1;digit<=5;++digit) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;ToeEdit e;e.digit=digit;e.side=blueToeMarkedSide(marks,digit);
        for(int s=1;s<count;++s) {
            const auto a=marks.find(toeEditName(e,order[s-1])),b=marks.find(toeEditName(e,order[s]));
            if(a==marks.end() || b==marks.end() || !(a->second.placed&2) || !(b->second.placed&1))continue;
            for(int mirror=0;mirror<2;++mirror) {
                ToeEdit side=e;if(mirror)side.side=e.side=='L'?'R':'L';
                const int i=model.sourceStructure(toeEditName(side,order[s-1])),j=model.sourceStructure(toeEditName(side,order[s]));
                if(i<0 || j<0 || std::size_t(i)>=pose.size() || std::size_t(j)>=pose.size())continue;
                Vec3 p=a->second.end[1],q=b->second.end[0];if(mirror){p=toeMirrorPoint(p);q=toeMirrorPoint(q);}
                result.push_back({std::size_t(i),std::size_t(j),toePoint(pose[std::size_t(i)],p+origin),
                                  toePoint(pose[std::size_t(j)],q+origin),true});
            }
        }
    }
}
// Transfer the user's per-bone B-frame transforms to the averaged comparison
// geometry, reflecting the selected foot to the other blue foot. No shaft or
// landmark inference is used for these explicit edits.
inline void applyManualBlueReference(const SkeletonComparison& c,const SkeletonModel& model,
    const ManualToeReference& ref,const std::vector<std::array<double,16>>& before,
    const std::vector<std::array<double,16>>& after,Vec3 origin,std::vector<std::array<double,16>>& pose) {
    if(before.size()!=c.parts().size() || after.size()!=c.parts().size() ||
       std::none_of(ref.edited.begin(),ref.edited.end(),[](bool b){return b;}))return;
    const auto beforePose=c.toePose(before,origin),afterPose=c.toePose(after,origin);
    const auto& comparisonReference=c.correctedToeReference();
    const auto beforeRaw=c.toePose(before,origin,&comparisonReference),afterRaw=c.toePose(after,origin,&comparisonReference);
    for(int digit=1;digit<=5;++digit) {
        if(!hasManualToe(model,ref,digit))continue;
        int count[2]{};
        for(int s=0;s<2;++s)for(int segment=-1;segment<3;++segment) {
            ToeEdit e;e.digit=digit;e.side=s?'R':'L';e.segment=segment;if(!validToeEdit(e))continue;
            const int i=model.sourceStructure(toeEditName(e,segment));
            if(i>=0 && std::size_t(i)<ref.edited.size() && ref.edited[std::size_t(i)])++count[s];
        }
        const char selected=count[1]>count[0]?'R':'L';
        for(int segment=-1;segment<3;++segment) {
            ToeEdit e;e.digit=digit;e.side=selected;e.segment=segment;if(!validToeEdit(e))continue;
            const int source=model.sourceStructure(toeEditName(e,segment));e.side=selected=='L'?'R':'L';
            const int mirror=model.sourceStructure(toeEditName(e,segment));
            if(source<0 || mirror<0 || std::size_t(source)>=pose.size() || std::size_t(mirror)>=pose.size())continue;
            const auto i=std::size_t(source),j=std::size_t(mirror);
            JointRigidTransform delta;
            if(i<ref.edited.size() && ref.edited[i]) {
                if(segment<0) {
                    const auto& part=c.parts()[i];if(part.points.empty())continue;
                    Vec3 center{};for(Vec3 p:part.points)center+=p;center=center/double(part.points.size());
                    delta=toeRigid(registeredToeDelta(after[i],before[i],c.metatarsalFrame(i),center,
                        [&](Vec3 p){return c.mapPart(p,part)+origin;}));
                } else delta=joint_detail::compose(toeRigid(afterRaw[i]),joint_detail::inverse(toeRigid(beforeRaw[i])));
            }
            const auto set=[&](std::size_t index,const JointRigidTransform& change) {
                const auto motion=joint_detail::compose(toeRigid(pose[index]),joint_detail::inverse(toeRigid(afterPose[index])));
                const auto target=joint_detail::compose(change,toeRigid(beforePose[index]));
                pose[index]=toeMatrix(joint_detail::compose(motion,target));
            };
            set(i,delta);set(j,toeMirrorTransform(delta,origin));
        }
    }
}
} // namespace gtc::native
