#include "client/skeleton_body.h"
#include "client/skeleton_finger_frame.h"
#include "client/skeleton_leg_frame.h"
#include "client/skeleton_spine_frame.h"
#include "client/skeleton_toe_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <set>

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

// A coarse fallback for group pivots, not a medically measured joint centre.
// The C room replaces the two hip pivots with fitted femoral-head estimates;
// the remaining pivots still need subject-specific anatomical calibration.
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
    sourceStructures_.clear();
    sourceLinks_.clear();
    sourceRig_.reset();
    sourceBodies_.clear();
    neutralAlignments_.clear();
    hasKneeAxes_ = false;
    kneeAxes_ = {};
    hasElbowAxes_ = false;
    elbowAxes_ = {};
    ++sourcePoseRevision_;
    // OBJ source labels are nested inside the larger runtime `o` groups.
    // Record their exact local ranges without duplicating the mesh or changing
    // the existing static GPU batches.
    const auto finishStructure = [&] {
        if (sourceStructures_.empty()) return;
        SkeletonSourceStructure& source = sourceStructures_.back();
        const SkeletonBone& group = bones_[source.group];
        source.pointCount = group.points.size() - source.firstPoint;
        source.triangleCount = group.triangles.size() - source.firstTriangle;
    };
    bool inStructure = false;
    std::vector<int> base;   // index of each group's first vertex in the file
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("o ", 0) == 0) {
            if (inStructure) finishStructure();
            inStructure = false;
            SkeletonBone bone;
            bone.name = line.substr(2);
            while (!bone.name.empty() && (bone.name.back() == '\r' || bone.name.back() == ' ')) bone.name.pop_back();
            bones_.push_back(std::move(bone));
            int first = 0;
            for (std::size_t index = 0; index + 1 < bones_.size(); ++index) {
                first += static_cast<int>(bones_[index].points.size());
            }
            base.push_back(first);
        } else if (line.rfind("g ", 0) == 0 && !bones_.empty()) {
            if (inStructure) finishStructure();
            SkeletonSourceStructure source;
            source.name = line.substr(2);
            while (!source.name.empty() && (source.name.back() == '\r' || source.name.back() == ' ')) source.name.pop_back();
            source.group = bones_.size() - 1;
            source.firstPoint = bones_.back().points.size();
            source.firstTriangle = bones_.back().triangles.size();
            sourceStructures_.push_back(std::move(source));
            inStructure = true;
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
    if (inStructure) finishStructure();
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

bool SkeletonModel::loadSourceLinks(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Cannot open skeleton source links " + path; return false; }
    std::vector<SkeletonSourceLink> links;
    std::set<std::pair<int, int>> seen;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string kind, first, second, mobility, extra;
        row >> kind >> first >> second >> mobility;
        if (!row || (row >> extra) || (kind != "joint" && kind != "cartilage")) {
            error = "Invalid skeleton source link in " + path; return false;
        }
        const int a = sourceStructure(first), b = sourceStructure(second);
        if (a < 0 || b < 0 || a == b || !seen.insert(std::minmax(a,b)).second) {
            error = "Unknown or duplicate skeleton source link in " + path; return false;
        }
        links.push_back({static_cast<std::size_t>(a), static_cast<std::size_t>(b), kind, mobility});
    }
    if (links.empty()) { error = "No skeleton source links in " + path; return false; }
    sourceLinks_ = std::move(links);
    return true;
}

