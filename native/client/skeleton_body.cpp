#include "client/skeleton_body.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace gtc::native {
namespace {

struct Transform {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 t;
    Vec3 apply(Vec3 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z + t.x,
                m[3] * p.x + m[4] * p.y + m[5] * p.z + t.y,
                m[6] * p.x + m[7] * p.y + m[8] * p.z + t.z};
    }
};

Transform compose(const Transform& outer, const Transform& inner) {
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
Transform rotation(double pitch, double yaw, double roll) {
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

Vec3 minimum(const std::vector<Vec3>& points) {
    Vec3 low{1e9, 1e9, 1e9};
    for (const Vec3& point : points) {
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y); low.z = std::min(low.z, point.z);
    }
    return low;
}

Vec3 maximum(const std::vector<Vec3>& points) {
    Vec3 high{-1e9, -1e9, -1e9};
    for (const Vec3& point : points) {
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y); high.z = std::max(high.z, point.z);
    }
    return high;
}

Vec3 centroid(const std::vector<Vec3>& points) {
    Vec3 sum;
    for (const Vec3& point : points) sum += point;
    return sum / std::max<std::size_t>(1, points.size());
}

// Which skeleton group hangs from which. Anything not listed is a root.
const char* parentOf(const std::string& name) {
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

// The joint a bone turns about: the cap of that bone which faces its parent.
// Taking the centroid of the vertices nearest the parent -- rather than the
// nearest vertex, which is noise, or the bbox corner, which drifts with how the
// bone is oriented -- puts the hip pivot on the femoral head and the elbow
// pivot on the trochlea, which is what makes a bend look like a bend.
Vec3 jointNear(const std::vector<Vec3>& points, Vec3 toward) {
    double closest = 1e9;
    for (const Vec3& point : points) closest = std::min(closest, length(point - toward));
    std::vector<Vec3> cap;
    for (const Vec3& point : points) {
        if (length(point - toward) < closest + 0.022) cap.push_back(point);
    }
    return cap.empty() ? centroid(points) : centroid(cap);
}

} // namespace

bool SkeletonModel::load(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Cannot open skeleton " + path;
        return false;
    }
    bones_.clear();
    std::vector<int> base;   // index of each group's first vertex in the file
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("o ", 0) == 0) {
            SkeletonBone bone;
            bone.name = line.substr(2);
            while (!bone.name.empty() && (bone.name.back() == '\r' || bone.name.back() == ' ')) bone.name.pop_back();
            bones_.push_back(std::move(bone));
            int first = 0;
            for (std::size_t index = 0; index + 1 < bones_.size(); ++index) {
                first += static_cast<int>(bones_[index].points.size());
            }
            base.push_back(first);
        } else if (line.rfind("v ", 0) == 0 && !bones_.empty()) {
            std::istringstream stream(line.substr(2));
            Vec3 point;
            stream >> point.x >> point.y >> point.z;
            bones_.back().points.push_back(point);
        } else if (line.rfind("f ", 0) == 0 && !bones_.empty()) {
            std::istringstream stream(line.substr(2));
            std::array<int, 3> face{};
            for (int corner = 0; corner < 3; ++corner) {
                std::string token;
                stream >> token;
                const std::size_t slash = token.find('/');
                if (slash != std::string::npos) token = token.substr(0, slash);
                face[corner] = token.empty() ? -1 : std::atoi(token.c_str()) - 1 - base.back();
            }
            bones_.back().triangles.push_back(face);
        }
    }
    if (bones_.empty()) {
        error = "Skeleton " + path + " has no bone groups";
        return false;
    }
    // Turn the flat list into a hierarchy and give every bone its joint.
    for (SkeletonBone& bone : bones_) {
        const std::string parent = parentOf(bone.name);
        bone.parent = -1;
        for (std::size_t index = 0; index < bones_.size(); ++index) {
            if (bones_[index].name == parent) bone.parent = static_cast<int>(index);
        }
    }
    for (SkeletonBone& bone : bones_) {
        bone.pivot = bone.parent >= 0 ? jointNear(bone.points, centroid(bones_[static_cast<std::size_t>(bone.parent)].points))
                                      : centroid(bone.points);
    }
    std::vector<Vec3> all;
    for (const SkeletonBone& bone : bones_) all.insert(all.end(), bone.points.begin(), bone.points.end());
    height_ = maximum(all).y - minimum(all).y;
    name_ = path.substr(path.find_last_of('/') + 1);
    return true;
}

int SkeletonModel::bone(const std::string& name) const {
    for (std::size_t index = 0; index < bones_.size(); ++index) {
        if (bones_[index].name == name) return static_cast<int>(index);
    }
    return -1;
}

