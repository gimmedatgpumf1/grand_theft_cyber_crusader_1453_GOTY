#include "client/skeleton_body.h"
#include <algorithm>

namespace gtc::native {
bool SkeletonModel::restoreSourcePose() {
    if (sourceRig_) sourceRig_->clearReferenceAdjustments();
    std::fill(disabledAlignments_.begin(),disabledAlignments_.end(),false);
    return !referenceBindPose_.empty() && setSourcePose(referenceBindPose_);
}
bool SkeletonModel::setSourcePose(const std::vector<AnatomicalKinematicTree::PoseUpdate>& updates) {
    if (!sourceRig_ || !sourceRig_->setPose(updates)) return false;
    ++sourcePoseRevision_;
    return true;
}
bool SkeletonModel::adjustReferenceToe(std::size_t link,const JointRigidTransform& correction) {
    if (!sourceRig_ || link>=sourceLinks_.size() || !joint_detail::rigid(correction)) return false;
    const auto& edge=sourceLinks_[link];
    if(edge.mobility!="great_toe_mtp" && edge.mobility!="lesser_toe_mtp" && edge.mobility!="toe_ip_hinge" &&
       edge.mobility!="tarsal_glide" && edge.mobility!="sesamoid_glide")return false;
    const auto a=sourceBodies_[edge.first],b=sourceBodies_[edge.second];
    const auto body=sourceRig_->parent(b)==a?b:(sourceRig_->parent(a)==b?a:0);
    if(!body || !sourceRig_->evaluate())return false;
    const auto frame=sourceRig_->absoluteTransforms()[body];
    const auto local=joint_detail::compose(joint_detail::compose(joint_detail::inverse(frame),correction),frame);
    if(!sourceRig_->appendReferenceAdjustment(body,local))return false;
    ++sourcePoseRevision_;return true;
}
bool SkeletonModel::repositionReferenceToeJoint(std::size_t link,Vec3 pivot) {
    if (!sourceRig_ || link>=sourceLinks_.size()) return false;
    const auto& edge=sourceLinks_[link];
    if(edge.mobility!="toe_ip_hinge" && edge.mobility!="great_toe_mtp" && edge.mobility!="lesser_toe_mtp")return false;
    const auto a=sourceBodies_[edge.first],b=sourceBodies_[edge.second];
    const auto body=sourceRig_->parent(b)==a?b:(sourceRig_->parent(a)==b?a:0);
    if(!body || !sourceRig_->repositionReferenceJoint(body,pivot))return false;
    ++sourcePoseRevision_;return true;
}
bool SkeletonModel::orientReferenceToeJoint(std::size_t link,Vec3 shaft) {
    if (!sourceRig_ || link>=sourceLinks_.size()) return false;
    const auto& edge=sourceLinks_[link];
    if(edge.mobility!="toe_ip_hinge" && edge.mobility!="great_toe_mtp" && edge.mobility!="lesser_toe_mtp")return false;
    const auto a=sourceBodies_[edge.first],b=sourceBodies_[edge.second];
    const auto body=sourceRig_->parent(b)==a?b:(sourceRig_->parent(a)==b?a:0);
    if(!body || !sourceRig_->orientReferenceJoint(body,shaft))return false;
    ++sourcePoseRevision_;return true;
}
} // namespace gtc::native
