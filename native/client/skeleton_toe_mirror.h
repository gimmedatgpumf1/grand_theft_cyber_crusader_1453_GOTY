#pragma once

#include "client/skeleton_metatarsal_registration.h"

namespace gtc::native {
// Keep the blue reference toes bilateral without constraining later left/right
// motion. The current right-side motion remains relative to its own corrected
// source pose; only its fixed reference correction is reflected from the left.
template<class Part,class Map>
void mirrorReferenceToePose(const std::vector<Part>& parts,std::vector<std::array<double,16>>& result,
    const std::vector<std::array<double,16>>& original,const std::vector<std::array<double,16>>& corrected,
    const std::vector<JointRigidTransform>& frames,Vec3 origin,Map map) {
    if(corrected.size()!=parts.size())return;
    const auto rigid=[](const std::array<double,16>& m) {
        JointRigidTransform t;
        t.rotation={m[0],m[4],m[8],m[1],m[5],m[9],m[2],m[6],m[10]};
        t.translation={m[12],m[13],m[14]};return t;
    };
    const auto matrix=[](const JointRigidTransform& t) {
        const auto& r=t.rotation;
        return std::array<double,16>{r[0],r[3],r[6],0,r[1],r[4],r[7],0,r[2],r[5],r[8],0,
                                     t.translation.x,t.translation.y,t.translation.z,1};
    };
    const auto reference=[&](std::size_t i) {
        const auto& p=parts[i];Vec3 center{};for(Vec3 v:p.points)center+=v;
        center=center/double(p.points.size());
        return rigid(registeredToeDelta(corrected[i],original[i],frames[i],center,
                                         [&](Vec3 v){return map(v,p)+origin;}));
    };
    for(std::size_t i=0;i<parts.size();++i) {
        const auto& name=parts[i].name;
        if(!name.ends_with("_R") || (name.rfind("FootProxP",0)!=0 && name.rfind("FootMidP",0)!=0 &&
                                      name.rfind("FootDistP",0)!=0) || parts[i].points.empty())continue;
        const std::string leftName=name.substr(0,name.size()-1)+"L";
        auto left=std::find_if(parts.begin(),parts.end(),[&](const Part& p){return p.name==leftName;});
        if(left==parts.end() || left->points.empty())continue;
        JointRigidTransform reflected=reference(std::size_t(left-parts.begin()));
        // Reflection about x=origin.x applied to both input and output points.
        constexpr double sign[3]={-1,1,1};
        for(int a=0;a<3;++a)for(int b=0;b<3;++b)reflected.rotation[a*3+b]*=sign[a]*sign[b];
        const Vec3 mirrorOrigin{2*origin.x,0,0};
        reflected.translation={-reflected.translation.x,reflected.translation.y,reflected.translation.z};
        reflected.translation+=mirrorOrigin-frameVector(reflected,mirrorOrigin);
        const auto motion=joint_detail::compose(rigid(result[i]),joint_detail::inverse(reference(i)));
        result[i]=matrix(joint_detail::compose(motion,reflected));
    }
}
} // namespace gtc::native