bool SkeletonModel::loadHipCenters(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Cannot open skeleton hip fits " + path; return false; }
    std::array<Vec3, 2> centers{};
    std::array<bool, 2> present{};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string side, extra;
        Vec3 center;
        double radius = 0;
        row >> side >> center.x >> center.y >> center.z >> radius;
        if (!row || (row >> extra) || (side != "L" && side != "R") ||
            !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
            !std::isfinite(radius) || radius <= 0.015 || radius >= 0.035) {
            error = "Invalid skeleton hip fit in " + path; return false;
        }
        const int which = side == "L" ? 0 : 1;
        const int group = bone(which == 0 ? "femur_l" : "femur_r");
        if (present[which] || group < 0 || bones_[group].points.empty()) {
            error = "Missing or duplicate femur for hip fit in " + path; return false;
        }
        const auto& points = bones_[group].points;
        const Vec3 low = minimum(points), high = maximum(points);
        if (center.x < low.x-0.01 || center.x > high.x+0.01 ||
            center.y < high.y-0.1 || center.y > high.y+0.01 ||
            center.z < low.z-0.01 || center.z > high.z+0.01) {
            error = "Hip fit is not near its proximal femur in " + path; return false;
        }
        double best = 1e9;
        for (const auto& point : points) best = std::min(best, std::abs(length(point-center)-radius));
        if (best > 0.003) { error = "Hip fit does not match femur surface in " + path; return false; }
        centers[which] = center;
        present[which] = true;
    }
    if (!present[0] || !present[1]) { error = "Incomplete skeleton hip fits in " + path; return false; }
    bones_[static_cast<std::size_t>(bone("femur_l"))].pivot = centers[0];
    bones_[static_cast<std::size_t>(bone("femur_r"))].pivot = centers[1];
    ++sourcePoseRevision_;
    return true;
}

bool SkeletonModel::loadShoulderCenters(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Cannot open skeleton shoulder fits " + path; return false; }
    std::array<Vec3,2> centers{}; std::array<bool,2> present{};
    std::string line;
    while (std::getline(input,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line); std::string side,extra; Vec3 c; double radius;
        row >> side >> c.x >> c.y >> c.z >> radius;
        if (!row || (row >> extra) || (side != "L" && side != "R") ||
            !joint_detail::finite(c) || !std::isfinite(radius) || radius <= 0.015 || radius >= 0.035) {
            error = "Invalid shoulder fit in " + path; return false;
        }
        const int which = side == "L" ? 0 : 1;
        const int group = bone(which == 0 ? "humerus_l" : "humerus_r");
        if (present[which] || group < 0 || bones_[group].points.empty()) {
            error = "Missing or duplicate humerus in " + path; return false;
        }
        const auto& p = bones_[group].points; const Vec3 lo=minimum(p), hi=maximum(p);
        if (c.x < lo.x-0.01 || c.x > hi.x+0.01 || c.y < lo.y-0.01 || c.y > hi.y+0.01 ||
            c.z < lo.z-0.01 || c.z > hi.z+0.01) {
            error = "Shoulder fit outside humerus in " + path; return false;
        }
        double best=1e9; for (Vec3 v : p) best=std::min(best,std::abs(length(v-c)-radius));
        if (best > 0.003) { error = "Shoulder fit does not match surface in " + path; return false; }
        centers[which]=c; present[which]=true;
    }
    if (!present[0] || !present[1]) { error = "Incomplete shoulder fits in " + path; return false; }
    bones_[static_cast<std::size_t>(bone("humerus_l"))].pivot=centers[0];
    bones_[static_cast<std::size_t>(bone("humerus_r"))].pivot=centers[1];
    ++sourcePoseRevision_;
    return true;
}

