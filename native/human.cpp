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

struct SurfaceHit {
    Vec3 point;
    double distance = 1e100;
    std::array<int, 3> face = {0, 0, 0};
    std::array<double, 3> bary = {1, 0, 0};
};

Vec3 closestTriangle(Vec3 point, Vec3 a, Vec3 b, Vec3 c, std::array<double, 3>& weights) {
    const Vec3 ab = b - a, ac = c - a, ap = point - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) { weights = {1, 0, 0}; return a; }
    const Vec3 bp = point - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) { weights = {0, 1, 0}; return b; }
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const double v = d1 / (d1 - d3);
        weights = {1 - v, v, 0}; return a + ab * v;
    }
    const Vec3 cp = point - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) { weights = {0, 0, 1}; return c; }
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const double w = d2 / (d2 - d6);
        weights = {1 - w, 0, w}; return a + ac * w;
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        weights = {0, 1 - w, w}; return b + (c - b) * w;
    }
    const double denominator = va + vb + vc;
    if (std::abs(denominator) < 1e-14) { weights = {1, 0, 0}; return a; }
    const double v = vb / denominator, w = vc / denominator;
    weights = {1 - v - w, v, w};
    return a + ab * v + ac * w;
}

SurfaceHit nearestSurface(Vec3 point, const std::vector<Triangle>& faces, const std::vector<Vec3>& vertices) {
    SurfaceHit best;
    for (const auto& triangle : faces) {
        std::array<double, 3> weights;
        const Vec3 candidate = closestTriangle(point, vertices[triangle.v[0]], vertices[triangle.v[1]],
                                               vertices[triangle.v[2]], weights);
        const double distance = length(point - candidate);
        if (distance < best.distance) {
            best.distance = distance;
            best.point = candidate;
            best.face = {triangle.v[0], triangle.v[1], triangle.v[2]};
            best.bary = weights;
        }
    }
    return best;
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
    bool weightsOk = false;
    const gtc::JsonValue weightDocument = gtc::JsonValue::parse(
        readText(root + "/tools/human-source/default_weights.mhw", weightsOk), weightsOk);
    const gtc::JsonValue* weightData = weightsOk ? weightDocument.get("weights") : nullptr;
    if (!weightData) {
        error = "Cannot parse CC0 default_weights.mhw";
        return false;
    }
    const std::set<std::string> keep = {
        "root", "spine03", "spine02", "spine01", "neck01", "head", "jaw",
        "clavicle.L", "shoulder01.L", "upperarm01.L", "upperarm02.L", "lowerarm01.L",
        "lowerarm02.L", "wrist.L", "pelvis.L", "upperleg01.L", "upperleg02.L",
        "lowerleg01.L", "lowerleg02.L", "foot.L", "clavicle.R", "shoulder01.R",
        "upperarm01.R", "upperarm02.R", "lowerarm01.R", "lowerarm02.R", "wrist.R",
        "pelvis.R", "upperleg01.R", "upperleg02.R", "lowerleg01.R", "lowerleg02.R", "foot.R"
    };
    auto keptAncestor = [&](std::string name) {
        while (!name.empty() && !keep.count(name)) {
            const gtc::JsonValue* definition = bones->get(name);
            name = definition ? definition->stringAt("parent") : "";
        }
        return name.empty() ? std::string("root") : name;
    };
    std::vector<double> headWeights(vertices.size(), 0.0);
    for (const auto& [bone, entries] : weightData->object()) {
        const std::string target = keptAncestor(bone);
        if (target != "neck01" && target != "head" && target != "jaw") continue;
        for (const auto& entry : entries.array()) {
            const auto& pair = entry.array();
            if (pair.size() < 2) continue;
            const int index = static_cast<int>(pair[0].number());
            if (index >= 0 && static_cast<std::size_t>(index) < headWeights.size()) headWeights[index] += pair[1].number();
        }
    }
    const double collar = headCutoff - 0.78;
    std::set<int> headSet;
    for (const auto& triangle : triangles) {
        for (int index : triangle.v) {
            if (vertices[index].y > headCutoff - 0.25) headSet.insert(index);
        }
    }
    headVertices.assign(headSet.begin(), headSet.end());
    for (const auto& triangle : triangles) {
        const Vec3 center = (vertices[triangle.v[0]] + vertices[triangle.v[1]] + vertices[triangle.v[2]]) / 3.0;
        if (center.y > headCutoff - 0.25) mappingTriangles.push_back(triangle);
        const double headWeight = (headWeights[triangle.v[0]] + headWeights[triangle.v[1]] + headWeights[triangle.v[2]]) / 3.0;
        const bool centralNeck = center.y > headCutoff - 0.20 && std::abs(center.x) < 0.65;
        if (center.y > headCutoff || (center.y > collar && headWeight > 0.42) || centralNeck) headTriangles.push_back(triangle);
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
    if (!fitIndices.empty()) {
        std::array<double, 3> scales = {scale, scale, scale};
        Vec3 translation{eyeCenter.x - sourceCenter.x * scale,
                         eyeCenter.y - sourceCenter.y * scale,
                         eyeCenter.z - sourceCenter.z * scale};
        std::vector<SurfaceHit> hits(fitIndices.size());
        for (int iteration = 0; iteration < 8; ++iteration) {
            for (std::size_t i = 0; i < fitIndices.size(); ++i) {
                const Vec3 source = canonical[fitIndices[i]];
                const Vec3 aligned{source.x * scales[0] + translation.x,
                                   source.y * scales[1] + translation.y,
                                   source.z * scales[2] + translation.z};
                hits[i] = nearestSurface(aligned, mappingTriangles, vertices);
            }
            for (int axis = 0; axis < 3; ++axis) {
                double sw = 0, sx = 0, sxx = 0, sy = 0, sxy = 0;
                for (std::size_t i = 0; i < fitIndices.size(); ++i) {
                    const Vec3 source = canonical[fitIndices[i]];
                    const double coordinate = axis == 0 ? source.x : axis == 1 ? source.y : source.z;
                    const double target = axis == 0 ? hits[i].point.x : axis == 1 ? hits[i].point.y : hits[i].point.z;
                    const double weight = std::min(1.0, 0.08 / std::max(0.08, hits[i].distance));
                    sw += weight; sx += weight * coordinate; sxx += weight * coordinate * coordinate;
                    sy += weight * target; sxy += weight * coordinate * target;
                }
                const double determinant = sw * sxx - sx * sx;
                if (std::abs(determinant) < 1e-12) continue;
                const double newScale = (sw * sxy - sx * sy) / determinant;
                const double newTranslation = (sy - newScale * sx) / sw;
                scales[axis] = std::clamp(newScale, 0.005, 2.0);
                if (axis == 0) translation.x = newTranslation;
                else if (axis == 1) translation.y = newTranslation;
                else translation.z = newTranslation;
            }
        }
        fitSurfaceTriangles.reserve(fitIndices.size());
        fitSurfaceBary.reserve(fitIndices.size());
        surfaceReady = true;
        for (std::size_t i = 0; i < fitIndices.size(); ++i) {
            const Vec3 source = canonical[fitIndices[i]];
            const Vec3 aligned{source.x * scales[0] + translation.x,
                               source.y * scales[1] + translation.y,
                               source.z * scales[2] + translation.z};
            const SurfaceHit hit = nearestSurface(aligned, mappingTriangles, vertices);
            fitSurfaceTriangles.push_back(hit.face);
            fitSurfaceBary.push_back(hit.bary);
            if (hit.distance > 0.80) surfaceReady = false;
        }
    }
    return !fitIndices.empty();
}

bool HumanModel::load(const std::string& root, bool isMale, std::string& error) {
    male = isMale;
    sourceRoot_ = root + "/tools/human-source";
    vertices.clear(); texcoords.clear(); triangles.clear(); mappingTriangles.clear(); headTriangles.clear(); headVertices.clear();
    specs.clear(); positive.clear(); negative.clear(); fitIndices.clear(); fitVertices.clear(); fitWeights.clear();
    fitSurfaceTriangles.clear(); fitSurfaceBary.clear(); surfaceReady = false;
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

std::vector<Vec3> HumanModel::shapeFit(const std::vector<double>& values, bool useSurface) const {
    if (!useSurface || !surfaceReady || fitSurfaceTriangles.size() != fitIndices.size()) return shape(fitVertices, values);
    std::vector<int> ids;
    std::set<int> unique;
    for (const auto& triangle : fitSurfaceTriangles) for (int index : triangle) unique.insert(index);
    ids.assign(unique.begin(), unique.end());
    const std::vector<Vec3> shaped = shape(ids, values);
    std::unordered_map<int, Vec3> points;
    for (std::size_t i = 0; i < ids.size(); ++i) points[ids[i]] = shaped[i];
    std::vector<Vec3> result;
    result.reserve(fitIndices.size());
    for (std::size_t i = 0; i < fitSurfaceTriangles.size(); ++i) {
        const auto& triangle = fitSurfaceTriangles[i];
        const auto& bary = fitSurfaceBary[i];
        result.push_back(points[triangle[0]] * bary[0] + points[triangle[1]] * bary[1] + points[triangle[2]] * bary[2]);
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
