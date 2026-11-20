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
    const bool correctedPose=corrected!=original;
    double longitudinal=0;
    if(correctedPose)for(const auto& p:parts)if(p.name=="FootMeta5_L" && !p.points.empty()) {
        double low=1e30,high=-1e30;
        for(Vec3 v:p.points) {const double z=map(v,p).z;low=std::min(low,z);high=std::max(high,z);}
        longitudinal=.0012*(high-low)/.06528;break;
    }
    const auto rawReference=[&](std::size_t i) {
        const auto& p=parts[i];Vec3 center{};for(Vec3 v:p.points)center+=v;
        center=center/double(p.points.size());
        return rigid(registeredToeDelta(corrected[i],original[i],frames[i],center,
                                         [&](Vec3 v){return map(v,p)+origin;}));
    };
    const auto reference=[&](std::size_t i) {
        auto r=rawReference(i);const auto& name=parts[i].name;
        if(name.starts_with("FootMidP5_") || name.starts_with("FootDistP5_"))r.translation.z+=longitudinal;
        return r;
    };
    // The averaged blue bones are larger at this articulation than the CT
    // surfaces. Register the two child pieces together in their reference
    // frame, leaving the source joints and later articulation unchanged.
    if(correctedPose)for(std::size_t i=0;i<parts.size();++i) {
        const auto& n=parts[i].name;
        if((!n.starts_with("FootMidP5_") && !n.starts_with("FootDistP5_")) || parts[i].points.empty())continue;
        const auto motion=joint_detail::compose(rigid(result[i]),joint_detail::inverse(rawReference(i)));
        result[i]=matrix(joint_detail::compose(motion,reference(i)));
    }
    for(std::size_t i=0;i<parts.size();++i) {
        const auto& name=parts[i].name;
        if(name.find("P5_R")==std::string::npos || (name.rfind("FootProxP",0)!=0 && name.rfind("FootMidP",0)!=0 &&
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
