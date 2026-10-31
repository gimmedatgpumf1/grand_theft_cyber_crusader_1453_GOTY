#pragma once

// Single pooled cubic (or degree-elevated quadratic) per sex and dimension.
// Coefficients are fitted jointly; there is no age-dependent formula blend.
// Pooled surface proxies and their non-clinical limitations are documented in
// tools/skeleton-source/anthropometry/POOLED_HEIGHT_MODELS.md.
#include "client/pooled_height_coefficients.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace gtc::native {
enum class PooledHeightDimension {
    sitting, upperArm, upperLeg, shoulder, pelvis, head, neck, torso, arm, leg
};

inline std::optional<double> pooledHeightDimension(bool male, PooledHeightDimension dimension, double height) {
    if (!std::isfinite(height) || height<=0) return std::nullopt;
    const int index=static_cast<int>(dimension);
    if (index<0 || index>=10) return std::nullopt;
    const auto& c=pooled_height_data::coefficients[male ? 0 : 1][index];
    const double x=std::clamp(height,50.0,250.0);
    const double t=(x-50)/200, u=1-t;
    const double value=c[0]*u*u*u+3*c[1]*t*u*u+3*c[2]*t*t*u+c[3]*t*t*t;
    const double slope=3*((c[1]-c[0])*u*u+2*(c[2]-c[1])*t*u+(c[3]-c[2])*t*t)/200;
    const double curvature=6*((c[2]-2*c[1]+c[0])*u+(c[3]-2*c[2]+c[1])*t)/(200*200);
    double result=value;
    if (height<50) {
        // Value/slope/curvature matching at 50, tending to a fixed positive
        // fraction of stature towards zero. This is not infant data.
        const double r=value/x, k=(x*slope/value-1)/(1-r);
        const double z=std::log(height)-std::log(x);
        const double q=0.5*(curvature*x/(r*(1-r))-k-(1-2*r)*k*k);
        const double distance=(height-x)/(0.15*x);
        const double logit=std::log(r)-std::log1p(-r)+(k*z+q*z*z)*std::exp(-distance*distance);
        const double fraction=logit>=0 ? 1/(1+std::exp(-logit)) : std::exp(logit)/(1+std::exp(logit));
        result=height*fraction;
    } else if (height>250) {
        // Continue with a positive, bounded asymptotic slope. The decaying
        // curvature matches the polynomial at 250 rather than adding a jump.
        const double width=std::min(0.15*x,0.25*std::min(slope,1-slope)/(std::abs(curvature)+1e-15));
        const double distance=height-x;
        if (!(width>0)) return std::nullopt;
        result=value+slope*distance+curvature*width*(distance+width*std::expm1(-distance/width));
    }
    if (!std::isfinite(result) || result<=0 || result>=height) return std::nullopt;
    return result;
}
} // namespace gtc::native
