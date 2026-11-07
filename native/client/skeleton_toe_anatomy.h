#pragma once

#include "client/skeleton_body.h"
#include "client/skeleton_finger_frame.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace gtc::native {
// Pose intact bones about the existing toe joints. Shaft directions are shape
// estimates, not radiographic measurements of the original subject. The MTP
// guard bands are based on published normal ranges, with a few degrees of
// tolerance for using mesh PCA rather than radiographic shaft bisectors. Pitch
// limits are modelling choices, not population measurements or a diagnosis:
// https://pmc.ncbi.nlm.nih.gov/articles/PMC10218812/
// http://uwmsk.org/footalignment/doku.php?id=hallux_valgus
inline bool alignAnatomicalReferenceToes(SkeletonModel& model) {
    constexpr double degrees=180.0/3.14159265358979323846;
    const auto point=[](const std::array<double,16>& m,Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    const auto center=[&](int id,const std::vector<std::array<double,16>>& pose) {
        const auto& s=model.sourceStructures()[id];Vec3 c{};
        for(std::size_t i=0;i<s.pointCount;++i)c+=model.bones()[s.group].points[s.firstPoint+i];
        return point(pose[id],c/double(s.pointCount));
    };
    const auto shaft=[&](int id,const std::vector<std::array<double,16>>& pose) {
        const auto& s=model.sourceStructures()[id];const Vec3* p=model.bones()[s.group].points.data()+s.firstPoint;
        Vec3 c{};for(std::size_t i=0;i<s.pointCount;++i)c+=p[i];c=c/double(s.pointCount);
        // Source axes can be in any orientation; the posed foot's forward
        // direction, transformed back to the bone, disambiguates its sign.
        const auto& m=pose[id];const Vec3 hint{m[2],m[6],m[10]};
        Vec3 a=FingerReferenceFrames::shaft(p,s.pointCount,c,hint);
        a=point(m,a)-point(m,{});return a.z<0?a*-1.0:a;
    };
    const auto link=[&](int a,int b) {
        for(std::size_t i=0;i<model.sourceLinks().size();++i) {
            const auto& e=model.sourceLinks()[i];
            if((e.first==std::size_t(a)&&e.second==std::size_t(b)) ||
               (e.second==std::size_t(a)&&e.first==std::size_t(b)))return i;
        }
        return model.sourceLinks().size();
    };
    // Calibrate from the actual change produced by the joint rather than
    // assuming a left/right axis sign. Keep the anatomical bend limits.
    const auto solve=[&](std::size_t joint,bool yaw,const auto& error) {
        for(int n=0;n<3;++n) {
            const auto q=yaw?model.referenceToeYaw(joint):model.referenceBend(joint);
            if(!q)return false;
            const double before=error();
            const auto set=[&](double v){return yaw?model.setReferenceToeYaw(joint,v):model.setReferenceBend(joint,v);};
            if(!set(*q+1))return false;
            const auto next=yaw?model.referenceToeYaw(joint):model.referenceBend(joint);
            if(!next)return false;
            const double delta=*next-*q;
            const double slope=std::abs(delta)>1e-8?std::remainder(error()-before,360.0)/delta:0;
            const double correction=std::abs(slope)>.04?std::clamp(-before/slope,-25.0,25.0):0;
            if(!set(*q+correction))return false;
        }
        return true;
    };
    const auto az=[](Vec3 a){return std::atan2(a.x,a.z)*degrees;};
    const auto pitch=[](Vec3 a){return std::atan2(a.y,std::hypot(a.x,a.z))*degrees;};
    // Valgus is toward the little toe. Retain an individual's angle where it
    // already falls in these broad guard bands rather than imposing one ideal
    // shape. Tolerance reflects the difference between PCA and X-ray axes.
    constexpr std::array<double,5> lower{0,-5,-5,-5,-15};
    constexpr std::array<double,5> upper{14,20,17.5,10,8.5};
    constexpr std::array<double,5> lowPitch{-10,-12,-12,-12,-8};
    constexpr std::array<double,5> highPitch{8,8,8,8,18};
    for(char side:{'L','R'})for(int digit=1;digit<=5;++digit) {
        const std::string n=std::to_string(digit)+"_"+side;
        const int meta=model.sourceStructure("FootMeta"+n),prox=model.sourceStructure("FootProxP"+n);
        const int mid=digit==1?-1:model.sourceStructure("FootMidP"+n),tip=model.sourceStructure("FootDistP"+n);
        if(meta<0||prox<0||tip<0||(digit>1&&mid<0))return false;
        const auto base=link(meta,prox),ip=link(prox,mid<0?tip:mid);
        if(base==model.sourceLinks().size()||ip==model.sourceLinks().size())return false;
        const double lateral=side=='L'?1.0:-1.0;
        for(int pass=0;pass<2;++pass) {
            if(!solve(base,true,[&](){const auto pose=model.sourceMatrices({},0);
                const double angle=lateral*std::remainder(az(shaft(prox,pose))-az(shaft(meta,pose)),360.0);
                return lateral*(angle-std::clamp(angle,lower[digit-1],upper[digit-1]));}))return false;
            if(!solve(base,false,[&](){const auto pose=model.sourceMatrices({},0);
                const double angle=pitch(shaft(prox,pose));
                return angle-std::clamp(angle,lowPitch[digit-1],highPitch[digit-1]);}))return false;
        }
        if(digit==1) {
            const auto pose=model.sourceMatrices({},0);
            if(pitch(shaft(tip,pose))-pitch(shaft(prox,pose))>10 &&
               !solve(ip,false,[&](){const auto current=model.sourceMatrices({},0);
                   return pitch(shaft(tip,current))-pitch(shaft(prox,current))-5.0;}))return false;
        } else if(digit==5) {
            const auto dip=link(mid,tip);
            if(dip==model.sourceLinks().size())return false;
            const auto pose=model.sourceMatrices({},0);
            if(pitch(center(tip,pose)-center(mid,pose)) < -40) {
                // Avoid an extreme down-turned tip without rotating the
                // source PIP frame sideways into its neighbouring toe.
                if(!model.setReferenceBend(ip,0) || !model.setReferenceBend(dip,-20))return false;
            }
        }
    }
    return true;
}
} // namespace gtc::native
