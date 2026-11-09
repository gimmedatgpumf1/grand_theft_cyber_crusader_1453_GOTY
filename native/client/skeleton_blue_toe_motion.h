#pragma once

#include "client/skeleton_toe_manual_reference.h"
#include "client/skeleton_rom.h"
#include <array>
#include <cmath>

namespace gtc::native {
// Retargeted blue toes have their own shaft angles: copying white rotations
// cannot enforce blue anatomical limits. Correct only movement relative to
// the preserved marked reference, carrying each joint's distal chain with it.
inline void constrainBlueToeMotion(const SkeletonModel& model,
    const std::vector<ComparisonToeAttachment>& reference,std::vector<std::array<double,2>>& recovery,
    std::vector<ComparisonToeAttachment>& current,std::vector<std::array<double,16>>& pose) {
    constexpr double rad=3.14159265358979323846/180.0;
    const auto find=[](const auto& list,std::size_t parent,std::size_t child)->int {
        for(std::size_t i=0;i<list.size();++i)
            if(list[i].parent==parent && list[i].child==child)return int(i);
        return -1;
    };
    const auto angle=[](const ComparisonToeAttachment& j) {
        return markedToeBend(j.parentShaft,j.childShaft,j.dorsal);
    };
    for(int digit=1;digit<=5;++digit) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;
        for(int segment=1;segment<count;++segment) {
            int joints[2]={-1,-1},refs[2]={-1,-1};
            std::array<std::array<std::size_t,3>,2> descendants{};
            int descendantCount[2]={0,0};
            for(int side=0;side<2;++side) {
                ToeEdit e;e.digit=digit;e.side=side?'R':'L';
                const int p=model.sourceStructure(toeEditName(e,order[segment-1]));
                const int c=model.sourceStructure(toeEditName(e,order[segment]));
                if(p<0 || c<0)continue;
                joints[side]=find(current,std::size_t(p),std::size_t(c));
                refs[side]=find(reference,std::size_t(p),std::size_t(c));
                for(int s=segment;s<count;++s) {
                    const int n=model.sourceStructure(toeEditName(e,order[s]));
                    if(n>=0 && std::size_t(n)<pose.size())descendants[side][descendantCount[side]++]=std::size_t(n);
                }
            }
            if(joints[0]<0 || joints[1]<0 || refs[0]<0 || refs[1]<0 || recovery.size()!=reference.size())continue;
            const auto a=angle(current[std::size_t(joints[0])]),b=angle(current[std::size_t(joints[1])]);
            const auto a0=angle(reference[std::size_t(refs[0])]),b0=angle(reference[std::size_t(refs[1])]);
            if(!a || !b || !a0 || !b0)continue;
            const auto& parentName=model.sourceStructures()[current[std::size_t(joints[0])].parent].name;
            const auto& childName=model.sourceStructures()[current[std::size_t(joints[0])].child].name;
            const auto range=referenceBendRange(segment==1?(digit==1?"great_toe_mtp":"lesser_toe_mtp"):"toe_ip_hinge",parentName,childName);
            if(!range)continue;
            // Use a shared motion, not one side's differently posed CT shaft,
            // so the averaged blue pair remains bilaterally matched.
            const auto delta=[](double now,double start) {return std::atan2(std::sin(now-start),std::cos(now-start));};
            const double movement=(delta(a->flexion,a0->flexion)+delta(b->flexion,b0->flexion))*.5;
            const double starts[2]={a0->flexion,b0->flexion},angles[2]={a->flexion,b->flexion};
            for(int side=0;side<2;++side) {
                auto& envelope=recovery[std::size_t(refs[side])];
                const double lo=std::max(std::min(range->extension*rad,starts[side]),envelope[0]);
                const double hi=std::min(std::max(range->flexion*rad,starts[side]),envelope[1]);
                const double target=std::clamp(starts[side]+movement,lo,hi);
                // Once an out-of-range reference recovers, it must not move
                // back out again. Never snap the initial marked pose.
                envelope[0]=std::max(lo,std::min(range->extension*rad,target));
                envelope[1]=std::min(hi,std::max(range->flexion*rad,target));
                const double correction=delta(target,angles[side]);
                if(std::abs(correction)<1e-9)continue;
                auto& joint=current[std::size_t(joints[side])];
                const auto bend=angle(joint);if(!bend)continue;
                auto turn=joint_detail::rotate(bend->axis,correction);
                turn.translation=joint.pivot-frameVector(turn,joint.pivot);
                const auto contains=[&](std::size_t i) {
                    for(int n=0;n<descendantCount[side];++n)if(descendants[side][n]==i)return true;
                    return false;
                };
                for(int n=0;n<descendantCount[side];++n) {
                    const auto i=descendants[side][n];
                    pose[i]=toeMatrix(joint_detail::compose(turn,toeRigid(pose[i])));
                }
                for(auto& j:current) {
                    if(contains(j.parent)) {
                        j.pivot=frameVector(turn,j.pivot)+turn.translation;
                        j.parentShaft=frameVector(turn,j.parentShaft);
                    }
                    if(contains(j.child)) {
                        j.childPivot=frameVector(turn,j.childPivot)+turn.translation;
                        j.childShaft=frameVector(turn,j.childShaft);
                    }
                }
            }
        }
    }
}
} // namespace gtc::native