bool SkeletonModel::loadElbowAxes(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Cannot open skeleton elbow fits " + path; return false; }
    std::array<SkeletonElbowAxis,2> fits{}; std::array<bool,2> present{};
    std::string line;
    while (std::getline(input,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream row(line); std::string side,extra; SkeletonElbowAxis fit;
        row >> side >> fit.origin.x >> fit.origin.y >> fit.origin.z
            >> fit.direction.x >> fit.direction.y >> fit.direction.z >> fit.sourceAngleDegrees;
        if (!row || (row >> extra) || (side!="L" && side!="R") ||
            !joint_detail::finite(fit.origin) || !joint_detail::finite(fit.direction) ||
            std::abs(length(fit.direction)-1)>1e-6 || !std::isfinite(fit.sourceAngleDegrees) ||
            fit.sourceAngleDegrees<0 || fit.sourceAngleDegrees>150) {
            error="Invalid elbow fit in " + path; return false;
        }
        const int which=side=="L" ? 0 : 1;
        const int h=bone(which==0 ? "humerus_l" : "humerus_r");
        const int f=bone(which==0 ? "forearm_l" : "forearm_r");
        if (present[which] || h<0 || f<0 || bones_[h].points.empty() || bones_[f].points.empty()) {
            error="Missing or duplicate elbow bones in " + path; return false;
        }
        for (int group : {h,f}) {
            double nearest=1e9;
            for (Vec3 p : bones_[group].points) nearest=std::min(nearest,length(p-fit.origin));
            if (nearest>0.04) { error="Elbow fit is not near its bones in " + path; return false; }
        }
        fits[which]=fit; present[which]=true;
    }
    if (!present[0] || !present[1]) { error="Incomplete elbow fits in " + path; return false; }
    elbowAxes_=fits; hasElbowAxes_=true; ++sourcePoseRevision_;
    return true;
}

bool SkeletonModel::loadKneeAxes(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Cannot open skeleton knee fits " + path; return false; }
    std::array<SkeletonKneeAxis, 2> fits{};
    std::array<bool, 2> present{};
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string side, extra;
        SkeletonKneeAxis fit;
        row >> side >> fit.origin.x >> fit.origin.y >> fit.origin.z
            >> fit.medialDirection.x >> fit.medialDirection.y >> fit.medialDirection.z >> fit.width;
        if (!row || (row >> extra) || (side != "L" && side != "R") ||
            !joint_detail::finite(fit.origin) || !joint_detail::finite(fit.medialDirection) ||
            !std::isfinite(fit.width) || fit.width <= 0.045 || fit.width >= 0.11 ||
            std::abs(length(fit.medialDirection)-1.0) > 1e-6) {
            error = "Invalid skeleton knee fit in " + path; return false;
        }
        const int which = side == "L" ? 0 : 1;
        const int femur = bone(which == 0 ? "femur_l" : "femur_r");
        const int shin = bone(which == 0 ? "lower_leg_l" : "lower_leg_r");
        if (present[which] || femur < 0 || shin < 0 || bones_[femur].points.empty()) {
            error = "Missing or duplicate femur or tibia in " + path; return false;
        }
        const auto& points = bones_[femur].points;
        const Vec3 low = minimum(points), high = maximum(points);
        if (fit.origin.y < low.y-0.01 || fit.origin.y > low.y+0.06 ||
            fit.origin.x < low.x-0.01 || fit.origin.x > high.x+0.01 ||
            fit.origin.z < low.z-0.01 || fit.origin.z > high.z+0.01) {
            error = "Knee fit is not near its distal femur in " + path; return false;
        }
        for (double sign : {-1.0, 1.0}) {
            const Vec3 landmark = fit.origin + fit.medialDirection * (sign * fit.width * 0.5);
            double nearest = 1e9;
            for (const auto& point : points) nearest = std::min(nearest, length(point-landmark));
            if (nearest > 0.005) { error = "Knee fit does not match femur surface in " + path; return false; }
        }
        fits[which] = fit;
        present[which] = true;
    }
    if (!present[0] || !present[1]) { error = "Incomplete skeleton knee fits in " + path; return false; }
    kneeAxes_ = fits;
    hasKneeAxes_ = true;
    for (int side = 0; side < 2; ++side) {
        auto& shin = bones_[static_cast<std::size_t>(bone(side == 0 ? "lower_leg_l" : "lower_leg_r"))];
        shin.pivot = fits[side].origin;
        // Facing-forward convention: positive bend rotates the shin posteriorly
        // on both sides. Actual physiological neutral and ROM are not known.
        shin.fittedBendAxis = fits[side].medialDirection * (side == 0 ? -1.0 : 1.0);
        shin.hasFittedBendAxis = true;
    }
    ++sourcePoseRevision_;
    return true;
}

