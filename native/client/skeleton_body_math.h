// The rigid transforms and the small measurements the body model is built from:
// composing and rotating, the bounds of a point set, which group hangs from
// which, and where a joint nearly is. They were in an anonymous namespace in
// skeleton_body.cpp; the reference rig is now its own file and needs them, so
// they are inline here.

#ifndef GTC_CLIENT_SKELETON_BODY_MATH_H
#define GTC_CLIENT_SKELETON_BODY_MATH_H

#include "client/skeleton_body.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native::skeleton_detail {

struct Transform {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 t;
    Vec3 apply(Vec3 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z + t.x,
                m[3] * p.x + m[4] * p.y + m[5] * p.z + t.y,
                m[6] * p.x + m[7] * p.y + m[8] * p.z + t.z};
    }
};

inline Transform compose(const Transform& outer, const Transform& inner) {
    Transform out;
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 3; ++column) {
        out.m[row * 3 + column] = outer.m[row * 3 + 0] * inner.m[0 * 3 + column] +
                                  outer.m[row * 3 + 1] * inner.m[1 * 3 + column] +
                                  outer.m[row * 3 + 2] * inner.m[2 * 3 + column];
    }
    out.t = outer.apply(inner.t);
    return out;
}
// Ry * Rx * Rz: yaw the body, bend it forward, then lean it sideways. The two
// products are done in that order and each inner sum runs over the *middle*
// matrix's column index -- indexing it by the outer row instead collapses the
// result to rank one, which flattens every bone onto a line.

inline Transform rotation(double pitch, double yaw, double roll) {
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp};
    const double ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    const double rz[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
    double middle[9];
    Transform out;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double sum = 0;
            for (int k = 0; k < 3; ++k) sum += rx[row * 3 + k] * rz[k * 3 + column];
            middle[row * 3 + column] = sum;
        }
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            double sum = 0;
            for (int k = 0; k < 3; ++k) sum += ry[row * 3 + k] * middle[k * 3 + column];
            out.m[row * 3 + column] = sum;
        }
    }
    return out;
}

inline Vec3 minimum(const std::vector<Vec3>& points) {
    Vec3 low{1e9, 1e9, 1e9};
    for (const Vec3& point : points) {
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y); low.z = std::min(low.z, point.z);
    }
    return low;
}

inline Vec3 maximum(const std::vector<Vec3>& points) {
    Vec3 high{-1e9, -1e9, -1e9};
    for (const Vec3& point : points) {
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y); high.z = std::max(high.z, point.z);
    }
    return high;
}

inline Vec3 centroid(const std::vector<Vec3>& points) {
    Vec3 sum;
    for (const Vec3& point : points) sum += point;
    return sum / std::max<std::size_t>(1, points.size());
}
// Which skeleton group hangs from which. Anything not listed is a root.

inline const char* parentOf(const std::string& name) {
    if (name == "spine_lumbar") return "pelvis";
    if (name == "spine_thoracic") return "spine_lumbar";
    if (name == "spine_cervical") return "spine_thoracic";
    if (name == "skull") return "spine_cervical";
    if (name == "mandible" || name == "teeth") return "skull";
    if (name == "ribs_l" || name == "ribs_r" || name == "sternum") return "spine_thoracic";
    if (name == "scapula_l" || name == "scapula_r") return "spine_thoracic";
    if (name == "clavicle_l" || name == "clavicle_r") return "spine_thoracic";
    if (name == "humerus_l") return "clavicle_l";
    if (name == "humerus_r") return "clavicle_r";
    if (name == "forearm_l") return "humerus_l";
    if (name == "forearm_r") return "humerus_r";
    if (name == "hand_l") return "forearm_l";
    if (name == "hand_r") return "forearm_r";
    if (name == "femur_l" || name == "femur_r") return "pelvis";
    if (name == "patella_l") return "femur_l";
    if (name == "patella_r") return "femur_r";
    if (name == "lower_leg_l") return "femur_l";
    if (name == "lower_leg_r") return "femur_r";
    if (name == "foot_l") return "lower_leg_l";
    if (name == "foot_r") return "lower_leg_r";
    return "";
}
// A coarse fallback for group pivots, not a medically measured joint centre.
// The C room replaces the two hip pivots with fitted femoral-head estimates;
// the remaining pivots still need subject-specific anatomical calibration.

inline Vec3 jointNear(const std::vector<Vec3>& points, Vec3 toward) {
    double closest = 1e9;
    for (const Vec3& point : points) closest = std::min(closest, length(point - toward));
    std::vector<Vec3> cap;
    for (const Vec3& point : points) {
        if (length(point - toward) < closest + 0.022) cap.push_back(point);
    }
    return cap.empty() ? centroid(points) : centroid(cap);
}

}  // namespace gtc::native::skeleton_detail

#endif
