#pragma once
#include "client/skeleton_body.h"
#include "client/skeleton_lines.h"
#include <array>
#include <string>

namespace gtc::native {
// Individually chosen reference corrections. Rotations act on the unmodified
// scanned surfaces; the axis/angle is not inferred from the surface geometry.
struct ManualToeAdjustment {const char* parent;const char* child;double yaw;Vec3 shift;double pitch=0;int parentEnd=1;};
inline bool applyManualToeAdjustment(SkeletonModel& model,const SkeletonLines& lines,const ManualToeAdjustment& a) {
        const int parent=model.sourceStructure(a.parent),child=model.sourceStructure(a.child);
        if(parent<0 || child<0)return false;
        std::size_t edge=0;for(;edge<model.sourceLinks().size();++edge) {
            const auto& e=model.sourceLinks()[edge];
            if((e.first==std::size_t(parent) && e.second==std::size_t(child)) ||
               (e.second==std::size_t(parent) && e.first==std::size_t(child)))break;
        }
        if(edge==model.sourceLinks().size())return false;
        const auto pose=model.sourceMatrices({},0);const auto& part=model.sourceStructures()[std::size_t(child)];
        Vec3 center{};for(std::size_t i=0;i<part.pointCount;++i)center+=model.bones()[part.group].points[part.firstPoint+i];
        center=center/double(part.pointCount);const auto& m=pose[std::size_t(child)];
        center={m[0]*center.x+m[4]*center.y+m[8]*center.z+m[12],m[1]*center.x+m[5]*center.y+m[9]*center.z+m[13],m[2]*center.x+m[6]*center.y+m[10]*center.z+m[14]};
        auto yaw=joint_detail::rotate({0,1,0},a.yaw*3.14159265358979323846/180);
        auto pitch=joint_detail::rotate({1,0,0},a.pitch*3.14159265358979323846/180);
        auto turn=joint_detail::compose(pitch,yaw);
        turn.translation=center-frameVector(turn,center)+a.shift;
        if(!model.adjustReferenceToe(edge,turn))return false;
        // Each endpoint below was selected on the corresponding B-view bone,
        // not chosen by a nearest-point search. Keep the hinge on the parent.
        const auto end=lines.sourceBoneEndpoints(a.parent);
        if(!end)return false;
        const auto updated=model.sourceMatrices({},0);const auto& pm=updated[std::size_t(parent)];
        const Vec3 q=(*end)[std::size_t(a.parentEnd)];
        const Vec3 pivot{pm[0]*q.x+pm[4]*q.y+pm[8]*q.z+pm[12],pm[1]*q.x+pm[5]*q.y+pm[9]*q.z+pm[13],pm[2]*q.x+pm[6]*q.y+pm[10]*q.z+pm[14]};
        return model.repositionReferenceToeJoint(edge,pivot);
}
inline bool fitManualFemaleLittleToe(SkeletonModel& model,const SkeletonLines& lines) {
    constexpr ManualToeAdjustment adjustments[]={
        {"FootProxP5_L","FootMidP5_L",-40,{.0005,0,-.0005},-35},
        {"FootMidP5_L","FootDistP5_L",-25,{.0025,0,.002},0},
        {"FootProxP5_R","FootMidP5_R",65,{-.0025,0,.0015},-25},
        {"FootMidP5_R","FootDistP5_R",-35,{-.0045,.0025,.0005},-30}
    };
    for(const auto& a:adjustments)if(!applyManualToeAdjustment(model,lines,a))return false;
    return true;
}
inline bool fitReferenceToeShafts(SkeletonModel& model,const SkeletonLines& lines,bool female=false) {
    using Adjustment=ManualToeAdjustment;
    constexpr Adjustment femaleAdjustments[]={
        {"FootProxP2_L","FootMidP2_L",-20,{}},
        {"FootMidP2_L","FootDistP2_L",20,{.002,0,0}},
        {"FootProxP2_R","FootMidP2_R",20,{}},
        {"FootMidP2_R","FootDistP2_R",-40,{-.002,0,.001}},
        {"FootProxP3_L","FootMidP3_L",-65,{}},
        {"FootMidP3_L","FootDistP3_L",65,{.009,0,.004}},
        {"FootProxP3_R","FootMidP3_R",60,{0,0,-.001}},
        {"FootMidP3_R","FootDistP3_R",-60,{-.007,0,.00325}},
        {"FootProxP4_L","FootMidP4_L",-20,{}},
        {"FootMidP4_L","FootDistP4_L",20,{.002,0,0}},
        {"FootProxP4_R","FootMidP4_R",0,{0,0,-.0015},20},
        {"FootMidP4_R","FootDistP4_R",0,{},-20},
    };
    constexpr Adjustment maleAdjustments[]={
        {"FootProxP1_R","FootDistP1_R",0,{-.0015,.0015,-.0033}},
        {"FootProxP5_L","FootMidP5_L",-20,{0,0,-.0007}},
        {"FootMidP5_L","FootDistP5_L",20,{.002,0,0}},
        {"FootMidP2_L","FootDistP2_L",0,{0,0,-.0025}},
        {"FootProxP2_R","FootMidP2_R",20,{}},
        {"FootMidP2_R","FootDistP2_R",-40,{-.002,0,-.0025}},
        {"FootProxP4_L","FootMidP4_L",-60,{0,0,-.001}},
        {"FootMidP4_L","FootDistP4_L",60,{.007,0,.004},0,1},
        {"FootProxP3_R","FootMidP3_R",35,{0,0,-.0015}},
        {"FootMidP3_R","FootDistP3_R",-35,{-.004,0,.00125}},
        {"FootProxP4_R","FootMidP4_R",35,{0,0,-.0015},-15},
        {"FootMidP4_R","FootDistP4_R",-35,{-.004,0,.00125},15}
    };
    if(female){for(const auto& a:femaleAdjustments)if(!applyManualToeAdjustment(model,lines,a))return false;}
    else {for(const auto& a:maleAdjustments)if(!applyManualToeAdjustment(model,lines,a))return false;}
    return true;
}
} // namespace gtc::native