void SkeletonModel::poseIdle(double seconds, double phase) {
    const double t = seconds * 0.8 + phase;
    const auto set = [&](const char* name, double pitch, double yaw, double roll) {
        const int index = bone(name);
        if (index < 0) return;
        SkeletonBone& target = bones_[static_cast<std::size_t>(index)];
        target.pitch = pitch; target.yaw = yaw; target.roll = roll;
    };
    // Weight shifts slowest, the head leads, the jaw trails everything.
    set("pelvis", 0.0, 0.05 * std::sin(t * 0.6), 0.03 * std::sin(t * 0.8 + 1.0));
    set("spine_lumbar", 0.01 * std::sin(t * 0.5), 0.02 * std::sin(t * 0.6 + 0.4), -0.02 * std::sin(t * 0.8 + 1.0));
    set("spine_thoracic", 0.02 * std::sin(t * 0.5 + 0.3), 0.02 * std::sin(t * 0.6 + 0.8), 0.02 * std::sin(t * 0.8 + 1.0));
    set("spine_cervical", 0.01 * std::sin(t * 0.4), 0.10 * std::sin(t * 0.45 + 0.7), 0.0);
    set("skull", 0.05 * std::sin(t * 0.3 + 0.2), 0.16 * std::sin(t * 0.45 + 1.2), 0.03 * std::sin(t * 0.35));
    set("mandible", 0.02 + 0.02 * std::sin(t * 0.9), 0.0, 0.0);
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? 1.0 : -1.0;
        const std::string suffix = side == 0 ? "_l" : "_r";
        const double offset = side == 0 ? 0.0 : 0.6;
        set(("clavicle" + suffix).c_str(), 0.01 * std::sin(t * 0.7 + offset), 0.0, 0.0);
        set(("humerus" + suffix).c_str(), 0.03 * std::sin(t * 0.7 + offset), 0.0, sign * 0.02 * std::sin(t * 0.6));
        set(("forearm" + suffix).c_str(), 0.05 + 0.03 * std::sin(t * 0.7 + offset + 0.5), 0.0, 0.0);
        set(("femur" + suffix).c_str(), 0.01 * std::sin(t * 0.6 + offset), 0.0, 0.0);
        set(("lower_leg" + suffix).c_str(), 0.01 * std::sin(t * 0.6 + offset), 0.0, 0.0);
    }
}

void SkeletonModel::draw(Rasterizer& rasterizer, const Camera& camera, Vec3 origin, double yaw, int onlyBone) const {
    if (onlyBone >= 0) {
        drawBones(rasterizer, camera, origin, yaw, static_cast<std::size_t>(onlyBone),
                  static_cast<std::size_t>(onlyBone) + 1);
        return;
    }
    drawBones(rasterizer, camera, origin, yaw, 0, bones_.size());
}

void SkeletonModel::drawBones(Rasterizer& rasterizer, const Camera& camera, Vec3 origin, double yaw,
                              std::size_t begin, std::size_t end) const {
    std::vector<Transform> pose(bones_.size());
    std::vector<char> solved(bones_.size(), 0);
    Transform place;
    const double c = std::cos(yaw), s = std::sin(yaw);
    place.m[0] = c; place.m[2] = s; place.m[6] = -s; place.m[8] = c;
    place.t = origin;
    // The source lists bones child-first (skull before neck, femur before
    // pelvis), so a single forward pass would pose every child against an
    // identity parent. Solve the chain depth-first instead: a bone is only
    // placed once its parent is.
    const std::function<Transform(std::size_t)> solve = [&](std::size_t index) -> Transform {
        if (solved[index]) return pose[index];
        const SkeletonBone& bone = bones_[index];
        const Transform swing = rotation(bone.pitch, bone.yaw, bone.roll);
        Transform local;
        local.m[0] = swing.m[0]; local.m[1] = swing.m[1]; local.m[2] = swing.m[2];
        local.m[3] = swing.m[3]; local.m[4] = swing.m[4]; local.m[5] = swing.m[5];
        local.m[6] = swing.m[6]; local.m[7] = swing.m[7]; local.m[8] = swing.m[8];
        local.t = bone.pivot - swing.apply(bone.pivot);
        solved[index] = 1;
        pose[index] = compose(bone.parent >= 0 ? solve(static_cast<std::size_t>(bone.parent)) : place, local);
        return pose[index];
    };
    for (std::size_t index = 0; index < bones_.size(); ++index) solve(index);
    Material boneMaterial;
    boneMaterial.color = colorHex("#e3d9bf");
    boneMaterial.specular = 0.12;
    end = std::min(end, bones_.size());
    for (std::size_t index = begin; index < end; ++index) {
        const SkeletonBone& bone = bones_[index];
        std::vector<Vec3> world;
        world.reserve(bone.points.size());
        for (const Vec3& point : bone.points) world.push_back(pose[index].apply(point));
        for (const auto& face : bone.triangles) {
            if (face[0] < 0 || face[1] < 0 || face[2] < 0) continue;
            if (face[0] >= static_cast<int>(world.size()) || face[1] >= static_cast<int>(world.size()) ||
                face[2] >= static_cast<int>(world.size())) continue;
            const Vec3 a = world[static_cast<std::size_t>(face[0])];
            const Vec3 b = world[static_cast<std::size_t>(face[1])];
            const Vec3 c = world[static_cast<std::size_t>(face[2])];
            const Vec3 normal = normalized(cross(b - a, c - a));
            // The points are already where they belong when the backend draws in
            // world space, which is the case for both GPU backends.
            rasterizer.worldTriangle(a, b, c, normal, boneMaterial);
        }
    }
}

} // namespace gtc::native
