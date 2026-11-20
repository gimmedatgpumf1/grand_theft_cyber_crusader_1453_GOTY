#pragma once
#include <array>
#include <cstdint>
namespace gtc::native::makehuman_foot_data {
struct Vertex {
    float x,y,z;
    std::uint8_t influence;
    std::array<std::uint8_t,4> bones;
    std::array<float,4> weights;
};
}
