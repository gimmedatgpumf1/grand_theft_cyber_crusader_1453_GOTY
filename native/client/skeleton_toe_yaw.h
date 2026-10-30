#pragma once

#include "client/skeleton_body.h"
#include "client/skeleton_lines.h"

namespace gtc::native {
// Align each source-derived toe shaft with the foot in the ground plane.
// This is a reference-frame correction, separate from plantar flexion and
// never applied to the original source pose.
inline bool alignReferenceToeDirections(SkeletonModel& model, const SkeletonLines& lines) {
    const auto point=[](const std::array<double,16>& m,Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    const auto center=[&](int id,const std::vector<std::array<double,16>>& pose) {
        const auto& part=model.sourceStructures()[id]; Vec3 c;
        for (std::size_t i=0;i<part.pointCount;++i) c+=model.bones()[part.group].points[part.firstPoint+i];
        return point(pose[id],c/static_cast<double>(part.pointCount));
    };
    const auto link=[&](int a,int b) {
        for (std::size_t i=0;i<model.sourceLinks().size();++i) {
            const auto& e=model.sourceLinks()[i];
            if ((e.first==static_cast<std::size_t>(a) && e.second==static_cast<std::size_t>(b)) ||
                (e.second==static_cast<std::size_t>(a) && e.first==static_cast<std::size_t>(b))) return i;
        }
        return model.sourceLinks().size();
    };
    constexpr double degrees=180.0/3.14159265358979323846;
    for (char side : {'L','R'}) {
        const std::string tag=std::string("_")+side;
        const int heel=model.sourceStructure("Cal"+tag),foot=model.sourceStructure("FootMeta3"+tag);
        if (heel<0 || foot<0) return false;
        for (int digit=1;digit<=5;++digit) {
            const auto n=std::to_string(digit)+tag;
            const int meta=model.sourceStructure("FootMeta"+n),prox=model.sourceStructure("FootProxP"+n);
            const int mid=digit==1 ? -1 : model.sourceStructure("FootMidP"+n);
            const int tip=model.sourceStructure("FootDistP"+n);
            if (meta<0 || prox<0 || tip<0 || (digit>1 && mid<0)) return false;
            struct Stage { int parent,child,from,to; };
            const Stage small[]={{prox,mid,prox,mid},{mid,tip,mid,tip},{meta,prox,prox,mid}};
            const Stage great[]={{meta,prox,-1,-1},{prox,tip,-1,-1},{meta,prox,prox,tip}};
            const Stage* stages=digit==1 ? great : small;
            for (int sweep=0;sweep<2;++sweep) for (int k=0;k<3;++k) {
                const auto& stage=stages[k];
                if (stage.parent<0 || stage.child<0) continue;
                const auto index=link(stage.parent,stage.child);
                const auto sourceAxis=lines.sourceBoneDirection(model.sourceStructures()[stage.child].name);
                if (index==model.sourceLinks().size() || !model.referenceToeYaw(index) || !sourceAxis) return false;
                const auto deviation=[&]() {
                    const auto pose=model.sourceMatrices({},0); const auto& m=pose[stage.child];
                    const Vec3 direction=stage.from<0 ? point(m,*sourceAxis)-point(m,{}) :
                        center(stage.to,pose)-center(stage.from,pose);
                    const Vec3 forward=center(foot,pose)-center(heel,pose);
                    // Tiny lesser bones are nearly round; use the line between
                    // adjacent centres instead of their unstable principal axes.
                    const double period=stage.from<0 ? 3.14159265358979323846 : 2*3.14159265358979323846;
                    return std::remainder(std::atan2(direction.x,direction.z)-std::atan2(forward.x,forward.z),period)*degrees;
                };
                // Estimate the direction change from a small measured turn;
                // both values refer to the actual articulated bone shaft.
                for (int pass=0;pass<2;++pass) {
                    const double current=*model.referenceToeYaw(index), before=deviation();
                    if (!model.setReferenceToeYaw(index,current+1)) return false;
                    const double slope=std::remainder(deviation()-before,360.0);
                    const double target=std::abs(slope)>1e-5 ? current-before/slope : current;
                    if (!model.setReferenceToeYaw(index,target)) return false;
                }
            }
        }
    }
    return true;
}
} // namespace gtc::native
