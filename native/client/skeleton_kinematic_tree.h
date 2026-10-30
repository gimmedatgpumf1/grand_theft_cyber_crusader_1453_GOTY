#pragma once

// Forward kinematics for explicitly calibrated, rigid, open-chain joints.
// This does not infer anatomical frames, solve contacts or close joint loops.
// Use only with subject-fitted inputs; independent coordinate bounds are not a
// complete physiological range-of-motion model.
#include "client/skeleton_joint.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace gtc::native {

class AnatomicalKinematicTree {
public:
    struct PoseUpdate {
        std::size_t body;             // the child whose joint is being set
        std::vector<double> angles;   // radians, joint coordinate order
    };
    AnatomicalKinematicTree() {
        nodes_.push_back(Node{}); // one explicit root, no parent joint
        local_.resize(1);
        absolute_.resize(1);
        skin_.resize(1);
    }
    // Parent must already exist. Requiring topological order makes cycles and
    // duplicate parents impossible; anatomical closed loops need a solver that
    // this class deliberately does not pretend to implement.
    std::optional<std::size_t> addJoint(std::size_t parent, const CalibratedRotationalJoint& joint) {
        if (parent >= nodes_.size() || nodes_.size() >= 512) return std::nullopt;
        Node node;
        node.parent = parent;
        node.joint = joint;
        return append(std::move(node));
    }
    std::optional<std::size_t> addFixed(std::size_t parent, JointRigidTransform childInParent) {
        if (parent >= nodes_.size() || nodes_.size() >= 512 || !joint_detail::rigid(childInParent)) return std::nullopt;
        Node node;
        node.parent = parent;
        node.fixed = childInParent;
        return append(std::move(node));
    }
    std::size_t size() const { return nodes_.size(); }
    const CalibratedRotationalJoint* joint(std::size_t body) const {
        if (body >= nodes_.size() || !nodes_[body].joint) return nullptr;
        return &*nodes_[body].joint;
    }
    // Validate the entire request before changing any joint. Finite values are
    // clamped by each supplied model's bounds, not by invented global limits.
    bool setPose(const std::vector<PoseUpdate>& pose) {
        for (std::size_t i = 0; i < pose.size(); ++i) {
            const auto* j = joint(pose[i].body);
            if (!j || pose[i].angles.size() != j->angles().size()) return false;
            for (const auto& q : pose[i].angles) if (!std::isfinite(q)) return false;
            for (std::size_t k = 0; k < i; ++k) if (pose[k].body == pose[i].body) return false;
        }
        for (const auto& item : pose) nodes_[item.body].joint->setAngles(item.angles);
        dirty_ = true;
        return true;
    }
    // Freeze the current calibrated configuration as the mesh's bind pose.
    // This does not change the geometry or replace the joint model's neutral.
    void captureBindPose() {
        updateLocal();
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            nodes_[i].bindInverse = joint_detail::inverse(local_[i]);
        bound_ = true;
    }
    bool hasBindPose() const { return bound_; }
    // Source geometry may be stored in the common bind-pose coordinate system.
    // skinTransforms() maps that geometry into the current root placement.
    // No mesh points are edited or rescaled. Buffers are reused across poses.
    bool evaluate(JointRigidTransform root = {}) {
        if (!bound_ || !joint_detail::rigid(root)) return false;
        updateLocal();
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            absolute_[i] = joint_detail::compose(root, local_[i]);
            skin_[i] = joint_detail::compose(absolute_[i], nodes_[i].bindInverse);
        }
        return true;
    }
    const std::vector<JointRigidTransform>& absoluteTransforms() const { return absolute_; }
    const std::vector<JointRigidTransform>& skinTransforms() const { return skin_; }
private:
    struct Node {
        std::size_t parent = 0;
        std::optional<CalibratedRotationalJoint> joint;
        JointRigidTransform fixed, bindInverse;
    };
    std::optional<std::size_t> append(Node node) {
        nodes_.push_back(std::move(node));
        local_.resize(nodes_.size());
        absolute_.resize(nodes_.size());
        skin_.resize(nodes_.size());
        dirty_ = true;
        bound_ = false;
        return nodes_.size()-1;
    }
    void updateLocal() {
        if (!dirty_) return;
        local_[0] = {};
        for (std::size_t i = 1; i < nodes_.size(); ++i) {
            const auto& n = nodes_[i];
            const auto localJoint = n.joint ? n.joint->childInParent() : n.fixed;
            local_[i] = joint_detail::compose(local_[n.parent], localJoint);
        }
        dirty_ = false;
    }
    std::vector<Node> nodes_;
    std::vector<JointRigidTransform> local_, absolute_, skin_;
    bool dirty_ = true, bound_ = false;
};

} // namespace gtc::native
