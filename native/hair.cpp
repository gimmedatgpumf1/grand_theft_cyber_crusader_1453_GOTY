#include "hair.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace gtc::native {
namespace {

struct Mapping {
    std::array<int, 3> refs;
    std::array<double, 3> weights;
    Vec3 offset;
};
struct Clo {
    std::string object;
    std::vector<Mapping> mappings;
    std::map<std::string, std::array<double, 3>> scales;
};
struct Obj {
    std::vector<Vec3> vertices;
    std::vector<Vec2> uvs;
    std::vector<std::array<std::pair<int, int>, 3>> faces;
};

bool corner(const std::string& token, int& vertex, int& uv) {
    const std::size_t slash = token.find('/');
    if (slash == std::string::npos) return false;
    vertex = std::atoi(token.substr(0, slash).c_str()) - 1;
    const std::size_t second = token.find('/', slash + 1);
    const std::string uvText = token.substr(slash + 1, second - slash - 1);
    uv = uvText.empty() ? 0 : std::atoi(uvText.c_str()) - 1;
    return vertex >= 0 && uv >= 0;
}

Obj readObj(const std::string& path) {
    Obj result;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string kind;
        stream >> kind;
        if (kind == "v") {
            Vec3 point; stream >> point.x >> point.y >> point.z; result.vertices.push_back(point);
        } else if (kind == "vt") {
            Vec2 uv; stream >> uv.x >> uv.y; result.uvs.push_back(uv);
        } else if (kind == "f") {
            std::vector<std::pair<int, int>> face;
            std::string token;
            while (stream >> token) {
                int vertex = -1, uv = -1;
                if (corner(token, vertex, uv)) face.emplace_back(vertex, uv);
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                result.faces.push_back({face[0], face[i], face[i + 1]});
            }
        }
    }
    return result;
}

Clo readClo(const std::string& path) {
    Clo result;
    std::ifstream input(path);
    std::string line;
    bool inside = false;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string kind;
        if (!(stream >> kind)) { inside = false; continue; }
        if (kind[0] == '#') continue;
        if (kind == "obj_file") { stream >> result.object; continue; }
        if (kind == "x_scale" || kind == "y_scale" || kind == "z_scale") {
            int a = 0, b = 0; double value = 1;
            stream >> a >> b >> value;
            result.scales[kind] = {static_cast<double>(a), static_cast<double>(b), value};
            continue;
        }
        if (kind == "verts") { inside = true; continue; }
        if (inside && (kind == "material" || kind == "vertexboneweights_file")) continue;
        if (!inside || !std::isdigit(static_cast<unsigned char>(kind[0]))) { inside = false; continue; }
        std::vector<double> values;
        values.push_back(std::atof(kind.c_str()));
        std::string token;
        while (stream >> token) values.push_back(std::atof(token.c_str()));
        if (values.size() == 1) {
            const int index = static_cast<int>(values[0]);
            result.mappings.push_back({{index, index, index}, {1, 0, 0}, {}});
        } else if (values.size() >= 9) {
            Mapping mapping;
            for (int i = 0; i < 3; ++i) {
                mapping.refs[i] = static_cast<int>(values[i]);
                mapping.weights[i] = values[i + 3];
            }
            mapping.offset = {values[6], values[7], values[8]};
            result.mappings.push_back(mapping);
        }
    }
    return result;
}

const std::map<int, std::string> kStyles = {
    {1, "short01"}, {2, "short02"}, {3, "bob01"}, {4, "grump"}, {5, "waves"},
    {6, "long-bangs"}, {7, "short04"}, {8, "short01"}, {9, "ponytail01"},
    {10, "braid01"}, {11, "afro01"}, {12, "long-waves"}, {13, "wavy-bob"},
    {14, "curly-bob"}, {15, "fifties-updo"}
};

} // namespace

bool HairLibrary::load(const std::string& root, const HumanModel& model, std::string& error) {
    (void)model;
    styles_.clear();
    const std::string base = root + "/tools/human-source/hair/";
    for (const auto& [number, key] : kStyles) {
        const std::string folder = base + key;
        std::string cloPath;
        std::error_code code;
        for (const auto& entry : std::filesystem::directory_iterator(folder, code)) {
            if (entry.path().extension() == ".mhclo") { cloPath = entry.path().string(); break; }
        }
        if (cloPath.empty()) {
            error = "No MHCLO for hair style " + key;
            return false;
        }
        const Clo clo = readClo(cloPath);
        const Obj obj = readObj(folder + "/" + clo.object);
        if (clo.object.empty() || obj.vertices.size() != clo.mappings.size()) {
            error = "MHCLO/OBJ vertex mismatch for " + key;
            return false;
        }
        HairStyle style;
        style.number = number;
        style.key = key;
        style.texture = root + "/web/assets/human/hair-" + key + ".png";
        style.receded = number == 8;
        std::set<int> required;
        std::map<std::pair<int, int>, int> unique;
        for (const auto& face : obj.faces) {
            std::array<int, 3> triangle;
            for (int cornerIndex = 0; cornerIndex < 3; ++cornerIndex) {
                const auto cornerValue = face[cornerIndex];
                auto found = unique.find(cornerValue);
                if (found == unique.end()) {
                    const int index = static_cast<int>(style.vertices.size());
                    found = unique.emplace(cornerValue, index).first;
                    HairVertex vertex;
                    const Mapping& mapping = clo.mappings[cornerValue.first];
                    vertex.refs = mapping.refs;
                    vertex.weights = mapping.weights;
                    vertex.offset = mapping.offset;
                    if (cornerValue.second >= 0 && static_cast<std::size_t>(cornerValue.second) < obj.uvs.size()) {
                        vertex.uv = obj.uvs[cornerValue.second];
                    }
                    for (int ref : vertex.refs) required.insert(ref);
                    style.vertices.push_back(vertex);
                }
                triangle[cornerIndex] = found->second;
            }
            style.triangles.push_back(triangle);
        }
        const std::string axes[] = {"x_scale", "y_scale", "z_scale"};
        for (int axis = 0; axis < 3; ++axis) {
            const auto found = clo.scales.find(axes[axis]);
            if (found == clo.scales.end()) continue;
            style.scaleValid[axis] = true;
            style.scaleRefs[axis] = {static_cast<int>(found->second[0]), static_cast<int>(found->second[1])};
            style.scaleDefaults[axis] = found->second[2];
            required.insert(style.scaleRefs[axis][0]);
            required.insert(style.scaleRefs[axis][1]);
        }
        style.required.assign(required.begin(), required.end());
        styles_.push_back(std::move(style));
    }
    return true;
}

const HairStyle* HairLibrary::find(int number) const {
    for (const auto& style : styles_) if (style.number == number) return &style;
    return nullptr;
}

} // namespace gtc::native