int SkeletonModel::sourceStructure(const std::string& name) const {
    for (std::size_t index = 0; index < sourceStructures_.size(); ++index)
        if (sourceStructures_[index].name == name) return static_cast<int>(index);
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

// The chain solve, shared by everything that needs the pose: drawing posed
// vertices, or handing the backend one matrix per bone.
static std::vector<Transform> solvePose(const std::vector<SkeletonBone>& bones, Vec3 origin, double yaw) {
    std::vector<Transform> pose(bones.size());
    std::vector<char> solved(bones.size(), 0);
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
        const SkeletonBone& bone = bones[index];
        Transform swing = rotation(bone.pitch, bone.yaw, bone.roll);
        if (bone.hasFittedBendAxis) {
            const auto hinge = joint_detail::rotate(bone.fittedBendAxis, bone.pitch);
            Transform bend;
            for (int i = 0; i < 9; ++i) bend.m[i] = hinge.rotation[i];
            swing = compose(rotation(0, bone.yaw, 0), compose(bend, rotation(0, 0, bone.roll)));
        }
        Transform local;
        local.m[0] = swing.m[0]; local.m[1] = swing.m[1]; local.m[2] = swing.m[2];
        local.m[3] = swing.m[3]; local.m[4] = swing.m[4]; local.m[5] = swing.m[5];
        local.m[6] = swing.m[6]; local.m[7] = swing.m[7]; local.m[8] = swing.m[8];
        local.t = bone.pivot - swing.apply(bone.pivot);
        solved[index] = 1;
        pose[index] = compose(bone.parent >= 0 ? solve(static_cast<std::size_t>(bone.parent)) : place, local);
        return pose[index];
    };
    for (std::size_t index = 0; index < bones.size(); ++index) solve(index);
    return pose;
}

std::vector<std::array<double, 16>> SkeletonModel::boneMatrices(Vec3 origin, double yaw) const {
    const std::vector<Transform> pose = solvePose(bones_, origin, yaw);
    std::vector<std::array<double, 16>> matrices(pose.size());
    for (std::size_t index = 0; index < pose.size(); ++index) {
        const Transform& transform = pose[index];
        // Column-major, which is what the shader and the push constant expect:
        // element [column * 4 + row].
        std::array<double, 16>& out = matrices[index];
        out[0] = transform.m[0]; out[1] = transform.m[3]; out[2] = transform.m[6]; out[3] = 0;
        out[4] = transform.m[1]; out[5] = transform.m[4]; out[6] = transform.m[7]; out[7] = 0;
        out[8] = transform.m[2]; out[9] = transform.m[5]; out[10] = transform.m[8]; out[11] = 0;
        out[12] = transform.t.x; out[13] = transform.t.y; out[14] = transform.t.z; out[15] = 1;
    }
    return matrices;
}

void SkeletonModel::buildRestGeometry(Rasterizer& rasterizer, const Camera& camera, int firstObjectId) const {
    Material boneMaterial;
    boneMaterial.color = colorHex("#e3d9bf");
    boneMaterial.specular = 0.12;
    for (std::size_t index = 0; index < bones_.size(); ++index) {
        const SkeletonBone& bone = bones_[index];
        rasterizer.setObject(static_cast<std::uint32_t>(firstObjectId + static_cast<int>(index)),
                             {0.0, 0.0}, 0.0F);
        rasterizer.mesh(bone.points, bone.triangles, boneMaterial);
    }
}

bool SkeletonModel::setSourceRig(AnatomicalKinematicTree rig,
                                 const std::vector<std::size_t>& sourceBodies, std::string& error) {
    if (sourceStructures_.empty() || sourceBodies.size() != sourceStructures_.size() ||
        !rig.hasBindPose() || !rig.evaluate()) {
        error = "Source rig must map every labelled structure and have a bind pose"; return false;
    }
    std::vector<bool> seen(rig.size());
    for (const auto body : sourceBodies) {
        if (body >= rig.size() || seen[body]) {
            error = "Source rig body map must be in range and unique"; return false;
        }
        seen[body] = true;
        const auto& m = rig.skinTransforms()[body];
        const JointRigidTransform identity;
        for (int i = 0; i < 9; ++i) if (std::abs(m.rotation[i]-identity.rotation[i]) > 1e-7) {
            error = "Source rig must be attached at its bind pose"; return false;
        }
        if (length(m.translation) > 1e-7) {
            error = "Source rig must be attached at its bind pose"; return false;
        }
    }
    sourceRig_ = std::move(rig);
    sourceBodies_ = sourceBodies;
    referenceBindPose_.clear();
    neutralAlignments_.clear();
    ++sourcePoseRevision_;
    return true;
}

