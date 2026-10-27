#include "native.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace gtc::native {
namespace {

const std::vector<int> kKey = {
    1, 4, 10, 13, 14, 33, 133, 159, 145, 362, 263, 386, 374, 61, 291, 152,
    234, 454, 70, 300, 63, 293, 105, 334, 66, 296, 107, 336, 55, 285, 65,
    295, 52, 282, 53, 283, 46, 276, 48, 115, 220, 45, 64, 98, 327, 344,
    278, 440, 275, 50, 280, 101, 330, 205, 425, 17, 0, 37, 39, 40, 267,
    269, 270, 321, 405, 314, 84, 181, 91, 146
};
const std::set<int> kFeature = {
    1, 4, 10, 13, 14, 33, 133, 159, 145, 362, 263, 386, 374, 61, 291,
    17, 0, 152
};
const std::set<int> kKeySet(kKey.begin(), kKey.end());

std::vector<std::string> strings(const gtc::JsonValue* value) {
    std::vector<std::string> result;
    if (!value) return result;
    for (const auto& item : value->array()) result.push_back(item.string());
    return result;
}

std::vector<int> integers(const gtc::JsonValue* value) {
    std::vector<int> result;
    if (!value) return result;
    for (const auto& item : value->array()) result.push_back(static_cast<int>(item.number()));
    return result;
}

bool parseFace(const std::string& token, int& vertex, int& uv) {
    const std::size_t slash = token.find('/');
    if (slash == std::string::npos) return false;
    vertex = std::atoi(token.substr(0, slash).c_str()) - 1;
    const std::size_t second = token.find('/', slash + 1);
    uv = std::atoi(token.substr(slash + 1, second - slash - 1).c_str()) - 1;
    return vertex >= 0 && uv >= 0;
}

} // namespace

std::string readText(const std::string& path, bool& ok) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    ok = input.good() || input.eof();
    return buffer.str();
}

bool writeText(const std::string& path, const std::string& text, std::string& error) {
    std::error_code code;
    const std::filesystem::path destination(path);
    if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path(), code);
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "Cannot write " + path;
        return false;
    }
    output << text;
    if (!output) {
        error = "Short write to " + path;
        return false;
    }
    return true;
}

const Target& HumanModel::target(const std::string& path) {
    auto found = targetCache_.find(path);
    if (found != targetCache_.end()) return found->second;
    Target result;
    std::ifstream input(path);
    if (!input) std::cerr << "missing target: " << path << '\n';
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        int index = -1;
        Vec3 delta;
        if (stream >> index >> delta.x >> delta.y >> delta.z && index >= 0) result[index] = delta;
    }
    return targetCache_.emplace(path, std::move(result)).first->second;
}

bool HumanModel::loadMesh(const std::string& root, std::string& error) {
    const std::string source = root + "/tools/human-source";
    std::ifstream input(source + "/base.obj");
    if (!input) {
        error = "Cannot open CC0 base.obj";
        return false;
    }
    std::string line;
    std::string group;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string kind;
        stream >> kind;
        if (kind == "v") {
            Vec3 point;
            stream >> point.x >> point.y >> point.z;
            vertices.push_back(point);
        } else if (kind == "vt") {
            Vec2 uv;
            stream >> uv.x >> uv.y;
            texcoords.push_back(uv);
        } else if (kind == "g") {
            stream >> group;
        } else if (kind == "f" && group == "body") {
            std::vector<std::array<int, 2>> face;
            std::string token;
            while (stream >> token) {
                int vertex = -1;
                int uv = -1;
                if (parseFace(token, vertex, uv)) face.push_back({vertex, uv});
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                Triangle triangle;
                const std::array<int, 2> corners[3] = {face[0], face[i], face[i + 1]};
                for (int corner = 0; corner < 3; ++corner) {
                    triangle.v[corner] = corners[corner][0];
                    triangle.uv[corner] = corners[corner][1];
                }
                triangles.push_back(triangle);
            }
        }
    }
    if (vertices.empty() || triangles.empty()) {
        error = "CC0 base mesh is empty";
        return false;
    }
    baseMacro_ = target(source + (male ? "/caucasian-male-young.target" : "/caucasian-female-young.target"));
    for (const auto& [index, delta] : baseMacro_) {
        if (index >= 0 && static_cast<std::size_t>(index) < vertices.size()) vertices[index] += delta;
    }
    double minimum = 1e100;
    double maximum = -1e100;
    std::set<int> bodyVertices;
    for (const auto& triangle : triangles) {
        for (int index : triangle.v) {
            bodyVertices.insert(index);
            minimum = std::min(minimum, vertices[index].y);
            maximum = std::max(maximum, vertices[index].y);
        }
    }
    ground = -minimum;
    scale = 1.8 / std::max(1e-9, maximum - minimum);
    return true;
}

