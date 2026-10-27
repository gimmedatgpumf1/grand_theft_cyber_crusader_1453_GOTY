#pragma once

#include "core/json.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gtc::native {

struct Vec2 {
    double x = 0;
    double y = 0;
};

struct Vec3 {
    double x = 0;
    double y = 0;
    double z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double b) { return {a.x * b, a.y * b, a.z * b}; }
inline Vec3 operator/(Vec3 a, double b) { return {a.x / b, a.y / b, a.z / b}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(Vec3 a) { double n = length(a); return n > 1e-12 ? a / n : Vec3{}; }

struct Triangle {
    int v[3] = {0, 0, 0};
    int uv[3] = {0, 0, 0};
};

struct MorphSpec {
    std::string key;
    std::string tab;
    int minimum = -100;
    int maximum = 100;
    std::vector<std::string> positive;
    std::vector<std::string> negative;
};

using Target = std::unordered_map<int, Vec3>;

class HumanModel {
public:
    bool load(const std::string& root, bool male, std::string& error);

    bool male = true;
    double scale = 0.108;
    double ground = 8.1676;
    double headCutoff = 0;
    double headShift = 0;
    Vec3 headCenter;
    std::vector<Vec3> vertices;
    std::vector<Vec2> texcoords;
    std::vector<Triangle> triangles;
    std::vector<Triangle> headTriangles;
    std::vector<int> headVertices;
    std::vector<MorphSpec> specs;
    std::vector<std::vector<Vec3>> positive;
    std::vector<std::vector<Vec3>> negative;
    std::vector<int> canonicalVertex;
    std::vector<double> canonicalDistance;
    std::vector<int> fitIndices;
    std::vector<int> fitVertices;
    std::vector<double> fitWeights;
    std::vector<int> eyeSource[2];
    Vec3 eyeDirection[2];

    Vec3 runtime(Vec3 raw) const;
    Vec3 joint(const std::vector<int>& ids) const;
    std::vector<Vec3> shape(const std::vector<int>& ids, const std::vector<double>& values) const;
    Vec3 shapedJoint(const std::vector<int>& ids, const std::vector<double>& values) const;
    int specIndex(const std::string& key) const;

private:
    Target baseMacro_;
    std::string sourceRoot_;
    std::vector<int> neckSource_;
    std::unordered_map<std::string, Target> targetCache_;

    bool loadMesh(const std::string& root, std::string& error);
    bool loadRig(const std::string& root, std::string& error);
    bool loadMorphs(const std::string& root, std::string& error);
    bool loadLandmarks(const std::string& root, std::string& error);
    std::vector<Vec3> morphDelta(const std::vector<std::string>& paths);
    const Target& target(const std::string& path);
};

struct Pose {
    double yaw = 0;
    double pitch = 0;
    double roll = 0;
    double distance = 1.75;
    double fovDegrees = 22;
};

struct Preset {
    std::string key;
    bool male = true;
    int skin = 1;
    int hair = 2;
    int hairColor = 0;
    int eyeColor = 2;
    int makeup = 0;
    int lipColor = 0;
    Pose pose;
    std::vector<double> morphs;
};

struct Reference {
    std::string key;
    std::vector<Vec2> normalized;
    std::vector<Vec2> points;
};

std::vector<Preset> loadPresets(const std::string& root, const HumanModel& model, std::string& error);
std::unordered_map<std::string, Reference> loadReferences(const std::string& root, std::string& error);
std::string presetsJson(const std::vector<Preset>& presets);
bool writeText(const std::string& path, const std::string& text, std::string& error);
std::string readText(const std::string& path, bool& ok);

struct FitResult {
    Preset preset;
    double initialScore = 0;
    double score = 0;
    std::uint64_t evaluations = 0;
    double milliseconds = 0;
    int iterations = 0;
};

FitResult fitPortrait(const HumanModel& model, const Reference& reference, const Preset& initial,
                      int iterations, bool verbose);

bool renderPortrait(const HumanModel& model, const Preset& preset, const std::string& root,
                    const std::string& output, int width, int height, std::string& error);

} // namespace gtc::native
