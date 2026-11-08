#pragma once
#include "client/skeleton_toe_blue_calibration.h"
#include "client/skeleton_toe_limits.h"
#include "client/skeleton_leg_frame.h"
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
// Transport each individual B-source bone orientation into its standing
// metatarsal frame. This preserves the marked directions and relative splay,
// rather than using the old estimated shaft or a common guessed toe direction.
inline bool applyMarkedToeReference(SkeletonModel& model,const ToeSurfaceMarks& marks) {
    for(char side:{'L','R'})for(int digit=1;digit<=5;++digit) {
        ToeEdit e;e.side=side;e.digit=digit;
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;
        const int meta=model.sourceStructure(toeEditName(e,-1));if(meta<0)return false;
        for(int s=1;s<count;++s) {
            const auto parent=toeEditName(e,order[s-1]),child=toeEditName(e,order[s]);
            const auto a=marks.find(parent),b=marks.find(child);
            if(a==marks.end() || b==marks.end() || a->second.placed!=3 || b->second.placed!=3)continue;
            const int i=model.sourceStructure(parent),j=model.sourceStructure(child);
            const auto link=toeEditLink(model,parent,child);
            if(i<0 || j<0 || link>=model.sourceLinks().size())return false;
            const auto pose=model.sourceMatrices({},0);
            auto target=toeRigid(pose[std::size_t(meta)]),current=toeRigid(pose[std::size_t(j)]);
            target.translation={};current.translation={};
            auto correction=joint_detail::compose(target,joint_detail::inverse(current));
            const Vec3 start=toePoint(pose[std::size_t(j)],b->second.end[0]);
            const Vec3 contact=toePoint(pose[std::size_t(i)],a->second.end[1]);
            correction.translation=contact-frameVector(correction,start);
            if(!model.adjustReferenceToe(link,correction))return false;
        }
    }
    return true;
}
// Place the kinematic pivot on the marked parent surface. Ragdoll anchors
// retain both independently marked surface points.
inline bool setMarkedToeSurfacePivots(SkeletonModel& model,const ToeSurfaceMarks& marks) {
    const auto left=sourceLegFrame(model,0),right=sourceLegFrame(model,1);
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
            if(a->second.placed==3 && b->second.placed==3) {
                const auto& leg=side=='L'?left:right;
                const int meta=model.sourceStructure("FootMeta"+std::to_string(digit)+"_"+side);
                if(!leg || meta<0)continue;
                const Vec3 parent=p-toePoint(pose[std::size_t(i)],a->second.end[0]);
                const Vec3 child=toePoint(pose[std::size_t(j)],b->second.end[1])-toePoint(pose[std::size_t(j)],b->second.end[0]);
                const Vec3 dorsal=frameVector(toeRigid(pose[std::size_t(meta)]),leg->soleNormal);
                const auto bend=markedToeBend(parent,child,dorsal);
                if(bend && !model.calibrateReferenceToeBend(link,bend->axis,bend->flexion))return false;
            }
        }
    }
    return true;
}
inline std::vector<ComparisonToeAttachment> whiteToeSurfaceAnchors(const SkeletonModel& model,const ToeSurfaceMarks& marks,Vec3 origin) {
    std::vector<ComparisonToeAttachment> result;const auto pose=model.sourceMatrices(origin,0);
    const auto left=sourceLegFrame(model,0),right=sourceLegFrame(model,1);
    for(int digit=1;digit<=5;++digit)for(char side:{'L','R'}) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;ToeEdit e;e.side=side;e.digit=digit;
        for(int s=1;s<count;++s) {
            const auto parent=toeEditName(e,order[s-1]),child=toeEditName(e,order[s]);
            const auto a=marks.find(parent),b=marks.find(child);
            if(a==marks.end() || b==marks.end() || !(a->second.placed&2) || !(b->second.placed&1))continue;
            const int i=model.sourceStructure(parent),j=model.sourceStructure(child);if(i<0 || j<0)continue;
            const Vec3 start=toePoint(pose[std::size_t(i)],a->second.end[0]);
            const Vec3 end=toePoint(pose[std::size_t(i)],a->second.end[1]);
            const Vec3 childStart=toePoint(pose[std::size_t(j)],b->second.end[0]);
            const auto& leg=side=='L'?left:right;
            const int meta=model.sourceStructure("FootMeta"+std::to_string(digit)+"_"+side);
            const Vec3 dorsal=leg && meta>=0 ? frameVector(toeRigid(pose[std::size_t(meta)]),leg->soleNormal) : Vec3{};
            result.push_back({std::size_t(i),std::size_t(j),end,childStart,true,
                              a->second.placed==3?end-start:Vec3{},
                              b->second.placed==3?toePoint(pose[std::size_t(j)],b->second.end[1])-childStart:Vec3{},dorsal});
        }
    }
    return result;
}
inline void blueToeSurfaceAnchors(const SkeletonComparison& comparison,const SkeletonModel& model,
    const ToeSurfaceMarks& marks,Vec3 origin,const std::vector<std::array<double,16>>& pose,
    std::vector<ComparisonToeAttachment>& result) {
    const auto left=sourceLegFrame(model,0),right=sourceLegFrame(model,1);
    for(int digit=1;digit<=5;++digit) {
        // Map both source sole normals through the same registration and
        // profile scaling as the marked metatarsals, then mirror their mean.
        Vec3 blueDorsal{};int normals=0;
        for(int side=0;side<2;++side) {
            const auto& leg=side?right:left;
            const int index=model.sourceStructure("FootMeta"+std::to_string(digit)+(side?"_R":"_L"));
            if(!leg || index<0 || std::size_t(index)>=comparison.parts().size() ||
               std::size_t(index)>=comparison.littleToeSkinReference().size())continue;
            const auto i=std::size_t(index);const auto& part=comparison.parts()[i];
            if(part.points.empty())continue;
            const Vec3 direction=frameVector(comparison.metatarsalFrame(i),
                frameVector(toeRigid(comparison.littleToeSkinReference()[i]),leg->soleNormal));
            const Vec3 point=part.points.front();
            Vec3 mapped=comparison.mapPart(point+direction*.01,part)-comparison.mapPart(point,part);
            if(length(mapped)<1e-8)continue;
            mapped=normalized(mapped);if(side)mapped=toeMirrorPoint(mapped);
            blueDorsal+=mapped;++normals;
        }
        if(normals && length(blueDorsal)>1e-8)blueDorsal=normalized(blueDorsal);
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;ToeEdit e;e.digit=digit;e.side=blueToeMarkedSide(marks,digit);
        for(int s=1;s<count;++s) {
            const auto a=marks.find(toeEditName(e,order[s-1])),b=marks.find(toeEditName(e,order[s]));
            if(a==marks.end() || b==marks.end() || !(a->second.placed&2) || !(b->second.placed&1))continue;
            for(int mirror=0;mirror<2;++mirror) {
                ToeEdit side=e;if(mirror)side.side=e.side=='L'?'R':'L';
                const int i=model.sourceStructure(toeEditName(side,order[s-1])),j=model.sourceStructure(toeEditName(side,order[s]));
                if(i<0 || j<0 || std::size_t(i)>=pose.size() || std::size_t(j)>=pose.size())continue;
                Vec3 start=a->second.end[0],p=a->second.end[1],q=b->second.end[0],tip=b->second.end[1];
                if(mirror){start=toeMirrorPoint(start);p=toeMirrorPoint(p);q=toeMirrorPoint(q);tip=toeMirrorPoint(tip);}
                const Vec3 parentPoint=toePoint(pose[std::size_t(i)],p+origin);
                const Vec3 childPoint=toePoint(pose[std::size_t(j)],q+origin);
                const int meta=model.sourceStructure("FootMeta"+std::to_string(digit)+"_"+side.side);
                const Vec3 localDorsal=side.side=='R'?toeMirrorPoint(blueDorsal):blueDorsal;
                const Vec3 dorsal=meta>=0 && std::size_t(meta)<pose.size() ? frameVector(toeRigid(pose[std::size_t(meta)]),localDorsal) : Vec3{};
                result.push_back({std::size_t(i),std::size_t(j),parentPoint,childPoint,true,
                                  a->second.placed==3?parentPoint-toePoint(pose[std::size_t(i)],start+origin):Vec3{},
                                  b->second.placed==3?toePoint(pose[std::size_t(j)],tip+origin)-childPoint:Vec3{},dorsal});
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
