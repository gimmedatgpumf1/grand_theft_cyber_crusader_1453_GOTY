// The reference rig: the anatomical kinematic tree the comparison measures
// against, built from the source links and structures the model loaded. It is
// one long piece of work that reads the model and produces a rig, so it lives
// apart from the rest of the body model.

#include "client/skeleton_body.h"

#include "client/skeleton_body_math.h"
#include "client/skeleton_finger_frame.h"
#include "client/skeleton_shoulder_frame.h"
#include "client/skeleton_leg_frame.h"
#include "client/skeleton_spine_frame.h"
#include "client/skeleton_toe_frame.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {

using namespace skeleton_detail;

bool SkeletonModel::installReferenceRig(std::string& error) {
    if (sourceLinks_.empty() || sourceStructures_.empty()) {
        error = "Cannot install a source rig without anatomical links"; return false;
    }
    const int root = sourceStructure("Sacrum"); if (root < 0) { error = "Skeleton is missing sacrum"; return false; }
    const std::size_t count = sourceStructures_.size();
    const std::size_t missing = static_cast<std::size_t>(-1); std::vector<std::size_t> mapping(count, missing), queue;
    std::vector<bool> visited(count); FingerReferenceFrames fingerFrames(count);
    std::vector<double> neutralAlignments(count+1,-1.0);
    AnatomicalKinematicTree rig;
    std::vector<AnatomicalKinematicTree::PoseUpdate> bindPose;
    const auto points = [&](std::size_t index) -> std::pair<const Vec3*, std::size_t> {
        const auto& part = sourceStructures_[index];
        return {bones_[part.group].points.data()+part.firstPoint, part.pointCount};
    };
    const auto center = [&](std::size_t index) {
        const auto [p,n] = points(index);
        Vec3 sum;
        for (std::size_t i=0; i<n; ++i) sum += p[i];
        return n ? sum / static_cast<double>(n) : Vec3{};
    };
    const auto contact = [&](std::size_t first, std::size_t second) {
        const auto [a,na] = points(first); const auto [b,nb] = points(second);
        const std::size_t as = std::max<std::size_t>(1,na/192), bs = std::max<std::size_t>(1,nb/192);
        double best = 1e9; Vec3 result = (center(first)+center(second))*0.5;
        for (std::size_t i=0; i<na; i+=as) for (std::size_t j=0; j<nb; j+=bs) {
            const double d = length(a[i]-b[j]);
            if (d < best) { best = d; result = (a[i]+b[j])*0.5; }
        }
        return result;
    };
    const std::array<std::optional<LegReferenceFrames>,2> legs{sourceLegFrame(*this,0),sourceLegFrame(*this,1)};
    const auto pelvisAngle=levelPelvisAngle(*this);
    if (!pelvisAngle) { error="Missing standing pelvis frame"; return false; }
    referencePelvisAngle_=*pelvisAngle;
    JointRigidTransform rootFrame; rootFrame.translation=center(static_cast<std::size_t>(root));
    auto rootJoint=CalibratedRotationalJoint::create(rootFrame,rootFrame,{{{0,0,1},-0.5,0.5}});
    auto rootBody=rootJoint ? rig.addJoint(0,*rootJoint) : std::nullopt;
    if (!rootBody) { error="Unable to add source root"; return false; }
    bindPose.push_back({*rootBody,{0}});
    mapping[static_cast<std::size_t>(root)]=*rootBody;
    visited[static_cast<std::size_t>(root)]=true; queue.push_back(static_cast<std::size_t>(root));
    const auto suffix = [](const std::string& name) -> int {
        return name.size() >= 2 && name.substr(name.size()-2) == "_L" ? 0 : 1;
    };
    constexpr double toRadians = 3.14159265358979323846 / 180.0;
    double spineTurn=0; std::array<std::optional<ShoulderReferenceFrame>,2> shoulders; std::optional<NeckReferenceFrames> neckFrames;
    for (std::size_t head=0; head<queue.size(); ++head) {
        const auto parent = queue[head];
        for (const auto& link : sourceLinks_) {
            if (link.first != parent && link.second != parent) continue;
            const auto child = link.first == parent ? link.second : link.first;
            if (visited[child]) continue;
            fingerFrames.inherit(parent,child);
            const auto& parentName = sourceStructures_[parent].name;
            const auto& childName = sourceStructures_[child].name;
            const auto bounds = referenceBendRange(link.mobility, parentName, childName);
            std::optional<std::size_t> body;
            std::vector<double> bindAngles{0};
            Vec3 alignmentAxis, toeYawAxis; double alignmentAngle=0; bool alignToNeutral=false;
            bool coupledPatella = false;
            if (link.mobility == "patellofemoral_glide" && hasKneeAxes_) {
                // Patellar flexion is approximately 70% of tibiofemoral
                // flexion (Amis et al., 2006). This is a rotational
                // approximation; actual patellar translation is not modelled.
                const int side = suffix(childName);
                const Vec3 axis=kneeAxes_[side].medialDirection * (side==0 ? -1.0 : 1.0);
                JointRigidTransform frame; frame.translation=kneeAxes_[side].origin;
                auto joint=CalibratedRotationalJoint::create(frame,frame,
                    {{axis,-0.7*135*toRadians,0.7*135*toRadians}});
                if (!joint) { error="Invalid patellar joint " + childName; return false; }
                body=rig.addJoint(mapping[parent],*joint); coupledPatella=true;
            } else if (bounds) {
                Vec3 pivot = contact(parent, child); const auto jawFrame=link.mobility=="temporomandibular" ? sourceJawHinge(*this) : std::nullopt;
                if (link.mobility=="temporomandibular") { if (!jawFrame) { error="Missing jaw hinge"; return false; } pivot=jawFrame->pivot; }
                const int side = suffix(childName);
                if (link.mobility == "shoulder_ball_socket") {
                    const int humerus = bone(side == 0 ? "humerus_l" : "humerus_r");
                    if (humerus < 0) { error = "Missing humerus"; return false; }
                    pivot = bones_[static_cast<std::size_t>(humerus)].pivot;
                } else if (link.mobility == "hip_ball_socket") {
                    const int femur = bone(side == 0 ? "femur_l" : "femur_r");
                    if (femur < 0) { error = "Missing fitted femur"; return false; }
                    pivot = bones_[static_cast<std::size_t>(femur)].pivot;
                } else if (link.mobility == "knee_modified_hinge" && hasKneeAxes_) {
                    pivot = kneeAxes_[side].origin;
                } else if (link.mobility == "elbow_hinge") {
                    // The radius and ulna share one elbow flexion axis.
                    const int h = sourceStructure(side == 0 ? "Humerus_L" : "Humerus_R");
                    const int u = sourceStructure(side == 0 ? "Ulna_L" : "Ulna_R");
                    if (hasElbowAxes_) pivot=elbowAxes_[side].origin;
                    else if (h >= 0 && u >= 0) pivot = contact(static_cast<std::size_t>(h),static_cast<std::size_t>(u));
                } else if (link.mobility == "wrist_condyloid") {
                    // The scaphoid and lunate both articulate with the radius.
                    // Their palm descendants form one group; use one shared
                    // frame instead of letting the wrist split that group.
                    const int r = sourceStructure(side == 0 ? "Radius_L" : "Radius_R");
                    const int sc = sourceStructure(side == 0 ? "HandScaphoid_L" : "HandScaphoid_R");
                    const int lu = sourceStructure(side == 0 ? "HandLuna_L" : "HandLuna_R");
                    if (r < 0 || sc < 0 || lu < 0) { error = "Missing wrist bones"; return false; }
                    pivot = (contact(static_cast<std::size_t>(r),static_cast<std::size_t>(sc)) +
                             contact(static_cast<std::size_t>(r),static_cast<std::size_t>(lu))) * 0.5;
                }
                Vec3 up = normalized(center(parent)-pivot);
                Vec3 down = normalized(center(child)-pivot);
                if (link.mobility == "shoulder_ball_socket") {
                    const int h = sourceStructure(side == 0 ? "Humerus_L" : "Humerus_R");
                    const int u = sourceStructure(side == 0 ? "Ulna_L" : "Ulna_R");
                    if (hasElbowAxes_) down=normalized(elbowAxes_[side].origin-pivot);
                    else if (h >= 0 && u >= 0)
                        down = normalized(contact(static_cast<std::size_t>(h),static_cast<std::size_t>(u))-pivot);
                }
                if (link.mobility == "wrist_condyloid") {
                    const int r = sourceStructure(side == 0 ? "Radius_L" : "Radius_R");
                    const int middle = sourceStructure(side == 0 ? "HandMeta3_L" : "HandMeta3_R");
                    if (r < 0 || middle < 0) { error = "Missing wrist frame"; return false; }
                    up = normalized(center(static_cast<std::size_t>(r))-pivot);
                    down = normalized(center(static_cast<std::size_t>(middle))-pivot);
                }
                if (link.mobility == "ankle_hinge") {
                    // Use the whole grouped foot, not the talus centroid, to
                    // establish plantarflexion relative to the tibial shaft.
                    const int tibia=sourceStructure(side==0 ? "Tibia_L" : "Tibia_R");
                    const int heel=sourceStructure(side==0 ? "Cal_L" : "Cal_R");
                    const int middle=sourceStructure(side==0 ? "FootMeta3_L" : "FootMeta3_R");
                    if (tibia<0 || heel<0 || middle<0) { error="Missing ankle frame"; return false; }
                    up=normalized(center(static_cast<std::size_t>(tibia))-pivot);
                    down=normalized(center(static_cast<std::size_t>(middle))-center(static_cast<std::size_t>(heel)));
                }
                if (link.mobility == "elbow_hinge") {
                    const int humerus = bone(side == 0 ? "humerus_l" : "humerus_r");
                    if (humerus >= 0) up = normalized(bones_[static_cast<std::size_t>(humerus)].pivot-pivot);
                    const int ulna = sourceStructure(side == 0 ? "Ulna_L" : "Ulna_R");
                    if (ulna >= 0) down = normalized(center(static_cast<std::size_t>(ulna))-pivot);
                }
                Vec3 axis = cross(up,down * -1.0), forearmAxis=down;
                double current = 0;
                if (link.mobility == "atlanto_occipital" || link.mobility == "intervertebral" ||
                    link.mobility == "temporomandibular") {
                    axis = jawFrame ? jawFrame->axis : Vec3{1,0,0};
                    if (link.mobility=="intervertebral" && parentName=="Sacrum" && childName=="L5") {
                        const int neck=sourceStructure("C7");
                        if (neck<0) { error="Missing spine reference"; return false; }
                        const Vec3 sacrum=center(parent);
                        const Vec3 shifted=sacrum+frameVector(joint_detail::rotate({0,0,1},*pelvisAngle),pivot-sacrum);
                        spineTurn=uprightSpineAngle(sacrum,shifted,shifted+center(neck)-pivot);
                        const double localTurn=spineTurn-*pelvisAngle;
                        alignmentAxis={0,0,localTurn<0 ? -1.0 : 1.0}; alignmentAngle=std::abs(localTurn); alignToNeutral=true;
                    }
                    if (parentName=="C7" && childName=="C6") {
                        neckFrames=sourceNeckFrames(*this,spineTurn); if (!neckFrames) { error="Missing neck frame"; return false; }
                        const double turn=neckFrames->neckTurn; alignmentAxis={0,0,turn<0 ? -1.0 : 1.0}; alignmentAngle=std::abs(turn); alignToNeutral=true;
                    }
                    if (link.mobility=="atlanto_occipital" && neckFrames) {
                        alignmentAxis=neckFrames->headAxis; alignmentAngle=neckFrames->headAngle; alignToNeutral=true;
                    }
                } else if (link.mobility == "atlanto_axial") {
                    axis = {0,1,0};
                    if (!neckFrames) { error="Missing neck frame"; return false; }
                    const double yaw=neckFrames->atlasYaw;
                    alignmentAxis={0,yaw<0 ? -1.0 : 1.0,0}; alignmentAngle=std::abs(yaw); alignToNeutral=true;
                } else if (link.mobility == "sternoclavicular" || link.mobility == "acromioclavicular") {
                    axis = {0,0,side == 0 ? 1.0 : -1.0};
                    if (link.mobility=="sternoclavicular") {
                        shoulders[side]=sourceShoulderFrame(*this,side,spineTurn);
                        if (!shoulders[side]) { error="Missing shoulder girdle frame"; return false; }
                        alignmentAxis=shoulders[side]->axis; alignmentAngle=shoulders[side]->angle;
                    } else {
                        if (!shoulders[side]) { error="Missing clavicle parent frame"; return false; }
                        alignmentAxis=shoulders[side]->scapularAxis; alignmentAngle=shoulders[side]->scapularAngle;
                    }
                    alignToNeutral=true;
                } else if (link.mobility == "shoulder_ball_socket") {
                    // The primary shoulder control is abduction: align the
                    // actual upper-arm shaft toward its side, not a world X
                    // rotation that assumes the subject began in neutral.
                    const Vec3 worldTarget = side == 0 ? Vec3{1,0,0} : Vec3{-1,0,0};
                    if (!shoulders[side]) { error="Missing shoulder parent frame"; return false; }
                    const auto trunk=joint_detail::rotate({0,0,1},spineTurn);
                    const auto parentRotation=joint_detail::compose(trunk,joint_detail::compose(shoulders[side]->correction,shoulders[side]->scapular));
                    const Vec3 target=frameVector(joint_detail::inverse(parentRotation),worldTarget);
                    axis = cross(down,target);
                    current = (3.14159265358979323846/2) -
                        std::acos(std::clamp(dot(down,target),-1.0,1.0));
                } else if (link.mobility == "hip_ball_socket" && hasKneeAxes_) {
                    // The same sagittal flexion plane on both sides of the pelvis.
                    axis=frameVector(joint_detail::rotate({0,0,1},-*pelvisAngle),{-1,0,0});
                    const auto& leg=legs[side]; if (!leg) { error="Missing leg frame"; return false; }
                    const auto correction=composeAlignments(leg->hipAlignment,leg->hipAngle,{0,0,1},-*pelvisAngle);
                    alignmentAxis=correction.first; alignmentAngle=correction.second; alignToNeutral=true;
                } else if (link.mobility == "knee_modified_hinge" && hasKneeAxes_) {
                    axis = kneeAxes_[side].medialDirection * (side == 0 ? -1.0 : 1.0);
                    up = normalized(up-axis*dot(up,axis));
                    const Vec3 other = normalized((down*-1.0)-axis*dot(down*-1.0,axis));
                    current = std::atan2(dot(axis,cross(up,other)),dot(up,other));
                    const auto& leg=legs[side]; if (!leg) { error="Missing leg frame"; return false; }
                    alignmentAxis=leg->kneeAlignment; alignmentAngle=leg->kneeAngle; alignToNeutral=true;
                } else if (link.mobility == "ankle_hinge") {
                    // Positive plantarflexion moves the forefoot away from
                    // the shin; 0 is perpendicular to the tibial shaft.
                    axis=cross(up,down);
                    current=std::asin(std::clamp(-dot(up,down),-1.0,1.0));
                    const auto& leg=legs[side]; if (!leg) { error="Missing foot frame"; return false; }
                    alignmentAxis=leg->ankleAlignment; alignmentAngle=leg->ankleAngle; alignToNeutral=true;
                } else if (link.mobility == "wrist_condyloid") {
                    const int ulna=sourceStructure(side==0 ? "Ulna_L" : "Ulna_R");
                    const int humerus=sourceStructure(side==0 ? "Humerus_L" : "Humerus_R");
                    if (ulna<0 || humerus<0) { error="Missing wrist forearm frame"; return false; }
                    const Vec3 elbow=hasElbowAxes_ ? elbowAxes_[side].origin : contact(humerus,ulna);
                    const auto frame=sourceWristFrame(*this,side,center(ulna)-elbow);
                    if (!frame) { error="Missing wrist palm frame"; return false; }
                    const auto wrist=wristReferenceFrame(frame->wrist-elbow,frame->hand,frame->palm);
                    axis=wrist.flexion; alignmentAxis=wrist.alignment; alignmentAngle=wrist.alignmentAngle; alignToNeutral=true;
                } else if (link.mobility == "thumb_cmc_saddle") {
                    const auto frame=sourceThumbFrame(*this,fingerFrames,parent,child,side);
                    if (!frame) { error="Missing thumb reference frame"; return false; }
                    axis=frame->flexion; alignmentAxis=frame->alignment; alignmentAngle=frame->alignmentAngle; alignToNeutral=true;
                } else if (link.mobility == "finger_mcp_condyloid" ||
                           link.mobility == "finger_pip_hinge" || link.mobility == "finger_dip_hinge" ||
                           link.mobility == "thumb_mcp" || link.mobility == "thumb_ip_hinge") {
                    const auto frame=sourceFingerFrame(*this,fingerFrames,parent,child,side,up,down,
                                                       link.mobility=="finger_mcp_condyloid" || link.mobility=="thumb_mcp");
                    if (!frame) { error="Missing finger reference frame"; return false; }
                    axis=frame->flexion; alignmentAxis=frame->alignment; alignmentAngle=frame->alignmentAngle; alignToNeutral=true;
                } else if (link.mobility == "great_toe_mtp" || link.mobility == "lesser_toe_mtp" ||
                           link.mobility == "toe_ip_hinge") {
                    const auto frame=sourceToeFrame(*this,fingerFrames,parent,child,side,pivot,legs[side]->soleNormal);
                    if (!frame) { error="Missing toe reference frame"; return false; }
                    axis=frame->flexion; alignmentAxis=frame->alignment; alignmentAngle=frame->alignmentAngle; toeYawAxis=frame->yaw; alignToNeutral=true;
                } else if (link.mobility == "elbow_hinge" && hasElbowAxes_) {
                    const int h=bone(side==0 ? "humerus_l" : "humerus_r");
                    const auto w=sourceWristFrame(*this,side,down);
                    if (h<0 || !w) { error="Missing elbow hand frame"; return false; }
                    forearmAxis=normalized(w->wrist-pivot);
                    const auto wrist=wristReferenceFrame(forearmAxis,w->hand,w->palm);
                    const Vec3 palm=frameVector(joint_detail::rotate(wrist.alignment,wrist.alignmentAngle),w->palm);
                    if (!shoulders[side]) { error="Missing elbow parent frame"; return false; }
                    const auto trunk=joint_detail::rotate({0,0,1},spineTurn);
                    const auto parentRotation=joint_detail::compose(trunk,joint_detail::compose(shoulders[side]->correction,shoulders[side]->scapular));
                    const auto frame=mirroredElbowFrame(bones_[h].pivot,pivot,w->wrist,palm,side==0,parentRotation);
                    axis=frame.flexion; alignmentAxis=frame.alignment; alignmentAngle=frame.alignmentAngle; alignToNeutral=true;
                } else if (link.mobility == "elbow_hinge") {
                    current = std::atan2(length(axis),std::clamp(dot(up,down*-1.0),-1.0,1.0));
                }
                if (length(axis) < 1e-5) {
                    Vec3 guide = std::abs(up.z) < 0.9 ? Vec3{0,0,1} : Vec3{0,1,0};
                    axis = cross(up,guide);
                }
                if (length(axis) < 1e-8) { error = "Degenerate source joint axis for " + childName; return false; }
                // Keep the registered source pose as the identity transform.
                current=std::clamp(current,bounds->extension*toRadians,bounds->flexion*toRadians);
                JointRigidTransform frame; frame.translation = pivot;
                const auto parentFrame = joint_detail::compose(frame,joint_detail::rotate(normalized(axis),-current));
                std::vector<JointRotationalAxis> axes{{axis,bounds->extension*toRadians,bounds->flexion*toRadians}};
                bindAngles={current};
                if (link.mobility=="sternoclavicular" || link.mobility=="acromioclavicular") {
                    const double sign=side==0 ? 1.0 : -1.0;
                    axes.push_back({{0,sign,0},-20*toRadians,20*toRadians}); bindAngles.push_back(0);
                    const Vec3 axial=link.mobility=="sternoclavicular" ? shoulders[side]->clavicleAxis : Vec3{sign,0,0};
                    axes.push_back({axial,-30*toRadians,30*toRadians}); bindAngles.push_back(0);
                }
                if (alignToNeutral && link.mobility!="hip_ball_socket") { axes.push_back({alignmentAxis,0,3.14159265358979323846}); bindAngles.push_back(0); }
                if (length(toeYawAxis)>1e-8) { axes.push_back({toeYawAxis,-1.57079632679,1.57079632679}); bindAngles.push_back(0); }
                if (link.mobility=="elbow_hinge" && alignToNeutral) { const auto r=*referenceBendRange("radioulnar_pivot"); axes.push_back({forearmAxis*(side==0 ? 1.0 : -1.0),r.extension*toRadians,r.flexion*toRadians}); bindAngles.push_back(0); }
                if (link.mobility == "hip_ball_socket" && hasKneeAxes_) {
                    // Internal/external rotation around the femoral shaft.
                    const Vec3 shaft=frameVector(joint_detail::rotate({0,0,1},-*pelvisAngle),{0,side==0 ? -1.0 : 1.0,0});
                    const auto rotation=*referenceBendRange("hip_rotation"), abduction=*referenceBendRange("hip_abduction");
                    axes.push_back({shaft,rotation.extension*toRadians,rotation.flexion*toRadians}); bindAngles.push_back(0);
                    axes.push_back({{0,0,side==0 ? 1.0 : -1.0},abduction.extension*toRadians,abduction.flexion*toRadians}); bindAngles.push_back(0);
                    axes.push_back({alignmentAxis,0,3.14159265358979323846}); bindAngles.push_back(0);
                }
                auto joint = CalibratedRotationalJoint::create(parentFrame,frame,axes);
                if (!joint || !joint->setAngles(bindAngles)) { error = "Invalid source joint " + childName; return false; }
                body = rig.addJoint(mapping[parent],*joint);
            } else {
                // No numeric bend range is specified for gliding/compound
                // joints; attach them rather than inventing medical angles.
                body = rig.addFixed(mapping[parent],{});
            }
            if (!body) { error = "Unable to attach source joint " + childName; return false; }
            if (bounds || coupledPatella) bindPose.push_back({*body,bindAngles});
            if (alignToNeutral) neutralAlignments[*body]=alignmentAngle;
            mapping[child] = *body; visited[child] = true; queue.push_back(child);
        }
    }
    if (queue.size() != count) { error = "Anatomical source graph is disconnected"; return false; }
    rig.captureBindPose();
    if (!setSourceRig(std::move(rig),mapping,error)) return false;
    referenceBindPose_ = std::move(bindPose);
    neutralAlignments_ = std::move(neutralAlignments); disabledAlignments_.assign(neutralAlignments_.size(),false);
    return true;
}

}  // namespace gtc::native
