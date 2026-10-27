#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc {

inline constexpr float kSlowWalkSpeed = 4.0F / 3.6F;
inline constexpr float kFastWalkSpeed = 6.4F / 3.6F;
inline constexpr float kSprintSpeed = 20.0F / 3.6F;
inline constexpr float kCrouchSpeed = 3.6F / 3.6F;
inline constexpr float kProneSpeed = 2.0F / 3.6F;
inline constexpr float kJumpHeight = 0.5F;
inline constexpr float kMaximumJumpHeight = 1.0F;
inline constexpr float kAutomaticStepHeight = 0.27F;
inline constexpr float kStandingHeight = 1.7F;
inline constexpr float kCrouchingHeight = 1.1F;
inline constexpr float kProneHeight = 0.45F;
// Kohler Wellworth K-3987: 368 mm rim, 381 mm seat, 743 mm tank,
// 705 mm depth, 457 mm width, and water 133 mm below the rim.
inline constexpr float kToiletSeatForwardOffset = 0.06F;
inline constexpr float kToiletRimHeight = 0.368F;
inline constexpr float kToiletSeatHeight = 0.381F;
inline constexpr float kToiletWaterHeight = kToiletRimHeight - 0.133F;
inline constexpr float kToiletTankHeight = 0.743F;
inline constexpr float kPissSpeed = 6.2F;
inline constexpr float kBulletSpeed = 32.0F;
inline constexpr float kProjectileGravity = 9.81F;
inline constexpr float kPissLifetime = 4.0F;
inline constexpr float kPissEmissionInterval = 1.0F / 30.0F;
inline constexpr float kBladderDrainPerSecond = 75.0F / 21.0F;
inline constexpr float kGameTimeScale = 15.0F;
inline constexpr float kPuddleLifetimeSeconds = 24.0F * 60.0F * 60.0F / kGameTimeScale;

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
    float base = 0.0F;
};

struct Needs {
    float bladder = 65.0F;
    float bowel = 40.0F;
};

enum class ToiletAction { None, Sit, Shit, Piss };

struct Appearance {
    bool male = true;
    int skin = 1;
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
    std::array<int, 128> morphs{};
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
    float elevation = 0.0F;
    float verticalVelocity = 0.0F;
    float charge = 0.0F;
    float fullChargeTime = 0.0F;
    float grabHeight = 0.0F;
    float health = 100.0F;
    float attackAnim = 0.0F;
    float attackCooldown = 0.0F;
    bool grounded = true;
    bool crouched = false;
    bool prone = false;
    bool grabbing = false;
    bool unzipped = false;
    bool blocking = false;
    bool pathSprint = false;
    bool pathAdvanced = false;
    std::uint32_t pendingAttack = 0;
    bool sprint = false;
    bool slowWalk = false;
    bool wantsPiss = false;
    bool crouchInput = false;
    bool grabInput = false;
    bool jumpInput = false;
    bool jumpHeld = false;
    bool chargeLocked = false;
    int grabbedObstacle = -1;
    bool pissing = false;
    bool onToilet = false;
    ToiletAction toiletAction = ToiletAction::None;
    ToiletAction queuedToiletAction = ToiletAction::None;
    bool pendingToilet = false;
    Appearance appearance;
    Needs needs;
    std::vector<Vec2> path;
    float actionTime = 0.0F;
    float projectileTimer = 0.0F;
    float aiTimer = 0.0F;
    std::uint32_t noticeSequence = 0;
    std::string notice;
};

enum class ProjectileKind { Piss, Bullet };

struct Projectile {
    std::uint32_t id = 0;
    std::uint32_t owner = 0;
    ProjectileKind kind = ProjectileKind::Piss;
    Vec2 position;
    Vec2 velocity;
    float height = 0.0F;
    float verticalVelocity = 0.0F;
    float age = 0.0F;
    bool stain = false;
    bool toilet = false;
};

struct Puddle {
    std::uint32_t id = 0;
    Vec2 position;
    float size = 0.12F;
    float height = 0.018F;
    Vec2 normal;
    bool vertical = false;
    double age = 0.0;
};

struct GameEvent {
    std::uint32_t id = 0;
    std::string text;
};

} // namespace gtc
