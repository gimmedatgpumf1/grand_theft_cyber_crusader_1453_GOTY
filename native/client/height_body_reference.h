#pragma once

// Height-only visual estimates using unmatched paediatric/adult surface proxies.
// These are not verified head or bone dimensions, or a diagnostic tool. See
// tools/skeleton-source/anthropometry/POOLED_HEIGHT_MODELS.md.
#include "client/pooled_height_models.h"
#include <optional>

namespace gtc::native {
enum class BodyReferenceSex { male, female };
struct HeightBodyReference {
    double headCm;  // head-circumference proxy, calibrated to Korean group mean
    double neckCm;  // child head-circumference proxy / adult tragion-to-cervicale
    double torsoCm; // child sitting-height proxy / adult cervicale-to-crotch
    double armCm;   // child upper-arm proxy / adult acromion to middle finger
    double legCm;   // child subischial proxy / adult trochanterion-to-floor
};

inline std::optional<HeightBodyReference> estimateHeightBodyReference(BodyReferenceSex sex, double h) {
    if (sex!=BodyReferenceSex::male && sex!=BodyReferenceSex::female) return std::nullopt;
    const bool male=sex==BodyReferenceSex::male;
    const auto head=pooledHeightDimension(male,PooledHeightDimension::head,h);
    const auto neck=pooledHeightDimension(male,PooledHeightDimension::neck,h);
    const auto torso=pooledHeightDimension(male,PooledHeightDimension::torso,h);
    const auto arm=pooledHeightDimension(male,PooledHeightDimension::arm,h);
    const auto leg=pooledHeightDimension(male,PooledHeightDimension::leg,h);
    if (!head || !neck || !torso || !arm || !leg) return std::nullopt;
    return HeightBodyReference{*head,*neck,*torso,*arm,*leg};
}
} // namespace gtc::native
