#pragma once

#include "client/skeleton_joint.h"
#include "client/skeleton_body.h"

namespace gtc::native {
struct ReferenceFrame {
    Vec3 flexion, alignment;
    double alignmentAngle;
};
inline Vec3 frameVector(const JointRigidTransform& t, Vec3 v) {
    const auto& m=t.rotation;
    return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,
            m[6]*v.x+m[7]*v.y+m[8]*v.z};
}
inline ReferenceFrame wristReferenceFrame(Vec3 proximal, Vec3 distal, Vec3 palm) {
    proximal=normalized(proximal); distal=normalized(distal);
    Vec3 alignment=cross(distal,proximal);
    const double angle=std::atan2(length(alignment),std::clamp(dot(distal,proximal),-1.0,1.0));
    if (length(alignment)<1e-8) alignment=cross(distal,palm);
    alignment=normalized(alignment);
    const Vec3 neutralPalm=frameVector(joint_detail::rotate(alignment,angle),palm);
    return {normalized(cross(proximal,neutralPalm)),alignment,angle};
}

// Measure the registered wrist/hand direction and palm plane from the actual
// labelled bones, before applying any pose or clinical flexion coordinate.
struct SourceWristFrame { ReferenceFrame frame; Vec3 palm, hand, wrist, radial; };
inline std::optional<SourceWristFrame> sourceWristFrame(const SkeletonModel& model, int side, Vec3 forearm) {
    const std::string tag=side==0 ? "_L" : "_R";
    const int r=model.sourceStructure("Radius"+tag), sc=model.sourceStructure("HandScaphoid"+tag);
    const int lu=model.sourceStructure("HandLuna"+tag), i=model.sourceStructure("HandMeta2"+tag);
    const int m=model.sourceStructure("HandMeta3"+tag), l=model.sourceStructure("HandMeta5"+tag);
    if (r<0 || sc<0 || lu<0 || i<0 || m<0 || l<0) return std::nullopt;
    const auto points=[&](int index) {
        const auto& s=model.sourceStructures()[index];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](int index) {
        const auto [p,n]=points(index); Vec3 sum;
        for (std::size_t j=0;j<n;++j) sum+=p[j];
        return n ? sum/static_cast<double>(n) : Vec3{};
    };
    const auto contact=[&](int a, int b) {
        const auto [p,n]=points(a); const auto [q,k]=points(b);
        const auto ps=std::max<std::size_t>(1,n/192), qs=std::max<std::size_t>(1,k/192);
        Vec3 result=(center(a)+center(b))*0.5; double best=1e9;
        for (std::size_t x=0;x<n;x+=ps) for (std::size_t y=0;y<k;y+=qs) {
            const double d=length(p[x]-q[y]);
            if (d<best) { best=d; result=(p[x]+q[y])*0.5; }
        }
        return result;
    };
    const Vec3 wrist=(contact(r,sc)+contact(r,lu))*0.5, hand=normalized(center(m)-wrist);
    const Vec3 palm=normalized(cross(center(i)-center(l),hand))*(side==0 ? -1.0 : 1.0);
    const Vec3 across=center(i)-center(l);
    const Vec3 radial=normalized(across-hand*dot(across,hand));
    return SourceWristFrame{wristReferenceFrame(forearm,hand,palm),palm,hand,wrist,radial};
}

// Compose two source-to-neutral rotations as a single fixed alignment.
inline std::pair<Vec3,double> composeAlignments(Vec3 first, double a, Vec3 second, double b) {
    const double ca=std::cos(a/2), sa=std::sin(a/2), cb=std::cos(b/2), sb=std::sin(b/2);
    double w=cb*ca-sb*sa*dot(second,first);
    Vec3 v=first*(cb*sa)+second*(sb*ca)+cross(second,first)*(sb*sa);
    if (w<0) { w=-w; v=v*-1.0; }
    const double n=length(v);
    return {n<1e-10 ? first : v/n, 2*std::atan2(n,w)};
}

// Match the illustrated straight, palm-forward T-pose while leaving source
// geometry intact. Elbow flexion remains a separate, mirrored coordinate.
inline ReferenceFrame mirroredElbowFrame(Vec3 shoulder, Vec3 elbow, Vec3 forearm,
                                          Vec3 sourcePalm, bool left, double spineTurn=0) {
    const Vec3 arm=normalized(elbow-shoulder), fore=normalized(forearm-elbow);
    const auto torso=joint_detail::rotate({0,0,1},spineTurn);
    const auto inverseTorso=joint_detail::inverse(torso);
    const Vec3 target=frameVector(inverseTorso,left ? Vec3{1,0,0} : Vec3{-1,0,0});
    Vec3 shoulderAxis=cross(arm,target);
    const double shoulderAngle=std::atan2(length(shoulderAxis),std::clamp(dot(arm,target),-1.0,1.0));
    if (length(shoulderAxis)<1e-8) shoulderAxis={0,0,1};
    const auto shoulderPose=joint_detail::rotate(normalized(shoulderAxis),shoulderAngle);
    const Vec3 flexion=normalized(frameVector(joint_detail::inverse(shoulderPose),
                                                frameVector(inverseTorso,{0,left ? -1.0 : 1.0,0})));
    Vec3 straighten=cross(fore,arm);
    const double straightAngle=std::atan2(length(straighten),std::clamp(dot(fore,arm),-1.0,1.0));
    if (length(straighten)<1e-8) straighten=flexion;
    straighten=normalized(straighten);
    const Vec3 palm=frameVector(torso,frameVector(shoulderPose,
                                    frameVector(joint_detail::rotate(straighten,straightAngle),sourcePalm)));
    const Vec3 worldAxis{1,0,0}, forward{0,0,1};
    const double turn=std::atan2(dot(worldAxis,cross(palm,forward)),dot(palm,forward));
    const auto [alignment,angle]=composeAlignments(straighten,straightAngle,arm*(left ? 1.0 : -1.0),turn);
    return {flexion,alignment,angle};
}
} // namespace gtc::native