bool SkeletonModel::installReferenceRig(std::string& error) {
    if (sourceLinks_.empty() || sourceStructures_.empty()) {
        error = "Cannot install a source rig without anatomical links"; return false;
    }
    const int root = sourceStructure("Sacrum");
    if (root < 0) { error = "Skeleton is missing sacrum"; return false; }
    const std::size_t count = sourceStructures_.size();
    const std::size_t missing = static_cast<std::size_t>(-1);
    std::vector<std::size_t> mapping(count, missing), queue;
    std::vector<bool> visited(count);
    FingerReferenceFrames fingerFrames(count);
    std::vector<double> neutralAlignments(count+1,-1.0);
    AnatomicalKinematicTree rig;
    std::vector<AnatomicalKinematicTree::PoseUpdate> bindPose;
    auto rootBody = rig.addFixed(0, {});
    if (!rootBody) { error = "Unable to add source root"; return false; }
    mapping[static_cast<std::size_t>(root)] = *rootBody;
    visited[static_cast<std::size_t>(root)] = true;
    queue.push_back(static_cast<std::size_t>(root));
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
    const auto suffix = [](const std::string& name) -> int {
        return name.size() >= 2 && name.substr(name.size()-2) == "_L" ? 0 : 1;
    };
    constexpr double toRadians = 3.14159265358979323846 / 180.0;
    double spineTurn=0;
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
            Vec3 alignmentAxis; double alignmentAngle=0; bool alignToNeutral=false;
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
                Vec3 pivot = contact(parent, child);
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
                    axis = {1,0,0};
                    if (link.mobility=="intervertebral" && parentName=="Sacrum" && childName=="L5") {
                        const int neck=sourceStructure("C7");
                        if (neck<0) { error="Missing spine reference"; return false; }
                        spineTurn=uprightSpineAngle(center(parent),pivot,center(neck));
                        alignmentAxis={0,0,spineTurn<0 ? -1.0 : 1.0}; alignmentAngle=std::abs(spineTurn); alignToNeutral=true;
                    }
                } else if (link.mobility == "atlanto_axial") {
                    axis = {0,1,0};
                    const int head=sourceStructure("Head"), jaw=sourceStructure("Jaw");
                    if (head<0 || jaw<0) { error="Missing head reference"; return false; }
                    const double yaw=forwardHeadYaw(center(jaw)-center(head),spineTurn);
                    alignmentAxis={0,yaw<0 ? -1.0 : 1.0,0}; alignmentAngle=std::abs(yaw); alignToNeutral=true;
                } else if (link.mobility == "sternoclavicular" || link.mobility == "acromioclavicular") {
                    axis = {0,0,side == 0 ? 1.0 : -1.0};
                } else if (link.mobility == "shoulder_ball_socket") {
                    // The primary shoulder control is abduction: align the
                    // actual upper-arm shaft toward its side, not a world X
                    // rotation that assumes the subject began in neutral.
                    const Vec3 worldTarget = side == 0 ? Vec3{1,0,0} : Vec3{-1,0,0};
                    const Vec3 target=frameVector(joint_detail::rotate({0,0,1},-spineTurn),worldTarget);
                    axis = cross(down,target);
                    current = (3.14159265358979323846/2) -
                        std::acos(std::clamp(dot(down,target),-1.0,1.0));
                } else if (link.mobility == "hip_ball_socket" && hasKneeAxes_) {
                    // The same sagittal flexion plane on both sides of the pelvis.
                    axis={-1,0,0};
                    const auto leg=sourceLegFrame(*this,side); if (!leg) { error="Missing leg frame"; return false; }
                    alignmentAxis=leg->hipAlignment; alignmentAngle=leg->hipAngle; alignToNeutral=true;
                } else if (link.mobility == "knee_modified_hinge" && hasKneeAxes_) {
                    axis = kneeAxes_[side].medialDirection * (side == 0 ? -1.0 : 1.0);
                    up = normalized(up-axis*dot(up,axis));
                    const Vec3 other = normalized((down*-1.0)-axis*dot(down*-1.0,axis));
                    current = std::atan2(dot(axis,cross(up,other)),dot(up,other));
                    const auto leg=sourceLegFrame(*this,side); if (!leg) { error="Missing leg frame"; return false; }
                    alignmentAxis=leg->kneeAlignment; alignmentAngle=leg->kneeAngle; alignToNeutral=true;
                } else if (link.mobility == "ankle_hinge") {
                    // Positive plantarflexion moves the forefoot away from
                    // the shin; 0 is perpendicular to the tibial shaft.
                    axis=cross(up,down);
                    current=std::asin(std::clamp(-dot(up,down),-1.0,1.0));
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
                    const auto frame=sourceToeFrame(*this,fingerFrames,parent,child,side,pivot);
                    if (!frame) { error="Missing toe reference frame"; return false; }
                    axis=frame->flexion; alignmentAxis=frame->alignment; alignmentAngle=frame->alignmentAngle; alignToNeutral=true;
                } else if (link.mobility == "elbow_hinge" && hasElbowAxes_) {
                    const int h=bone(side==0 ? "humerus_l" : "humerus_r");
                    const auto w=sourceWristFrame(*this,side,down);
                    if (h<0 || !w) { error="Missing elbow hand frame"; return false; }
                    forearmAxis=normalized(w->wrist-pivot);
                    const auto wrist=wristReferenceFrame(forearmAxis,w->hand,w->palm);
                    const Vec3 palm=frameVector(joint_detail::rotate(wrist.alignment,wrist.alignmentAngle),w->palm);
                    const auto frame=mirroredElbowFrame(bones_[h].pivot,pivot,w->wrist,palm,side==0,spineTurn);
                    axis=frame.flexion; alignmentAxis=frame.alignment; alignmentAngle=frame.alignmentAngle; alignToNeutral=true;
                } else if (link.mobility == "elbow_hinge") {
                    current = std::atan2(length(axis),std::clamp(dot(up,down*-1.0),-1.0,1.0));
                }
                if (length(axis) < 1e-5) {
                    Vec3 guide = std::abs(up.z) < 0.9 ? Vec3{0,0,1} : Vec3{0,1,0};
                    axis = cross(up,guide);
                }
                if (length(axis) < 1e-8) { error = "Degenerate source joint axis for " + childName; return false; }
                // Coordinates refer to anatomical neutral, but the supplied
                // mesh is already in its subject's source pose. Make every
                // joint's source coordinate the identity relative transform.
                // Otherwise a nonzero parent bind angle incorrectly rotates
                // the child's pivot and axis before any posing occurs.
                current=std::clamp(current,bounds->extension*toRadians,bounds->flexion*toRadians);
                JointRigidTransform frame; frame.translation = pivot;
                const auto parentFrame = joint_detail::compose(frame,joint_detail::rotate(normalized(axis),-current));
                std::vector<JointRotationalAxis> axes{{axis,bounds->extension*toRadians,bounds->flexion*toRadians}};
                bindAngles={current};
                if (alignToNeutral && link.mobility!="hip_ball_socket") { axes.push_back({alignmentAxis,0,3.14159265358979323846}); bindAngles.push_back(0); }
                if (link.mobility=="elbow_hinge" && alignToNeutral) { const auto r=*referenceBendRange("radioulnar_pivot"); axes.push_back({forearmAxis*(side==0 ? 1.0 : -1.0),r.extension*toRadians,r.flexion*toRadians}); bindAngles.push_back(0); }
                if (link.mobility == "hip_ball_socket" && hasKneeAxes_) {
                    // Internal/external rotation around the femoral shaft.
                    const Vec3 shaft{0,side==0 ? -1.0 : 1.0,0};
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
    neutralAlignments_ = std::move(neutralAlignments);
    return true;
}

