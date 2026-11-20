#pragma once

#include "client/skeleton_toe_manual_reference.h"
#include "client/skeleton_rom.h"
#include <array>
#include <cmath>

namespace gtc::native {
// Extract the twist around an axis from a relative rigid rotation. A pure
// off-axis swing or translation cannot become a separate toe movement.
inline double blueToeHingeTwist(const JointRigidTransform& relative,Vec3 axis) {
    const auto& m=relative.rotation;
    double w=0,x=0,y=0,z=0;
    const double trace=m[0]+m[4]+m[8];
    if(trace>0) {
        const double s=std::sqrt(trace+1.0)*2;
        w=.25*s;x=(m[7]-m[5])/s;y=(m[2]-m[6])/s;z=(m[3]-m[1])/s;
    } else if(m[0]>m[4] && m[0]>m[8]) {
        const double s=std::sqrt(std::max(0.0,1.0+m[0]-m[4]-m[8]))*2;
        if(s<1e-12)return 0;
        w=(m[7]-m[5])/s;x=.25*s;y=(m[1]+m[3])/s;z=(m[2]+m[6])/s;
    } else if(m[4]>m[8]) {
        const double s=std::sqrt(std::max(0.0,1.0+m[4]-m[0]-m[8]))*2;
        if(s<1e-12)return 0;
        w=(m[2]-m[6])/s;x=(m[1]+m[3])/s;y=.25*s;z=(m[5]+m[7])/s;
    } else {
        const double s=std::sqrt(std::max(0.0,1.0+m[8]-m[0]-m[4]))*2;
        if(s<1e-12)return 0;
        w=(m[3]-m[1])/s;x=(m[2]+m[6])/s;y=(m[5]+m[7])/s;z=.25*s;
    }
    const double projection=x*axis.x+y*axis.y+z*axis.z;
    if(std::abs(w)+std::abs(projection)<1e-12)return 0;
    const double angle=2*std::atan2(projection,w);
    return std::atan2(std::sin(angle),std::cos(angle));
}

// Reconstruct each blue child from its standing frame and ONE marked hinge
// coordinate, parent before child. Retargeted white matrices are inputs only:
// off-axis rotations and translation from them cannot pass into the child.
// The hinge normal is still an approximation: shaft marks plus foot dorsal,
// not a measured patient-specific joint axis.
inline void constrainBlueToeMotion(const SkeletonModel& model,
    const std::vector<std::array<double,16>>& standing,
    const std::vector<ComparisonToeAttachment>& reference,std::vector<std::array<double,2>>& recovery,
    std::vector<std::array<double,16>>& pose) {
    if(standing.size()!=pose.size() || recovery.size()!=reference.size())return;
    constexpr double rad=3.14159265358979323846/180.0;
    const auto input=pose;
    const auto find=[&](std::size_t parent,std::size_t child)->int {
        for(std::size_t i=0;i<reference.size();++i)
            if(reference[i].parent==parent && reference[i].child==child)return int(i);
        return -1;
    };
    for(int digit=1;digit<=5;++digit) {
        const std::array<int,4> order=digit==1?std::array<int,4>{-1,0,2,3}:std::array<int,4>{-1,0,1,2};
        const int count=digit==1?3:4;
        for(int segment=1;segment<count;++segment) {
            int refs[2]={-1,-1},parents[2]={-1,-1},children[2]={-1,-1};
            std::optional<MarkedToeBend> bends[2];
            double movement[2]{};
            for(int side=0;side<2;++side) {
                ToeEdit e;e.digit=digit;e.side=side?'R':'L';
                const int p=model.sourceStructure(toeEditName(e,order[segment-1]));
                const int c=model.sourceStructure(toeEditName(e,order[segment]));
                if(p<0 || c<0 || std::size_t(p)>=pose.size() || std::size_t(c)>=pose.size())continue;
                parents[side]=p;children[side]=c;
                refs[side]=find(std::size_t(p),std::size_t(c));
                if(refs[side]<0)continue;
                const auto& j=reference[std::size_t(refs[side])];
                bends[side]=markedToeBend(j.parentShaft,j.childShaft,j.dorsal);
                if(!bends[side])continue;
                const auto rawParent=joint_detail::compose(toeRigid(input[j.parent]),joint_detail::inverse(toeRigid(standing[j.parent])));
                const auto rawChild=joint_detail::compose(toeRigid(input[j.child]),joint_detail::inverse(toeRigid(standing[j.child])));
                const auto relative=joint_detail::compose(joint_detail::inverse(rawParent),rawChild);
                movement[side]=blueToeHingeTwist(relative,bends[side]->axis);
            }
            // An unmarked or degenerate joint has no defined hinge. Freeze it
            // in its standing frame rather than passing unconstrained motion.
            const auto freeze=[&] {
                for(int side=0;side<2;++side)if(parents[side]>=0 && children[side]>=0) {
                    const auto p=std::size_t(parents[side]),c=std::size_t(children[side]);
                    const auto parentMotion=joint_detail::compose(toeRigid(pose[p]),joint_detail::inverse(toeRigid(standing[p])));
                    pose[c]=toeMatrix(joint_detail::compose(parentMotion,toeRigid(standing[c])));
                }
            };
            if(refs[0]<0 || refs[1]<0 || !bends[0] || !bends[1]) {freeze();continue;}
            const auto& first=reference[std::size_t(refs[0])];
            const auto range=referenceBendRange(segment==1?(digit==1?"great_toe_mtp":"lesser_toe_mtp"):"toe_ip_hinge",
                model.sourceStructures()[first.parent].name,model.sourceStructures()[first.child].name);
            if(!range) {freeze();continue;}
            const double shared=(movement[0]+movement[1])*.5;
            for(int side=0;side<2;++side) {
                const auto& j=reference[std::size_t(refs[side])];
                const double start=bends[side]->flexion;
                auto& envelope=recovery[std::size_t(refs[side])];
                const double lo=std::max(std::min(range->extension*rad,start),envelope[0]);
                const double hi=std::min(std::max(range->flexion*rad,start),envelope[1]);
                const double target=std::clamp(start+shared,lo,hi);
                envelope[0]=std::max(lo,std::min(range->extension*rad,target));
                envelope[1]=std::min(hi,std::max(range->flexion*rad,target));
                const auto parentMotion=joint_detail::compose(toeRigid(pose[j.parent]),joint_detail::inverse(toeRigid(standing[j.parent])));
                auto hinge=joint_detail::rotate(bends[side]->axis,target-start);
                hinge.translation=j.pivot-frameVector(hinge,j.pivot);
                pose[j.child]=toeMatrix(joint_detail::compose(joint_detail::compose(parentMotion,hinge),toeRigid(standing[j.child])));
            }
        }
    }
}
} // namespace gtc::native
