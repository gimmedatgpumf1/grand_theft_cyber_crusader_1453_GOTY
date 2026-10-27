#include "native.h"

#include <iomanip>
#include <sstream>

namespace gtc::native {
namespace {

const char* kNames[] = {"marilyn", "ibrahim", "xi", "putin", "trump", "taylor", "obama"};

std::vector<Vec2> normalizePoints(const std::vector<Vec2>& points) {
    std::vector<Vec2> result = points;
    if (points.size() < 468) return result;
    const Vec2 midpoint{(points[33].x + points[263].x) * 0.5, (points[33].y + points[263].y) * 0.5};
    const double ex = points[263].x - points[33].x;
    const double ey = points[263].y - points[33].y;
    const double scale = std::max(1e-12, std::hypot(ex, ey));
    const double angle = -std::atan2(ey, ex);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    for (auto& point : result) {
        const double x = point.x - midpoint.x;
        const double y = point.y - midpoint.y;
        point = {(c * x - s * y) / scale, (s * x + c * y) / scale};
    }
    return result;
}

} // namespace

std::vector<Preset> loadPresets(const std::string& root, const HumanModel& model, std::string& error) {
    bool ok = false;
    const gtc::JsonValue all = gtc::JsonValue::parse(readText(root + "/native/presets.json", ok), ok);
    if (!ok) {
        error = "Cannot parse native/presets.json";
        return {};
    }
    std::vector<Preset> result;
    for (const char* name : kNames) {
        const gtc::JsonValue* entry = all.get(name);
        if (!entry) continue;
        Preset preset;
        preset.key = name;
        preset.male = entry->boolAt("male", true);
        preset.skin = static_cast<int>(entry->numberAt("skin", 1));
        preset.hair = static_cast<int>(entry->numberAt("hair", 2));
        preset.hairColor = static_cast<int>(entry->numberAt("hairColor", 0));
        preset.eyeColor = static_cast<int>(entry->numberAt("eyeColor", 2));
        preset.makeup = static_cast<int>(entry->numberAt("makeup", 0));
        preset.lipColor = static_cast<int>(entry->numberAt("lipColor", 0));
        preset.pose.yaw = entry->numberAt("yaw", 0);
        preset.pose.pitch = entry->numberAt("pitch", 0);
        preset.pose.roll = entry->numberAt("roll", 0);
        preset.morphs.assign(model.specs.size(), 0);
        const gtc::JsonValue* morphs = entry->get("morphs");
        if (!morphs || morphs->array().size() != model.specs.size()) {
            error = std::string("Wrong universal morph count for ") + name;
            return {};
        }
        for (std::size_t i = 0; i < model.specs.size(); ++i) preset.morphs[i] = morphs->array()[i].number();
        result.push_back(std::move(preset));
    }
    if (result.size() != 7) error = "Expected seven native portrait presets";
    return result;
}

std::unordered_map<std::string, Reference> loadReferences(const std::string& root, std::string& error) {
    bool ok = false;
    const gtc::JsonValue all = gtc::JsonValue::parse(
        readText(root + "/tools/human-source/portrait-fitting/reference-landmarks.json", ok), ok);
    if (!ok) {
        error = "Cannot parse reference landmarks";
        return {};
    }
    std::unordered_map<std::string, Reference> result;
    for (const char* name : kNames) {
        const gtc::JsonValue* entry = all.get(name);
        const gtc::JsonValue* points = entry ? entry->get("points") : nullptr;
        if (!points || points->array().size() < 468) continue;
        Reference reference;
        reference.key = name;
        for (const auto& point : points->array()) {
            const auto& coordinates = point.array();
            if (coordinates.size() < 2) break;
            reference.points.push_back({coordinates[0].number(), coordinates[1].number()});
        }
        if (reference.points.size() < 468) continue;
        reference.normalized = normalizePoints(reference.points);
        result.emplace(name, std::move(reference));
    }
    if (result.size() != 7) error = "Expected seven reference landmark sets";
    return result;
}

std::string presetsJson(const std::vector<Preset>& presets) {
    std::ostringstream output;
    output << std::setprecision(10) << "{\n";
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const Preset& preset = presets[i];
        output << "  \"" << gtc::jsonEscape(preset.key) << "\": {";
        output << "\"male\":" << (preset.male ? "true" : "false")
               << ",\"skin\":" << preset.skin << ",\"hair\":" << preset.hair
               << ",\"hairColor\":" << preset.hairColor << ",\"eyeColor\":" << preset.eyeColor
               << ",\"makeup\":" << preset.makeup << ",\"lipColor\":" << preset.lipColor
               << ",\"yaw\":" << preset.pose.yaw << ",\"pitch\":" << preset.pose.pitch
               << ",\"roll\":" << preset.pose.roll << ",\"morphs\":[";
        for (std::size_t j = 0; j < preset.morphs.size(); ++j) {
            if (j) output << ',';
            output << preset.morphs[j];
        }
        output << "]}" << (i + 1 == presets.size() ? "\n" : ",\n");
    }
    output << "}\n";
    return output.str();
}

} // namespace gtc::native