bool SkeletonModel::restoreSourcePose() {
    return !referenceBindPose_.empty() && setSourcePose(referenceBindPose_);
}

bool SkeletonModel::setSourcePose(const std::vector<AnatomicalKinematicTree::PoseUpdate>& updates) {
    if (!sourceRig_ || !sourceRig_->setPose(updates)) return false;
    ++sourcePoseRevision_;
    return true;
}

std::optional<double> SkeletonModel::referenceBend(std::size_t link) const {
    if (!sourceRig_ || link >= sourceLinks_.size()) return std::nullopt;
    const auto& edge = sourceLinks_[link];
    if (edge.first >= sourceBodies_.size() || edge.second >= sourceBodies_.size()) return std::nullopt;
    const auto a = sourceBodies_[edge.first], b = sourceBodies_[edge.second];
    std::size_t child;
    if (sourceRig_->parent(b) == a) child = b;
    else if (sourceRig_->parent(a) == b) child = a;
    else return std::nullopt;
    const auto* joint = sourceRig_->joint(child);
    if (!joint || joint->angles().empty()) return std::nullopt;
    return joint->angles()[0] * (180.0 / 3.14159265358979323846);
}

bool SkeletonModel::setReferenceBend(std::size_t link, double degrees) {
    if (!sourceRig_ || link >= sourceLinks_.size()) return false;
    const auto& relationship = sourceLinks_[link];
    const auto bounded = clampReferenceBend(relationship.mobility, degrees,
        sourceStructures_[relationship.first].name, sourceStructures_[relationship.second].name);
    if (!bounded || relationship.first >= sourceBodies_.size() || relationship.second >= sourceBodies_.size()) return false;
    const auto first = sourceBodies_[relationship.first], second = sourceBodies_[relationship.second];
    std::size_t body;
    if (sourceRig_->parent(second) == first) body = second;
    else if (sourceRig_->parent(first) == second) body = first;
    else return false;
    const auto* joint = sourceRig_->joint(body);
    if (!joint || joint->angles().empty()) return false;
    constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;
    auto angles = joint->angles();
    angles[0] = *bounded * radiansPerDegree;
    if (body<neutralAlignments_.size() && neutralAlignments_[body]>=0 && angles.size()>1)
        angles[relationship.mobility=="hip_ball_socket" ? 3 : 1]=neutralAlignments_[body];
    std::vector<AnatomicalKinematicTree::PoseUpdate> updates{{body, angles}};
    if (relationship.mobility == "elbow_hinge" || relationship.mobility == "wrist_condyloid") {
        // A paired forearm or palm must not split when one of its anatomical
        // links is addressed. Both child joints use the same frame and angle.
        const std::string prefix = relationship.mobility == "elbow_hinge" ? "Humerus_" : "Radius_";
        const auto parent = sourceStructures_[relationship.first].name.rfind(prefix,0) == 0
                                ? relationship.first : relationship.second;
        for (const auto& other : sourceLinks_) {
            if (&other == &relationship || other.mobility != relationship.mobility ||
                (other.first != parent && other.second != parent)) continue;
            const auto neighbour = other.first == parent ? other.second : other.first;
            const auto otherBody = sourceBodies_[neighbour];
            if (sourceRig_->parent(otherBody) != sourceBodies_[parent]) continue;
            const auto* otherJoint = sourceRig_->joint(otherBody);
            if (!otherJoint || otherJoint->angles().empty()) continue;
            auto otherAngles = otherJoint->angles(); otherAngles[0] = angles[0];
            if (otherBody<neutralAlignments_.size() && neutralAlignments_[otherBody]>=0 && otherAngles.size()>1)
                otherAngles[1]=neutralAlignments_[otherBody];
            if (otherAngles.size()>2 && angles.size()>2) otherAngles[2]=angles[2];
            updates.push_back({otherBody,otherAngles});
        }
    }
    if (relationship.mobility == "knee_modified_hinge") {
        // Drive the patella relative to its *source* position. The source CT
        // is not assumed to be fully extended, so coupling uses the change in
        // knee angle, not an absolute angle from a guessed bind pose.
        const auto femur=sourceStructures_[relationship.first].name.rfind("Femur_",0)==0
                             ? relationship.first : relationship.second;
        for (const auto& other : sourceLinks_) {
            if (other.mobility != "patellofemoral_glide" ||
                (other.first != femur && other.second != femur)) continue;
            const auto patella=other.first==femur ? other.second : other.first;
            const auto patellaBody=sourceBodies_[patella];
            const auto* patellarJoint=sourceRig_->joint(patellaBody);
            if (!patellarJoint || sourceRig_->parent(patellaBody)!=sourceBodies_[femur]) continue;
            for (const auto& bind : referenceBindPose_) if (bind.body==body && !bind.angles.empty()) {
                auto patellarAngles=patellarJoint->angles();
                patellarAngles[0]=0.7*(angles[0]-bind.angles[0]);
                updates.push_back({patellaBody,patellarAngles});
                break;
            }
        }
    }
    return setSourcePose(updates);
}

