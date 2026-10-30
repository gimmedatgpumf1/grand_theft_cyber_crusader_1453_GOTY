#pragma once

// A single-coordinate spatial joint with coupled rotations and translations.
// The supplied curves and reference frames are model data; they are not inferred
// from meshes and are not evidence of a particular subject's knee kinematics.
#include "client/skeleton_joint.h"
#include "client/skeleton_coupling.h"

namespace gtc::native {
struct SpatialJointAxis {
    Vec3 direction;
    double constant = 0;
    std::optional<CalibratedJointCoupling> function;
};

class CalibratedSpatialJoint {
public:
    static std::optional<CalibratedSpatialJoint> create(
        JointRigidTransform parent, JointRigidTransform child, double minimum, double maximum,
        std::array<SpatialJointAxis, 3> rotations, std::array<SpatialJointAxis, 3> translations) {
        if (!joint_detail::rigid(parent) || !joint_detail::rigid(child) ||
            !std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) return std::nullopt;
        for (auto* axes : {&rotations, &translations}) for (auto& a : *axes) {
            if (!joint_detail::finite(a.direction) || !std::isfinite(a.constant)) return std::nullopt;
            const double n = length(a.direction);
            if (!std::isfinite(n) || n < 1e-9) return std::nullopt;
            a.direction = a.direction / n;
        }
        CalibratedSpatialJoint joint(parent, child, minimum, maximum, std::move(rotations), std::move(translations));
        if (!joint.setAngle(std::clamp(0.0, minimum, maximum))) return std::nullopt;
        return joint;
    }
    bool setAngle(double requested) {
        if (!std::isfinite(requested)) return false;
        const double angle = std::clamp(requested, minimum_, maximum_);
        std::array<double, 3> rotations{}, translations{};
        for (int i = 0; i < 3; ++i) {
            const auto r = value(rotations_[i], angle), t = value(translations_[i], angle);
            if (!r || !t) return false;
            rotations[i] = *r; translations[i] = *t;
        }
        Vec3 translation;
        for (int i = 0; i < 3; ++i) translation += translations_[i].direction * translations[i];
        if (!joint_detail::finite(translation)) return false;
        angle_ = angle;
        rotationValues_ = rotations;
        translationValues_ = translations;
        return true;
    }
    double angle() const { return angle_; }
    JointRigidTransform childInParent() const {
        JointRigidTransform motion;
        for (int i = 0; i < 3; ++i)
            motion = joint_detail::compose(motion, joint_detail::rotate(rotations_[i].direction, rotationValues_[i]));
        for (int i = 0; i < 3; ++i)
            motion.translation += translations_[i].direction * translationValues_[i];
        return joint_detail::compose(joint_detail::compose(parent_, motion), childInverse_);
    }
private:
    static std::optional<double> value(const SpatialJointAxis& axis, double x) {
        if (axis.function) return axis.function->value(x);
        return axis.constant;
    }
    CalibratedSpatialJoint(JointRigidTransform p, JointRigidTransform c, double lo, double hi,
        std::array<SpatialJointAxis, 3> r, std::array<SpatialJointAxis, 3> t)
        : parent_(p), childInverse_(joint_detail::inverse(c)), minimum_(lo), maximum_(hi),
          rotations_(std::move(r)), translations_(std::move(t)) {}
    JointRigidTransform parent_, childInverse_;
    double minimum_, maximum_, angle_ = 0;
    std::array<SpatialJointAxis, 3> rotations_, translations_;
    std::array<double, 3> rotationValues_{}, translationValues_{};
};
} // namespace gtc::native
