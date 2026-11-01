#pragma once

#include <array>
#include <cmath>
#include <optional>

namespace gtc::native {
// User-supplied comparison targets. Height and all results are in centimetres.
// These are visual targets, not a clinically validated anatomical model.
inline std::optional<std::array<double,7>> femaleComparisonFormulas(double height) {
    if (!std::isfinite(height) || height<=0) return std::nullopt;
    const std::array<double,7> result{
        0.0568*height+11.832,
        0.0213*height+7.1832,
        0.3446*height+4.5758,
        0.4734*height-1.3187,
        0.568*height-6.9588,
        6.7179*std::exp(0.0089*height),
        10.87*std::exp(0.0075*height)
    };
    for (double value:result) if (!std::isfinite(value) || value<=0) return std::nullopt;
    return result;
}
} // namespace gtc::native
