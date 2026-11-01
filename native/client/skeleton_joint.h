#pragma once

// Rigid, rotational joint primitive. Frames, axes and angle limits must be
// supplied in a calibrated anatomical reference configuration; this file does
// not infer them from bone meshes and is not a model of joint contact or glide.
#include "client/raster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace gtc::native {

struct JointRigidTransform {
    // Row-major orthonormal rotation, local-to-parent, and translation.
    std::array<double, 9> rotation{1,0,0, 0,1,0, 0,0,1};
    Vec3 translation{};
};

struct JointRotationalAxis {
    Vec3 direction; // expressed in the joint frame; normalised by Joint::create
    double minimum = 0, maximum = 0; // radians in the calibrated reference frame
};

namespace joint_detail {
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline JointRigidTransform compose(const JointRigidTransform& a, const JointRigidTransform& b) {
    JointRigidTransform r;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        r.rotation[i*3+j] = 0;
        for (int k = 0; k < 3; ++k) r.rotation[i*3+j] += a.rotation[i*3+k] * b.rotation[k*3+j];
    }
    r.translation = {a.rotation[0]*b.translation.x + a.rotation[1]*b.translation.y + a.rotation[2]*b.translation.z + a.translation.x,
                     a.rotation[3]*b.translation.x + a.rotation[4]*b.translation.y + a.rotation[5]*b.translation.z + a.translation.y,
                     a.rotation[6]*b.translation.x + a.rotation[7]*b.translation.y + a.rotation[8]*b.translation.z + a.translation.z};
    return r;
}
inline JointRigidTransform inverse(const JointRigidTransform& a) {
    JointRigidTransform r;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.rotation[i*3+j] = a.rotation[j*3+i];
    const Vec3 t = a.translation;
    r.translation = {-(r.rotation[0]*t.x+r.rotation[1]*t.y+r.rotation[2]*t.z),
                     -(r.rotation[3]*t.x+r.rotation[4]*t.y+r.rotation[5]*t.z),
                     -(r.rotation[6]*t.x+r.rotation[7]*t.y+r.rotation[8]*t.z)};
    return r;
}
inline bool rigid(const JointRigidTransform& t) {
    if (!finite(t.translation)) return false;
    for (double n : t.rotation) if (!std::isfinite(n)) return false;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        double v = 0;
        for (int k = 0; k < 3; ++k) v += t.rotation[k*3+i]*t.rotation[k*3+j];
        if (std::abs(v - (i == j ? 1.0 : 0.0)) > 1e-7) return false;
    }
    const auto& m = t.rotation;
    const double det = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return std::abs(det - 1.0) < 1e-7;
}
inline JointRigidTransform rotate(Vec3 axis, double q) {
    const double c = std::cos(q), s = std::sin(q), d = 1-c;
    const double x = axis.x, y = axis.y, z = axis.z;
    JointRigidTransform r;
    r.rotation = {c+x*x*d, x*y*d-z*s, x*z*d+y*s,
                  y*x*d+z*s, c+y*y*d, y*z*d-x*s,
                  z*x*d-y*s, z*y*d+x*s, c+z*z*d};
    return r;
}
} // namespace joint_detail

// Ordered rotational coordinates, like the specified OpenSim
// CustomJoint subset. This does not support coupled limits, translations,
// cartilage deformation, contact or closed-loop constraints. A fourth axis
// may carry a fixed source-to-reference alignment, not an extra anatomical DOF.
class CalibratedRotationalJoint {
public:
    static std::optional<CalibratedRotationalJoint> create(
        JointRigidTransform parentFrame, JointRigidTransform childFrame,
        std::vector<JointRotationalAxis> axes) {
        if (!joint_detail::rigid(parentFrame) || !joint_detail::rigid(childFrame) ||
            axes.empty() || axes.size() > 4) return std::nullopt;
        for (auto& axis : axes) {
            if (!joint_detail::finite(axis.direction) || !std::isfinite(axis.minimum) ||
                !std::isfinite(axis.maximum) || axis.minimum > axis.maximum) return std::nullopt;
            const double size = length(axis.direction);
            if (!std::isfinite(size) || size < 1e-9) return std::nullopt;
            axis.direction = axis.direction / size;
        }
        return CalibratedRotationalJoint(parentFrame, childFrame, std::move(axes));
    }
    // Apply all coordinates atomically; finite out-of-range values are clamped.
    // Invalid input leaves the previous valid pose intact.
    bool setAngles(const std::vector<double>& requested) {
        if (requested.size() != axes_.size()) return false;
        for (double q : requested) if (!std::isfinite(q)) return false;
        for (std::size_t i = 0; i < requested.size(); ++i)
            angles_[i] = std::clamp(requested[i], axes_[i].minimum, axes_[i].maximum);
        return true;
    }
    const std::vector<double>& angles() const { return angles_; }
    const std::vector<JointRotationalAxis>& axes() const { return axes_; }
    const JointRigidTransform& parentFrame() const { return parentFrame_; }
    JointRigidTransform childInParent() const {
        JointRigidTransform motion;
        for (std::size_t i = 0; i < axes_.size(); ++i)
            motion = joint_detail::compose(motion, joint_detail::rotate(axes_[i].direction, angles_[i]));
        return joint_detail::compose(joint_detail::compose(parentFrame_, motion), childFrameInverse_);
    }
private:
    CalibratedRotationalJoint(JointRigidTransform parent, JointRigidTransform child,
                              std::vector<JointRotationalAxis> axes)
        : parentFrame_(parent), childFrameInverse_(joint_detail::inverse(child)), axes_(std::move(axes)) {
        for (const auto& axis : axes_) angles_.push_back(std::clamp(0.0, axis.minimum, axis.maximum));
    }
    JointRigidTransform parentFrame_, childFrameInverse_;
    std::vector<JointRotationalAxis> axes_;
    std::vector<double> angles_;
};

} // namespace gtc::native