std::vector<std::array<double, 16>> SkeletonModel::sourceMatrices(Vec3 origin, double yaw) const {
    std::vector<std::array<double, 16>> matrices;
    matrices.reserve(sourceStructures_.size());
    if (sourceRig_) {
        const double c = std::cos(yaw), s = std::sin(yaw);
        JointRigidTransform placement;
        placement.rotation = {c,0,s, 0,1,0, -s,0,c};
        placement.translation = origin;
        if (!sourceRig_->evaluate(placement)) return {};
        for (const auto body : sourceBodies_) {
            const auto& t = sourceRig_->skinTransforms()[body];
            matrices.push_back({t.rotation[0],t.rotation[3],t.rotation[6],0,
                                t.rotation[1],t.rotation[4],t.rotation[7],0,
                                t.rotation[2],t.rotation[5],t.rotation[8],0,
                                t.translation.x,t.translation.y,t.translation.z,1});
        }
    } else {
        const auto groups = boneMatrices(origin, yaw);
        for (const auto& part : sourceStructures_) matrices.push_back(groups[part.group]);
    }
    return matrices;
}

void SkeletonModel::buildSourceRestGeometry(Rasterizer& rasterizer, int firstObjectId) const {
    Material material;
    material.color = colorHex("#e3d9bf");
    material.specular = 0.12;
    for (std::size_t index = 0; index < sourceStructures_.size(); ++index) {
        const auto& source = sourceStructures_[index];
        const auto& group = bones_[source.group];
        std::vector<Vec3> points(group.points.begin() + source.firstPoint,
                                 group.points.begin() + source.firstPoint + source.pointCount);
        std::vector<std::array<int, 3>> triangles;
        triangles.reserve(source.triangleCount);
        for (std::size_t i = source.firstTriangle; i < source.firstTriangle + source.triangleCount; ++i) {
            const auto face = group.triangles[i];
            triangles.push_back({face[0] - static_cast<int>(source.firstPoint),
                                 face[1] - static_cast<int>(source.firstPoint),
                                 face[2] - static_cast<int>(source.firstPoint)});
        }
        rasterizer.setObject(static_cast<std::uint32_t>(firstObjectId + static_cast<int>(index)),
                             {0.0, 0.0}, 0.0F);
        rasterizer.mesh(points, triangles, material);
    }
}

