#include "client/skeleton_body.h"
#include "client/skeleton_finger_frame.h"
#include "client/skeleton_shoulder_frame.h"
#include "client/skeleton_leg_frame.h"
#include "client/skeleton_spine_frame.h"
#include "client/skeleton_toe_frame.h"
#include "client/skeleton_body_math.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <set>
namespace gtc::native {

using namespace skeleton_detail;


bool SkeletonModel::load(const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Cannot open skeleton " + path;
        return false;
    }
    bones_.clear();
    sourceStructures_.clear();
    sourceLinks_.clear(); orbitalCenters_.reset(); dynamicSourcePose_.clear();
    sourceRig_.reset();
    sourceBodies_.clear();
    neutralAlignments_.clear(); disabledAlignments_.clear();
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
    if (!dynamicSourcePose_.empty()) { drawDynamicSource(rasterizer,origin,yaw); return; }
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
    neutralAlignments_.clear(); disabledAlignments_.clear();
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
    // A marked reference can already be outside a normal range. Let the
    // joint recover toward it incrementally, without snapping the bone pose.
    angles[0] = (joint->measuredPrimary() ? degrees : *bounded) * radiansPerDegree;
    if (body<neutralAlignments_.size() && neutralAlignments_[body]>=0 && !disabledAlignments_[body] && angles.size()>1)
        angles[relationship.mobility=="hip_ball_socket" || relationship.mobility=="sternoclavicular" || relationship.mobility=="acromioclavicular" ? 3 : 1]=neutralAlignments_[body];
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
    matrices.reserve(sourceStructures_.size()); if (!dynamicSourcePose_.empty()) return placedDynamicPose(origin,yaw);
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
    } else { const auto groups = boneMatrices(origin, yaw);
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
