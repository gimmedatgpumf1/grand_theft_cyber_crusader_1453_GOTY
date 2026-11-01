#pragma once

// Piecewise-cubic relationship between a calibrated independent coordinate
// and one dependent coordinate. Coefficients must come from the source model;
// this does not infer subject-specific scapular motion or enforce contact.
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace gtc::native {
struct CouplingKnot {
    double x = 0, y = 0, slope = 0, quadratic = 0, cubic = 0;
};
class CalibratedJointCoupling {
public:
    static std::optional<CalibratedJointCoupling> create(std::vector<CouplingKnot> knots) {
        if (knots.size() < 2) return std::nullopt;
        for (std::size_t i = 0; i < knots.size(); ++i) {
            const auto& k = knots[i];
            if (!std::isfinite(k.x) || !std::isfinite(k.y) || !std::isfinite(k.slope) ||
                !std::isfinite(k.quadratic) || !std::isfinite(k.cubic)) return std::nullopt;
            if (!i) continue;
            const auto& a = knots[i-1];
            const double dx = k.x - a.x;
            if (!(dx > 0) || !std::isfinite(dx)) return std::nullopt;
            const double y = a.y + dx*(a.slope + dx*(a.quadratic + dx*a.cubic));
            const double slope = a.slope + dx*(2*a.quadratic + dx*3*a.cubic);
            if (!std::isfinite(y) || !std::isfinite(slope) || std::abs(y-k.y) > 1e-8 ||
                std::abs(slope-k.slope) > 1e-8) return std::nullopt;
        }
        return CalibratedJointCoupling(std::move(knots));
    }
    // SimmSpline extends linearly using the endpoint slope outside its knots.
    // Driver-coordinate limits should be applied before evaluating this curve.
    std::optional<double> value(double x) const {
        if (!std::isfinite(x)) return std::nullopt;
        const auto& first = knots_.front();
        const auto& last = knots_.back();
        double result;
        if (x < first.x) result = first.y + (x-first.x)*first.slope;
        else if (x > last.x) result = last.y + (x-last.x)*last.slope;
        else {
            const auto next = std::upper_bound(knots_.begin(), knots_.end(), x,
                [](double q, const CouplingKnot& k) { return q < k.x; });
            const auto& k = next == knots_.begin() ? first : *(next-1);
            const double dx = x-k.x;
            result = k.y + dx*(k.slope + dx*(k.quadratic + dx*k.cubic));
        }
        if (!std::isfinite(result)) return std::nullopt;
        return result;
    }
private:
    explicit CalibratedJointCoupling(std::vector<CouplingKnot> knots) : knots_(std::move(knots)) {}
    std::vector<CouplingKnot> knots_;
};
} // namespace gtc::native