void SkeletonModel::drawBones(Rasterizer& rasterizer, const Camera& camera, Vec3 origin, double yaw,
                              std::size_t begin, std::size_t end) const {
    const std::vector<Transform> pose = solvePose(bones_, origin, yaw);
    Material boneMaterial;
    boneMaterial.color = colorHex("#e3d9bf");
    boneMaterial.specular = 0.12;
    end = std::min(end, bones_.size());
    for (std::size_t index = begin; index < end; ++index) {
        const SkeletonBone& bone = bones_[index];
        // Scratch, not a fresh allocation: this is 28 allocations per body per
        // pose and they all have the same short lifetime.
        thread_local std::vector<Vec3> world;
        world.clear();
        world.reserve(bone.points.size());
        for (const Vec3& point : bone.points) world.push_back(pose[index].apply(point));
        for (const auto& face : bone.triangles) {
            if (face[0] < 0 || face[1] < 0 || face[2] < 0) continue;
            if (face[0] >= static_cast<int>(world.size()) || face[1] >= static_cast<int>(world.size()) ||
                face[2] >= static_cast<int>(world.size())) continue;
            const Vec3 a = world[static_cast<std::size_t>(face[0])];
            const Vec3 b = world[static_cast<std::size_t>(face[1])];
            const Vec3 c = world[static_cast<std::size_t>(face[2])];
            // Left unnormalised on purpose: the GPU shaders normalise what they
            // are given, so the square root and three divides per triangle were
            // being thrown away. The software path normalises in worldTriangle,
            // where its own flat-shading test needs real unit normals.
            rasterizer.worldTriangle(a, b, c, cross(b - a, c - a), boneMaterial);
        }
    }
}

} // namespace gtc::native
