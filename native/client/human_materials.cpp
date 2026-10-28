// Skin and fabric surfaces for the imported body.

#include "client/human_materials.h"

#include "client/human_palette.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace gtc::native {

const Texture& agedSkinTexture(const std::string& root, const std::string& sex, int age, bool normal) {
    const double old = std::clamp((age - 24) / 58.0, 0.0, 1.0);
    const double blend = old * old * (3 - 2 * old) * (normal ? 1.0 : 0.94);
    const std::string key = root + sex + std::to_string(age) + (normal ? "normal" : "detail");
    static std::map<std::string, Texture> cache;
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    const std::string base = root + "/web/assets/human/skin-" + sex;
    const Texture& young = loadTexture(base + (normal ? "-young-normal.png" : "-young-detail.jpg"));
    const Texture& aged = loadTexture(base + (normal ? "-old-normal.png" : "-old-detail.jpg"));
    if (young.width != aged.width || young.height != aged.height || young.pixels.empty()) return young;
    Texture result;
    result.width = young.width;
    result.height = young.height;
    result.pixels.resize(young.pixels.size());
    for (std::size_t i = 0; i < result.pixels.size(); ++i) {
        result.pixels[i] = static_cast<unsigned char>(std::lround(young.pixels[i] * (1 - blend) + aged.pixels[i] * blend));
    }
    return cache.emplace(key, std::move(result)).first->second;
}

Material skinMaterial(const HumanModel& model, const Character& character, const std::string& root,
                      const std::vector<double>& morphs) {
    Material material;
    material.kind = Material::Kind::Skin;
    material.color = skinColor(character.appearance.skin);
    material.specular = 0.035;
    const std::string sex = model.male ? "male" : "female";
    const int ageIndex = model.specIndex("age");
    const int age = ageIndex >= 0 && static_cast<std::size_t>(ageIndex) < morphs.size() ?
        static_cast<int>(morphs[ageIndex]) : 25;
    material.texture = &agedSkinTexture(root, sex, age, false);
    material.normalYoung = &agedSkinTexture(root, sex, age, true);
    material.age = age;
    return material;
}

const Texture& fabricTexture() {
    static const Texture texture = [] {
        Texture result;
        result.width = result.height = 128;
        result.pixels.resize(128U * 128U * 4U, 255);
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
            const bool vertical = x % 4 == 0, horizontal = y % 4 == 0;
            const bool dark = (vertical && x % 8) || (horizontal && y % 8);
            const unsigned char value = (vertical || horizontal) ? (dark ? 237 : 245) : 244;
            const std::size_t offset = (static_cast<std::size_t>(y) * 128 + x) * 4;
            result.pixels[offset] = result.pixels[offset + 1] = result.pixels[offset + 2] = value;
        }
        return result;
    }();
    return texture;
}

}  // namespace gtc::native
