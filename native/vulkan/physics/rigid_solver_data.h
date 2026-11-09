#pragma once

// Host-side layouts for the experimental Vulkan rigid-body solver.
// Keep in step with rigid_solver.comp.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gtc::native::gpu_physics {

struct alignas(16) Body {
    std::array<float,4> positionMass{};
    std::array<float,4> rotation{0,0,0,1};
    std::array<float,4> linearVelocity{};
    std::array<float,4> angularVelocity{}; // w is filled with the sorted-proxy axis by the runtime
    std::array<float,4> inverseInertia{};
    std::array<float,4> previousPosition{};
    std::array<float,4> previousRotation{0,0,0,1};
    std::array<std::uint32_t,4> samples{};
    // Local proxy AABB half-extents and maximum sphere radius, filled by the runtime.
    std::array<float,4> bounds{};
};
struct alignas(16) Joint {
    // bodies.z: 0 = limited ball, 1 = hinge, 2 = positional only.
    // bodies.w: 0 = ordinary joint, 1..3 = toe chain projection depth.
    std::array<std::uint32_t,4> bodies{};
    std::array<float,4> parentAnchor{};
    std::array<float,4> childAnchor{};
    // Axes and tangents are body-local, unit length and perpendicular. The
    // axes' fourth components hold the lower/upper hinge angles in radians.
    std::array<float,4> parentAxis{};
    std::array<float,4> childAxis{};
    std::array<float,4> parentTangent{};
    std::array<float,4> childTangent{};
    // Approximate XYZ angles relative to the calibrated initial ball frame.
    std::array<float,4> lowerAngles{};
    std::array<float,4> upperAngles{};
};
// Shader phase 4 writes one correction per body; phase 5 applies it only
// after a barrier. No body reads another body while it is being modified.
struct alignas(16) ContactDelta {
    std::array<float,4> position{};
    std::array<float,4> rotation{};
};
struct alignas(16) SkinBinding {
    std::array<float,16> localFromMesh{};
    std::array<std::uint32_t,4> indices{}; // body index, renderer matrix slot
};
struct alignas(16) Step {
    std::uint32_t phase=0, first=0, count=0;
    float dt=0;
    std::array<float,4> gravityFloor{0,-9.81f,0,0};
    std::array<float,4> dragOrigin{};
    std::array<float,4> dragDirection{};
    std::array<std::uint32_t,4> dragControl{}; // active, press sequence
    std::array<float,4> damping{};       // free linear/angular, contact linear/angular
    std::array<float,4> floorDamping{};  // floor tangential and angular
};
static_assert(sizeof(Body)==144 && sizeof(Joint)==144 && sizeof(ContactDelta)==32 && sizeof(SkinBinding)==80 && sizeof(Step)==112);
static_assert(offsetof(Body,samples)==112 && offsetof(Step,gravityFloor)==16);

struct JointBatch { std::uint32_t first=0, count=0; };

// Colour once when the rig is built. Every constraint in one colour has
// disjoint endpoints, so its GPU invocations can update both bodies in parallel
// without races or floating-point atomic extensions. Returned batches index the
// reordered joint vector and must execute in order with a barrier between them.
inline bool colourJoints(std::size_t bodyCount, const std::vector<Joint>& input,
                         std::vector<Joint>& ordered, std::vector<JointBatch>& batches) {
    std::vector<std::vector<Joint>> colours;
    std::vector<std::vector<bool>> occupied;
    for (const Joint& joint:input) {
        const auto a=joint.bodies[0], b=joint.bodies[1];
        if (a>=bodyCount || b>=bodyCount || a==b) { ordered.clear(); batches.clear(); return false; }
        std::size_t colour=0;
        while (colour<colours.size() && (occupied[colour][a] || occupied[colour][b])) ++colour;
        if (colour==colours.size()) {
            colours.emplace_back();
            occupied.emplace_back(bodyCount,false);
        }
        colours[colour].push_back(joint);
        occupied[colour][a]=occupied[colour][b]=true;
    }
    ordered.clear(); batches.clear(); ordered.reserve(input.size());
    for (const auto& colour:colours) {
        batches.push_back({static_cast<std::uint32_t>(ordered.size()),static_cast<std::uint32_t>(colour.size())});
        ordered.insert(ordered.end(),colour.begin(),colour.end());
    }
    return true;
}

} // namespace gtc::native::gpu_physics
