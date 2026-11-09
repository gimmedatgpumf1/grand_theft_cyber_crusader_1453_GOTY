#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gtc::native::gpu_physics {
// std430 layouts shared by the planar solver, matrix conversion, and host.
struct alignas(16) Body {
    std::array<float,4> positionMass{}; // position xyz, inverse mass
    std::array<float,4> rotation{0,0,0,1};
    std::array<float,4> linearVelocity{};
    std::array<float,4> angularVelocity{};
    std::array<float,4> inverseInertia{};
    std::array<std::uint32_t,4> shapes{}; // offset, vertex count, CCW convex polygon
    std::array<float,4> material{}; // friction, depth, pressure-drag coefficient, restitution
};
struct alignas(16) Joint {
    std::array<std::uint32_t,4> bodies{};
    std::array<float,4> parentAnchor{};
    std::array<float,4> childAnchor{};
};
struct alignas(16) SkinBinding {
    std::array<float,16> localFromMesh{};
    std::array<std::uint32_t,4> indices{};
};
struct FluidEnvironment {
    // Liquid is below the horizontal level. With density zero, only air is present.
    std::array<float,4> medium{1.225f,0,-10000,0}; // kg/m³, kg/m³, metres
    std::array<float,4> velocity{}; // air xy and liquid xy in m/s
};
struct alignas(16) Step {
    std::uint32_t phase=0, first=0, count=0;
    float dt=0;
    std::array<float,4> gravityFloor{0,-9.81f,0,0};
    std::array<float,4> dragOrigin{};
    std::array<float,4> dragDirection{};
    std::array<std::uint32_t,4> dragControl{};
    std::array<float,4> medium{};
    std::array<float,4> fluidVelocity{};
};
static_assert(sizeof(Body)==112 && sizeof(Joint)==48 && sizeof(SkinBinding)==80 && sizeof(Step)==112);
static_assert(offsetof(Body,shapes)==80 && offsetof(Step,gravityFloor)==16);
} // namespace gtc::native::gpu_physics
