#pragma once

// Published exploratory anthropometric reference, not an automatic rig fit.
// Hara et al. 2016; see anthropometry/GIRDLE_WIDTH_FORMULAS.md.
#include <cmath>
#include <optional>

namespace gtc::native {

struct HaraHipOffsetMm {
    double anterior;   // relative to inter-ASIS midpoint, negative = posterior
    double lateral;    // distance to the subject's lateral side
    double superior;   // relative to inter-ASIS midpoint, negative = inferior
};

// Clinical leg length runs from ASIS to medial malleolus through the medial
// femoral epicondyle. It is not stature, femur+tibia length, or height minus
// sitting height. Both input and result are millimetres. This reference is
// descriptive; it neither locates a mesh's ASIS nor changes a skeleton.
inline std::optional<HaraHipOffsetMm> haraHipOffsetMm(double clinicalLegLengthMm) {
    if (!std::isfinite(clinicalLegLengthMm) || clinicalLegLengthMm<=0) return std::nullopt;
    const HaraHipOffsetMm v{11-0.063*clinicalLegLengthMm,
                            8+0.086*clinicalLegLengthMm,
                            -9-0.078*clinicalLegLengthMm};
    if (!std::isfinite(v.anterior) || !std::isfinite(v.lateral) || !std::isfinite(v.superior))
        return std::nullopt;
    return v;
}

// Lateral component of inter-centre distance in the same pelvis frame,
// assuming mirror symmetry of the regression and known left/right leg lengths.
// It is not the Euclidean 3-D distance if the two centres differ in other axes.
inline std::optional<double> haraInterHipLateralMm(double leftClinicalLegMm,
                                                  double rightClinicalLegMm) {
    const auto l=haraHipOffsetMm(leftClinicalLegMm), r=haraHipOffsetMm(rightClinicalLegMm);
    if (!l || !r) return std::nullopt;
    const double width=l->lateral+r->lateral;
    return std::isfinite(width) ? std::optional<double>(width) : std::nullopt;
}

} // namespace gtc::native
