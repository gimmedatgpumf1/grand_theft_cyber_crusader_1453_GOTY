#pragma once
#include <array>
namespace gtc::native::makehuman_foot_data {
struct ReducedVertex {
    std::array<int,3> source;
    std::array<float,3> barycentric;
    std::array<float,3> offset;
};
}