bool HumanModel::loadRig(const std::string& root, std::string& error) {
    bool ok = false;
    const gtc::JsonValue skeleton = gtc::JsonValue::parse(
        readText(root + "/tools/human-source/default.mhskel", ok), ok);
    if (!ok) {
        error = "Cannot parse default.mhskel";
        return false;
    }
    const gtc::JsonValue* bones = skeleton.get("bones");
    const gtc::JsonValue* joints = skeleton.get("joints");
    if (!bones || !joints) {
        error = "Skeleton has no bones/joints";
        return false;
    }
    auto jointIds = [&](const std::string& bone, const std::string& part) {
        const gtc::JsonValue* definition = bones->get(bone);
        const std::string name = definition ? definition->stringAt(part) : "";
        return integers(joints->get(name));
    };
    neckSource_ = jointIds("neck01", "head");
    eyeSource[0] = jointIds("eye.L", "head");
    eyeSource[1] = jointIds("eye.R", "head");
    const std::vector<int> eyeTail[2] = {jointIds("eye.L", "tail"), jointIds("eye.R", "tail")};
    if (neckSource_.empty() || eyeSource[0].empty() || eyeSource[1].empty()) {
        error = "Skeleton eye/neck joints are missing";
        return false;
    }
    headCutoff = joint(neckSource_).y - 0.07;
    headShift = joint(eyeSource[0]).y - 7.28415;
    for (int side = 0; side < 2; ++side) {
        eyeDirection[side] = normalized(joint(eyeTail[side]) - joint(eyeSource[side]));
    }
    std::set<int> headSet;
    for (const auto& triangle : triangles) {
        for (int index : triangle.v) {
            if (vertices[index].y > headCutoff - 0.25) headSet.insert(index);
        }
    }
    headVertices.assign(headSet.begin(), headSet.end());
    for (const auto& triangle : triangles) {
        const Vec3 center = (vertices[triangle.v[0]] + vertices[triangle.v[1]] + vertices[triangle.v[2]]) / 3.0;
        if (center.y > headCutoff + 0.36) headTriangles.push_back(triangle);
    }
    Vec3 low{1e100, 1e100, 1e100};
    Vec3 high{-1e100, -1e100, -1e100};
    for (int index : headVertices) {
        const Vec3 point = runtime(vertices[index]);
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y); low.z = std::min(low.z, point.z);
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y); high.z = std::max(high.z, point.z);
    }
    headCenter = (low + high) * 0.5;
    return true;
}

std::vector<Vec3> HumanModel::morphDelta(const std::vector<std::string>& paths) {
    std::vector<Vec3> combined(vertices.size());
    const std::string& source = sourceRoot_;
    for (const std::string& relative : paths) {
        if (relative.empty()) continue;
        if (relative[0] != '@') {
            std::string path = relative;
            const std::string token = "{sex}";
            const std::size_t at = path.find(token);
            if (at != std::string::npos) path.replace(at, token.size(), male ? "male" : "female");
            for (const auto& [index, delta] : target(source + "/targets/" + path)) {
                if (index >= 0 && static_cast<std::size_t>(index) < combined.size()) combined[index] += delta;
            }
            continue;
        }
        const std::string groupName = relative.substr(1);
        const std::string macro = groupName == "old" ? "caucasian" : groupName;
        const std::string filename = macro + (male ? "-male-" : "-female-") +
            (groupName == "old" ? "old.target" : "young.target");
        const Target& destination = target(source + "/targets/macrodetails/" + filename);
        std::set<int> indices;
        for (const auto& [index, unused] : destination) indices.insert(index);
        for (const auto& [index, unused] : baseMacro_) indices.insert(index);
        Vec3 reference;
        for (int index : neckSource_) {
            const auto a = destination.find(index);
            const auto b = baseMacro_.find(index);
            if (a != destination.end()) reference += a->second;
            if (b != baseMacro_.end()) reference -= b->second;
        }
        reference = reference / std::max<std::size_t>(1, neckSource_.size());
        for (int index : indices) {
            if (index < 0 || static_cast<std::size_t>(index) >= vertices.size() ||
                vertices[index].y < headCutoff - 0.20) continue;
            Vec3 delta = reference * -1.0;
            const auto a = destination.find(index);
            const auto b = baseMacro_.find(index);
            if (a != destination.end()) delta += a->second;
            if (b != baseMacro_.end()) delta -= b->second;
            combined[index] += delta;
        }
    }
    return combined;
}

bool HumanModel::loadMorphs(const std::string& root, std::string& error) {
    bool ok = false;
    const gtc::JsonValue definitions = gtc::JsonValue::parse(readText(root + "/tools/human-morphs.json", ok), ok);
    if (!ok || definitions.array().empty()) {
        error = "Cannot parse universal human-morphs.json";
        return false;
    }
    for (const auto& definition : definitions.array()) {
        MorphSpec spec;
        spec.key = definition.stringAt("key");
        spec.tab = definition.stringAt("tab");
        spec.minimum = static_cast<int>(definition.numberAt("min", -100));
        spec.maximum = static_cast<int>(definition.numberAt("max", 100));
        spec.positive = strings(definition.get("positive"));
        spec.negative = strings(definition.get("negative"));
        specs.push_back(spec);
    }
    positive.reserve(specs.size());
    negative.reserve(specs.size());
    for (const auto& spec : specs) {
        positive.push_back(morphDelta(spec.positive));
        negative.push_back(morphDelta(spec.negative));
    }
    targetCache_.clear();
    return true;
}

