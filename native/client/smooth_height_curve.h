#pragma once

// Natural cubic approximation of an existing reference, not new clinical data.
// Samples are precomputed at load time; evaluation is C2 between samples.
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace gtc::native {
// Continue a dimension/stature fraction outside observed bounds. The local
// correction matches curvature as well as value and slope, and fades away from
// the boundary. It remains a bounded extrapolation, not observed medical data.
inline std::optional<double> smoothBoundedContinuation(double h,double x,double y,double slope,double curvature) {
    if (!std::isfinite(h) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(slope) ||
        !std::isfinite(curvature) || h<=0 || x<=0 || y<=0 || y>=x) return std::nullopt;
    const double r=y/x, k=(x*slope/y-1)/(1-r);
    const double z=std::log(h)-std::log(x);
    const double c=(curvature*x/(r*(1-r))-k-(1-2*r)*k*k)*0.5;
    const double distance=(h-x)/(0.15*x);
    const double logit=std::log(r)-std::log1p(-r)+k*z+c*z*z*std::exp(-distance*distance);
    const double fraction=logit>=0 ? 1/(1+std::exp(-logit)) : std::exp(logit)/(1+std::exp(logit));
    const double result=h*fraction;
    if (!std::isfinite(result) || result<=0 || result>=h) return std::nullopt;
    return result;
}

struct SmoothHeightCurve {
    struct Evaluation { double value, slope, curvature; };
    std::vector<double> x, y, second;

    bool build(double upper, const std::function<std::optional<double>(double)>& reference) {
        x.clear(); y.clear(); second.clear();
        if (!std::isfinite(upper) || upper<=100) return false;
        for (double h=100;h<upper;h+=10) x.push_back(h);
        x.push_back(upper);
        for (double h:x) {
            const auto v=reference(h);
            if (!v || !std::isfinite(*v)) return false;
            y.push_back(*v);
        }
        const std::size_t n=x.size();
        if (n<2) return false;
        second.assign(n,0);
        std::vector<double> diagonal(n), right(n);
        for (std::size_t i=1;i+1<n;++i) {
            const double left=x[i]-x[i-1], next=x[i+1]-x[i];
            const double pivot=2*(left+next)-left*diagonal[i-1];
            diagonal[i]=next/pivot;
            right[i]=(6*((y[i+1]-y[i])/next-(y[i]-y[i-1])/left)-left*right[i-1])/pivot;
        }
        for (std::size_t i=n-1;i-->1;) second[i]=right[i]-diagonal[i]*second[i+1];
        return true;
    }

    std::optional<Evaluation> evaluate(double h) const {
        if (x.size()<2 || !std::isfinite(h) || h<x.front() || h>x.back()) return std::nullopt;
        const auto it=std::upper_bound(x.begin(),x.end(),h);
        const std::size_t i=it==x.end() ? x.size()-2 : static_cast<std::size_t>(it-x.begin()-1);
        const double width=x[i+1]-x[i], a=(x[i+1]-h)/width, b=(h-x[i])/width;
        const double value=a*y[i]+b*y[i+1]+((a*a*a-a)*second[i]+(b*b*b-b)*second[i+1])*width*width/6;
        const double slope=(y[i+1]-y[i])/width+width*((1-3*a*a)*second[i]+(3*b*b-1)*second[i+1])/6;
        const double curvature=a*second[i]+b*second[i+1];
        if (!std::isfinite(value) || !std::isfinite(slope) || !std::isfinite(curvature)) return std::nullopt;
        return Evaluation{value,slope,curvature};
    }
};
} // namespace gtc::native
