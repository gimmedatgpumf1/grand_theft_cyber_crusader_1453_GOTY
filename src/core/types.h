#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float scale) const { return {x * scale, y * scale}; }
    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }
};

inline float length(const Vec2& value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

inline Vec2 normalized(const Vec2& value) {
    const float magnitude = length(value);
    return magnitude > 0.0001F ? value * (1.0F / magnitude) : Vec2{};
}

inline float distance(const Vec2& a, const Vec2& b) {
    return length(a - b);
}

struct Obstacle {
    Vec2 center;
    Vec2 halfSize;
    float height = 3.0F;
    std::string kind;
};

struct Needs {
    float bladder = 65.0F;
    float bowel = 40.0F;
};

enum class ToiletAction { None, Sit, Shit, Piss };

struct Appearance {
    bool male = true;
    int skin = 1;
    int portrait = 0;
    int hair = 2;
    int hairColor = 0;
    int shirt = 0;
    int pants = 0;
    int beard = 0;
    bool coat = false;
    bool sunglasses = false;
    int eyeColor = 2;
    int makeup = 0;
    int lipColor = 0;
    bool suit = false;
    int tie = 0;
    bool dress = false;
    bool beret = false;
    bool earrings = false;
    bool necklace = false;
    bool fur = false;
    std::array<int, 51> morphs{};
};

struct Character {
    std::uint32_t id = 0;
    std::string name;
    std::string color = "#d7ff54";
    bool ai = false;
    Vec2 position;
    Vec2 manualMove;
    float yaw = 3.14159265F;
    float pitch = 0.0F;
    float speed = 0.0F;
    bool sprint = false;
    bool wantsPiss = false;
    bool pissing = false;
    bool onToilet = false;
    ToiletAction toiletAction = ToiletAction::None;
    ToiletAction queuedToiletAction = ToiletAction::None;
    bool pendingToilet = false;
    Appearance appearance;
    Needs needs;
    std::vector<Vec2> path;
    float actionTime = 0.0F;
    float puddleTimer = 0.0F;
    float aiTimer = 0.0F;
    std::uint32_t noticeSequence = 0;
    std::string notice;
};

struct Puddle {
    std::uint32_t id = 0;
    Vec2 position;
    float size = 0.25F;
};

struct GameEvent {
    std::uint32_t id = 0;
    std::string text;
};

} // namespace gtc