bool HumanModel::loadLandmarks(const std::string& root, std::string& error) {
    std::ifstream input(root + "/tools/human-source/portrait-fitting/canonical_face_model.obj");
    if (!input) {
        error = "Cannot open canonical_face_model.obj";
        return false;
    }
    std::vector<Vec3> canonical;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("v ", 0) != 0) continue;
        std::istringstream stream(line.substr(2));
        Vec3 point;
        stream >> point.x >> point.y >> point.z;
        canonical.push_back(point);
    }
    if (canonical.size() < 468) {
        error = "Canonical face model has fewer than 468 vertices";
        return false;
    }
    canonical.resize(468);
    const Vec3 sourceCenter = (canonical[33] + canonical[263]) * 0.5;
    const Vec3 eyeCenter = (joint(eyeSource[0]) + joint(eyeSource[1])) * 0.5;
    canonicalVertex.resize(468);
    canonicalDistance.resize(468);
    for (int landmark = 0; landmark < 468; ++landmark) {
        const Vec3 aligned = (canonical[landmark] - sourceCenter) * scale + eyeCenter;
        double best = 1e100;
        int bestIndex = 0;
        for (int index : headVertices) {
            const double distance = length(vertices[index] - aligned);
            if (distance < best) {
                best = distance;
                bestIndex = index;
            }
        }
        canonicalVertex[landmark] = bestIndex;
        canonicalDistance[landmark] = best;
    }
    std::set<int> selected(kKey.begin(), kKey.end());
    for (int index = 0; index < 468; index += 6) selected.insert(index);
    for (int index : selected) {
        if (canonicalDistance[index] >= 0.34) continue;
        fitIndices.push_back(index);
        fitVertices.push_back(canonicalVertex[index]);
        double weight = 0.32;
        if (kFeature.count(index)) weight = 1.8;
        else if (kKeySet.count(index)) weight = 1.0;
        else if (index == 10 || index == 152 || index == 234 || index == 454) weight = 0.7;
        fitWeights.push_back(weight);
    }
    return !fitIndices.empty();
}

bool HumanModel::load(const std::string& root, bool isMale, std::string& error) {
    male = isMale;
    sourceRoot_ = root + "/tools/human-source";
    vertices.clear(); texcoords.clear(); triangles.clear(); headTriangles.clear(); headVertices.clear();
    specs.clear(); positive.clear(); negative.clear(); fitIndices.clear(); fitVertices.clear(); fitWeights.clear();
    targetCache_.clear(); baseMacro_.clear(); neckSource_.clear();
    return loadMesh(root, error) && loadRig(root, error) && loadMorphs(root, error) && loadLandmarks(root, error);
}

Vec3 HumanModel::runtime(Vec3 raw) const {
    return {raw.x * scale, (raw.y + ground) * scale, raw.z * scale};
}

Vec3 HumanModel::joint(const std::vector<int>& ids) const {
    Vec3 result;
    for (int index : ids) if (index >= 0 && static_cast<std::size_t>(index) < vertices.size()) result += vertices[index];
    return result / std::max<std::size_t>(1, ids.size());
}

std::vector<Vec3> HumanModel::shape(const std::vector<int>& ids, const std::vector<double>& values) const {
    std::vector<Vec3> result;
    result.reserve(ids.size());
    for (int index : ids) result.push_back(runtime(vertices[index]));
    for (std::size_t spec = 0; spec < specs.size(); ++spec) {
        const double value = spec < values.size() ? values[spec] / 100.0 : 0;
        if (std::abs(value) < 1e-12) continue;
        const auto& deltas = value > 0 ? positive[spec] : negative[spec];
        const double amount = std::abs(value) * scale;
        for (std::size_t point = 0; point < ids.size(); ++point) {
            const int index = ids[point];
            if (index >= 0 && static_cast<std::size_t>(index) < deltas.size()) result[point] += deltas[index] * amount;
        }
    }
    return result;
}

Vec3 HumanModel::shapedJoint(const std::vector<int>& ids, const std::vector<double>& values) const {
    const std::vector<Vec3> points = shape(ids, values);
    Vec3 result;
    for (Vec3 point : points) result += point;
    return result / std::max<std::size_t>(1, points.size());
}

int HumanModel::specIndex(const std::string& key) const {
    for (std::size_t i = 0; i < specs.size(); ++i) if (specs[i].key == key) return static_cast<int>(i);
    return -1;
}

} // namespace gtc::native
