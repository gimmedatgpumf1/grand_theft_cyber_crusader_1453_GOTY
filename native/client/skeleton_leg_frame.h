#pragma once

#include "client/skeleton_limb_frame.h"

namespace gtc::native {
struct LegReferenceFrames {
    Vec3 hip, knee, ankle;
    Vec3 hipAlignment, kneeAlignment, ankleAlignment, soleNormal, solePoint;
    double hipAngle, kneeAngle, ankleAngle, soleHeight;
};

// Straighten the real hip-to-knee and knee-to-ankle joint-centre lines, then
// point the foot forward. The source-to-reference rotations are separate from
// bounded clinical flexion and axial rotation; they do not modify the meshes.
inline std::optional<LegReferenceFrames> sourceLegFrame(const SkeletonModel& model, int side) {
    const std::string tag=side==0 ? "_L" : "_R";
    const int femur=model.sourceStructure("Femur"+tag), tibia=model.sourceStructure("Tibia"+tag);
    const int talus=model.sourceStructure("Tal"+tag), heel=model.sourceStructure("Cal"+tag);
    const int met=model.sourceStructure("FootMeta3"+tag), big=model.sourceStructure("FootMeta1"+tag);
    const int little=model.sourceStructure("FootMeta5"+tag), bone=model.bone(side==0 ? "femur_l" : "femur_r");
    const int medial=model.sourceStructure("FootSesaMed"+tag), lateral=model.sourceStructure("FootSesaLat"+tag);
    if (femur<0 || tibia<0 || talus<0 || heel<0 || met<0 || big<0 || little<0 || medial<0 || lateral<0 || bone<0) return std::nullopt;
    const auto points=[&](int i) {
        const auto& s=model.sourceStructures()[i];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](int i) {
        const auto [p,n]=points(i); Vec3 c; for (std::size_t j=0;j<n;++j) c+=p[j];
        return n ? c/static_cast<double>(n) : Vec3{};
    };
    const auto contact=[&](int a, int b) {
        const auto [p,n]=points(a); const auto [q,k]=points(b);
        const auto ps=std::max<std::size_t>(1,n/192), qs=std::max<std::size_t>(1,k/192);
        double best=1e9; Vec3 result=(center(a)+center(b))*0.5;
        for (std::size_t x=0;x<n;x+=ps) for (std::size_t y=0;y<k;y+=qs) {
            const double d=length(p[x]-q[y]);
            if (d<best) { best=d; result=(p[x]+q[y])*0.5; }
        }
        return result;
    };
    const Vec3 hip=model.bones()[bone].pivot, knee=model.kneeAxes()[side].origin, ankle=contact(tibia,talus);
    const Vec3 thigh=normalized(knee-hip), shin=normalized(ankle-knee);
    if (length(thigh)<1e-8 || length(shin)<1e-8) return std::nullopt;
    const auto align=[](Vec3 from,Vec3 to) {
        Vec3 axis=cross(from,to); const double n=length(axis);
        if (n<1e-8) axis=std::abs(from.x)<0.9 ? normalized(cross(from,{1,0,0})) : normalized(cross(from,{0,1,0}));
        else axis=axis/n;
        return std::pair<Vec3,double>{axis,std::atan2(n,std::clamp(dot(from,to),-1.0,1.0))};
    };
    const auto [kAxis,kAngle]=align(shin,thigh);
    const auto [hAxis,hAngle]=align(thigh,{0,-1,0});
    const Vec3 bendAxis=normalized(model.kneeAxes()[side].medialDirection*(side==0 ? -1.0 : 1.0));
    Vec3 up=normalized(center(femur)-knee), down=normalized(center(tibia)-knee);
    up=normalized(up-bendAxis*dot(up,bendAxis));
    const Vec3 other=normalized((down*-1.0)-bendAxis*dot(down*-1.0,bendAxis));
    const double current=std::clamp(std::atan2(dot(bendAxis,cross(up,other)),dot(up,other)),0.0,135.0*3.141592653589793/180.0);
    // At knee coordinate zero the parent frame first cancels the source bend.
    const auto [neutralKneeAxis,neutralKneeAngle]=composeAlignments(kAxis,kAngle,bendAxis,current);
    const Vec3 foot=normalized(center(met)-center(heel));
    const Vec3 ankleUp=normalized(center(tibia)-ankle);
    const Vec3 ankleAxis=normalized(cross(ankleUp,foot));
    if (length(ankleAxis)<1e-8) return std::nullopt;
    const double ankleCurrent=std::clamp(std::asin(std::clamp(-dot(ankleUp,foot),-1.0,1.0)),
                                         -20.0*3.141592653589793/180.0,50.0*3.141592653589793/180.0);
    const auto rotation=joint_detail::compose(joint_detail::rotate(hAxis,hAngle),
                        joint_detail::compose(joint_detail::rotate(kAxis,kAngle),joint_detail::rotate(ankleAxis,-ankleCurrent)));
    // A flat foot is defined by the underside of the heel and the distal ends
    // of the first and fifth metatarsals and their sesamoids, not by their
    // centres or the tibia angle.
    const auto lowest=[&](int index,bool distal,const JointRigidTransform& pose) {
        const auto [p,n]=points(index); double minAlong=1e9, maxAlong=-1e9;
        for (std::size_t i=0;i<n;++i) { const double v=dot(p[i],foot); minAlong=std::min(minAlong,v); maxAlong=std::max(maxAlong,v); }
        Vec3 result; double low=1e9;
        for (std::size_t i=0;i<n;++i) {
            if (distal && dot(p[i],foot)<minAlong+0.6*(maxAlong-minAlong)) continue;
            const Vec3 v=frameVector(pose,p[i]-ankle);
            if (v.y<low) { low=v.y; result=v; }
        }
        return result;
    };
    Vec3 flatAxis{1,0,0}; double flatAngle=0;
    // Updating the three support vertices after each small correction accounts
    // for a different point becoming the bottom of an irregular source bone.
    for (int pass=0;pass<12;++pass) {
        const auto pose=joint_detail::compose(joint_detail::rotate(flatAxis,flatAngle),rotation);
        const Vec3 back=lowest(heel,false,pose), outer=lowest(little,true,pose);
        Vec3 inner=lowest(big,true,pose);
        for (int sesamoid : {medial,lateral}) {
            const Vec3 candidate=lowest(sesamoid,false,pose);
            if (candidate.y<inner.y) inner=candidate;
        }
        Vec3 normal=normalized(cross(inner-back,outer-back));
        if (normal.y<0) normal=normal*-1.0;
        if (length(normal)<1e-8) return std::nullopt;
        const auto [stepAxis,stepAngle]=align(normal,{0,1,0});
        const auto accumulated=composeAlignments(flatAxis,flatAngle,stepAxis,stepAngle);
        flatAxis=accumulated.first; flatAngle=accumulated.second;
    }
    const Vec3 ankleAlignment=frameVector(joint_detail::inverse(rotation),flatAxis);
    const Vec3 f=frameVector(joint_detail::rotate(flatAxis,flatAngle),frameVector(rotation,foot));
    const double yaw=std::atan2(-f.x,f.z);
    const auto [neutralHipAxis,neutralHipAngle]=composeAlignments(hAxis,hAngle,{0,1,0},yaw);
    const auto flattened=joint_detail::compose(joint_detail::rotate(flatAxis,flatAngle),rotation);
    const double sole=std::min({lowest(heel,false,flattened).y,lowest(big,true,flattened).y,
                                lowest(little,true,flattened).y,lowest(medial,false,flattened).y,lowest(lateral,false,flattened).y});
    const auto inverse=joint_detail::inverse(flattened);
    const Vec3 soleNormal=frameVector(inverse,{0,1,0});
    const Vec3 solePoint=ankle+frameVector(inverse,lowest(heel,false,flattened));
    return LegReferenceFrames{hip,knee,ankle,neutralHipAxis,neutralKneeAxis,ankleAlignment,soleNormal,solePoint,
                              neutralHipAngle,neutralKneeAngle,flatAngle,sole};
}
// Level the measured femoral-head centres, not the soles. Balancing the feet
// by tilting the whole pelvis made the male pelvis lean more than seven degrees.
// The source limbs can have unequal lengths; do not rotate the pelvic ring to
// disguise that difference or scale the original bones.
inline std::optional<double> levelPelvisAngle(const SkeletonModel& model) {
    const auto left=sourceLegFrame(model,0), right=sourceLegFrame(model,1);
    if (!left || !right) return std::nullopt;
    const double dx=left->hip.x-right->hip.x, dy=left->hip.y-right->hip.y;
    if (std::hypot(dx,dy)<1e-8) return std::nullopt;
    return -std::atan2(dy,dx);
}
} // namespace gtc::native
