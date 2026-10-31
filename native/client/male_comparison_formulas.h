#pragma once

#include <array>
#include <cmath>
#include <optional>

namespace gtc::native {
// User-supplied comparison targets. Height and all results are in centimetres.
// These are visual targets, not a clinically validated anatomical model.
inline std::optional<std::array<double,7>> maleComparisonFormulas(double height) {
    if (!std::isfinite(height) || height<=0) return std::nullopt;
    const std::array<double,7> result{
        16.273*std::exp(0.0018*height),
        0.0193*height+7.4136,
        0.3536*height+4.4647,
        0.4515*height+1.4121,
        0.5553*height-6.5708,
        7.6737*std::exp(0.0075*height),
        10.719*std::exp(0.0077*height)
    };
    for (double value:result) if (!std::isfinite(value) || value<=0) return std::nullopt;
    return result;
}
} // namespace gtc::native
