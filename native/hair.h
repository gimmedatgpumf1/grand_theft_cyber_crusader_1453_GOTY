#pragma once

#include "native.h"

#include <array>
#include <map>

namespace gtc::native {

struct HairVertex {
    std::array<int, 3> refs = {0, 0, 0};
    std::array<double, 3> weights = {1, 0, 0};
    Vec3 offset;
    Vec2 uv;
};

struct HairStyle {
    int number = 0;
    std::string key;
    std::string texture;
    bool receded = false;
    std::vector<HairVertex> vertices;
    std::vector<std::array<int, 3>> triangles;
    std::vector<int> required;
    std::array<std::array<int, 2>, 3> scaleRefs = {};
    std::array<double, 3> scaleDefaults = {1, 1, 1};
    std::array<bool, 3> scaleValid = {false, false, false};
};

class HairLibrary {
public:
    bool load(const std::string& root, const HumanModel& model, std::string& error);
    const HairStyle* find(int number) const;
    const std::vector<HairStyle>& styles() const { return styles_; }

private:
    std::vector<HairStyle> styles_;
};

} // namespace gtc::native
