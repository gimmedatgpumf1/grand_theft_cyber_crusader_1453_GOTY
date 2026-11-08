#pragma once

#include "client/skeleton_toe_mirror.h"
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

namespace gtc::native {
// The comparison bones are independently averaged and scaled, so transferring
// each source bone around its own centre does not preserve a shared hinge.
// Cache a contact on the *posed comparison* and make both bodies carry that
// same point as they move. This does not change the standing bone positions.
struct ComparisonToeAttachment {
    std::size_t parent=0, child=0;
    Vec3 pivot{};
    Vec3 childPivot{};
    bool separateSurfacePoints=false;
    Vec3 parentShaft{}; // posed direction between the user's two parent marks
    Vec3 childShaft{};  // posed direction between the child's two marks
    Vec3 dorsal{};      // posed foot dorsal direction for sagittal flexion
};
inline JointRigidTransform toeRigid(const std::array<double,16>& m) {
    JointRigidTransform t;
    t.rotation={m[0],m[4],m[8],m[1],m[5],m[9],m[2],m[6],m[10]};
    t.translation={m[12],m[13],m[14]}; return t;
}
inline Vec3 toePoint(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
            m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
template<class Part,class Map>
std::vector<ComparisonToeAttachment> comparisonToeAttachments(const std::vector<Part>& parts,
    const std::vector<std::array<double,16>>& neutral,Map map) {
    std::vector<ComparisonToeAttachment> joints;
    const auto find=[&](const std::string& name) {
        for(std::size_t i=0;i<parts.size();++i)if(parts[i].name==name)return i;
        return parts.size();
    };
    for(const char side:{'L','R'})for(int digit=1;digit<=5;++digit) {
        const std::string suffix=std::to_string(digit)+"_"+side;
        const std::array<std::string,4> names={"FootMeta"+suffix,"FootProxP"+suffix,
                                               "FootMidP"+suffix,"FootDistP"+suffix};
        const std::array<int,4> order=digit==1?std::array<int,4>{0,1,3,-1}:std::array<int,4>{0,1,2,3};
        for(int link=0;link<3 && order[link+1]>=0;++link) {
            const auto a=find(names[order[link]]),b=find(names[order[link+1]]);
            if(a==parts.size() || b==parts.size() || parts[a].points.empty() || parts[b].points.empty())continue;
            Vec3 pivot{}; double best=std::numeric_limits<double>::max();
            const auto& pa=parts[a];const auto& pb=parts[b];
            const auto as=std::max<std::size_t>(1,pa.points.size()/192),bs=std::max<std::size_t>(1,pb.points.size()/192);
            for(std::size_t i=0;i<pa.points.size();i+=as) {
                const Vec3 p=toePoint(neutral[a],map(pa.points[i],pa));
                for(std::size_t j=0;j<pb.points.size();j+=bs) {
                    const Vec3 q=toePoint(neutral[b],map(pb.points[j],pb));
                    const Vec3 d=p-q;const double distance=dot(d,d);
                    if(distance<best){best=distance;pivot=(p+q)*.5;}
                }
            }
            joints.push_back({a,b,pivot});
        }
    }
    return joints;
}
template<class Part,class Map>
std::vector<std::array<double,16>> comparisonToeNeutral(const std::vector<Part>& parts,
    const std::vector<std::array<double,16>>& original,const std::vector<std::array<double,16>>& rest,
    const std::vector<std::array<double,16>>& corrected,const std::vector<JointRigidTransform>& frames,Vec3 origin,Map map) {
    const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::vector<std::array<double,16>> result(parts.size(),identity);
    for(std::size_t i=0;i<parts.size();++i) {
        const auto& p=parts[i];
        if(p.points.empty() || (p.name.rfind("FootProxP",0)!=0 && p.name.rfind("FootMidP",0)!=0 &&
                               p.name.rfind("FootDistP",0)!=0))continue;
        Vec3 center{};for(Vec3 q:p.points)center+=q;center=center/double(p.points.size());
        result[i]=registeredToeDelta(rest[i],original[i],frames[i],center,[&](Vec3 q){return map(q,p)+origin;});
    }
    mirrorReferenceToePose(parts,result,original,corrected,frames,origin,map);
    return result;
}
inline std::vector<std::array<double,16>> localToeMatrices(std::vector<std::array<double,16>> matrices,Vec3 origin) {
    for(auto& m:matrices) {
        const Vec3 rotated{m[0]*origin.x+m[4]*origin.y+m[8]*origin.z,
                           m[1]*origin.x+m[5]*origin.y+m[9]*origin.z,
                           m[2]*origin.x+m[6]*origin.y+m[10]*origin.z};
        m[12]+=rotated.x-origin.x;m[13]+=rotated.y-origin.y;m[14]+=rotated.z-origin.z;
    }
    return matrices;
}
inline std::vector<ComparisonToeAttachment> posedToeAttachments(std::vector<ComparisonToeAttachment> joints,
    const std::vector<std::array<double,16>>& neutral,const std::vector<std::array<double,16>>& posed,Vec3 origin) {
    for(auto& joint:joints) {
        const auto motion=joint_detail::compose(toeRigid(posed[joint.parent]),joint_detail::inverse(toeRigid(neutral[joint.parent])));
        joint.pivot=frameVector(motion,joint.pivot+origin)+motion.translation;
    }
    return joints;
}
// Adjust individually inspected comparison joints independently of the CT
// registration. The averaged phalanges have different contours from the CT;
// rotations and translations below were chosen from resting close-ups.
template<class Part,class Map>
void fitComparisonToePlacement(const std::vector<Part>& parts,std::vector<std::array<double,16>>& pose,Map map,bool male,Vec3 origin,double amount=1) {
    double scale=1;
    for(const auto& p:parts)if(p.name==(male?"FootMeta2_L":"FootMeta5_L") && !p.points.empty()) {
        double lo=1e30,hi=-1e30;
        for(Vec3 v:p.points){const double z=map(v,p).z;lo=std::min(lo,z);hi=std::max(hi,z);}
        scale=(hi-lo)/.06528;break;
    }
    for(std::size_t i=0;i<parts.size();++i) {
        const auto& n=parts[i].name;
        if(male && n.starts_with("FootDistP2_"))pose[i][14]-=.0025*scale*amount;
        else if(male && (n.starts_with("FootMidP5_") || n.starts_with("FootDistP5_")))pose[i][14]-=.0012*scale*amount;
        else if(n.starts_with("FootMidP4_"))pose[i][14]-=(male?.0015:.0010)*scale*amount;
        else if(n.starts_with("FootDistP4_"))pose[i][14]-=(male?.0020:.0015)*scale*amount;
        else if(!male && n.starts_with("FootMidP3_")) {pose[i][14]-=.0015*scale*amount;pose[i][13]+=.0006*scale*amount;}
        else if(!male && n.starts_with("FootDistP3_")) {pose[i][14]-=.0025*scale*amount;pose[i][13]+=.0008*scale*amount;}
        else if(!male && n.starts_with("FootMidP5_"))pose[i][14]-=.0012*scale*amount;
        else if(!male && n.starts_with("FootDistP5_"))pose[i][14]-=.0022*scale*amount;
        if(!male && !parts[i].points.empty() && (n.starts_with("FootMidP5_") || n.starts_with("FootDistP5_"))) {
            Vec3 center{};for(Vec3 q:parts[i].points)center+=map(q,parts[i])+origin;
            center=toePoint(pose[i],center/double(parts[i].points.size()));
            const double side=n.ends_with("_L")?1:-1;
            const double angle=(n.starts_with("FootMidP5_")?-20:20)*side*amount*3.14159265358979323846/180;
            auto turn=joint_detail::rotate({0,1,0},angle);
            turn.translation=center-frameVector(turn,center);
            const auto t=joint_detail::compose(turn,toeRigid(pose[i]));const auto& r=t.rotation;
            pose[i]={r[0],r[3],r[6],0,r[1],r[4],r[7],0,r[2],r[5],r[8],0,t.translation.x,t.translation.y,t.translation.z,1};
        }
    }
}
inline std::vector<std::array<double,16>> rebaseComparisonToePose(
    const std::vector<std::array<double,16>>& posed,const std::vector<std::array<double,16>>& changed,
    const std::vector<std::array<double,16>>& standing) {
    auto result=posed;
    for(std::size_t i=0;i<result.size();++i) {
        const auto t=joint_detail::compose(joint_detail::compose(toeRigid(posed[i]),joint_detail::inverse(toeRigid(changed[i]))),toeRigid(standing[i]));
        const auto& r=t.rotation;
        result[i]={r[0],r[3],r[6],0,r[1],r[4],r[7],0,r[2],r[5],r[8],0,t.translation.x,t.translation.y,t.translation.z,1};
    }
    return result;
}
inline void attachComparisonToes(const std::vector<ComparisonToeAttachment>& joints,
    const std::vector<std::array<double,16>>& neutral,std::vector<std::array<double,16>>& current,Vec3 origin) {
    // Parent-before-child order carries each correction through the chain.
    for(const auto& joint:joints) {
        const auto motion=[&](std::size_t i) {
            return joint_detail::compose(toeRigid(current[i]),joint_detail::inverse(toeRigid(neutral[i])));
        };
        const auto a=motion(joint.parent),b=motion(joint.child);
        const Vec3 pivot=joint.pivot+origin;
        const Vec3 shift=(frameVector(a,pivot)+a.translation)-(frameVector(b,pivot)+b.translation);
        current[joint.child][12]+=shift.x;current[joint.child][13]+=shift.y;current[joint.child][14]+=shift.z;
    }
}
} // namespace gtc::native
