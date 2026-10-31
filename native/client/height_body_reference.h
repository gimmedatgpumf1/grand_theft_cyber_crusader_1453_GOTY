#pragma once

// Height-only research estimates of five differently defined surface landmarks.
// This is not a skeletal scaling system or a diagnostic tool. See
// tools/skeleton-source/anthropometry/WHOLE_BODY_HEIGHT_COMPARISON.md.
#include <algorithm>
#include <cmath>
#include <optional>

namespace gtc::native {
enum class BodyReferenceSex { male, female };
struct HeightBodyReference {
    double headCm;  // unverified Korean dataset's vertical-head dimension
    double neckCm;  // interpolated Fichera neck-link predictions at BMI 25
    double torsoCm; // standing vertical cervicale-to-crotch
    double armCm;   // acromion-stylion plus stylion-middle finger
    double legCm;   // standing vertical trochanterion-to-floor
};

namespace height_body_detail {
// Forward straight-line fit on its sample's observed range. Outside that range
// use the value/slope-matched bounded dimension/stature fraction continuation.
// This continuation is a modelling convenience, not extrapolated medical data.
inline std::optional<double> line(double h, double a, double b, double lo, double hi) {
    const double x=std::clamp(h,lo,hi), y=a+b*x;
    if (!(y>0 && y<x)) return std::nullopt;
    double v=y;
    if (h!=x) {
        const double r=y/x, exponent=(x*b/y-1)/(1-r);
        const double z=std::log(r)-std::log1p(-r)+exponent*(std::log(h)-std::log(x));
        const double fraction=z>=0 ? 1/(1+std::exp(-z)) : std::exp(z)/(1+std::exp(z));
        v=h*fraction;
    }
    return std::isfinite(v) && v>0 && v<h ? std::optional<double>(v) : std::nullopt;
}
// Connect a 39-week neonatal limb ratio to the adult fit continuously; the
// interval is an unvalidated bridge, not a paediatric regression.
inline std::optional<double> infantBridge(double h, double ratio, double adultStart,
                                          std::optional<double> adultValue) {
    if (!adultValue) return std::nullopt;
    if (h<=50) return h*ratio;
    if (h>=adultStart) return adultValue;
    const double t=(h-50)/(adultStart-50), w=t*t*t*(t*(6*t-15)+10);
    const double v=(1-w)*h*ratio+w*(*adultValue);
    return std::isfinite(v) && v>0 && v<h ? std::optional<double>(v) : std::nullopt;
}
} // namespace height_body_detail

inline std::optional<HeightBodyReference> estimateHeightBodyReference(BodyReferenceSex sex, double h) {
    if (!std::isfinite(h) || h<=0 || (sex!=BodyReferenceSex::male && sex!=BodyReferenceSex::female))
        return std::nullopt;
    using height_body_detail::line;
    const bool male=sex==BodyReferenceSex::male;
    const auto head=male ? line(h,9.519390,0.079034,154.7,187.7) :
                           line(h,11.177137,0.063486,142.3,172.6);
    // Approximated from rounded 5th/50th/95th percentile table; these are BMI 25
    // fixed predictions, not a new sex-specific fit to the original data.
    const auto neck=male ? line(h,11.2-(2.3/31)*178,2.3/31,163,194) :
                           line(h,10.8-(1.9/28)*165,1.9/28,151,179);
    const auto torso=male ? line(h,9.824626889078571,0.3263433653317256,149.1,199.3) :
                            line(h,8.831677988700205,0.32241520461333356,140.9,182.9);
    const auto armAdult=male ? line(h,-1.944431609432535,0.45885024218626663,149.1,199.3) :
                               line(h,-3.3344315178830297,0.465454824499171,140.9,182.9);
    const auto legAdult=male ? line(h,-16.99483649702122,0.6097644465657162,149.1,199.3) :
                               line(h,-12.69145825107915,0.5970413778726937,140.9,182.9);
    const auto arm=height_body_detail::infantBridge(h,male ? 0.4286 : 0.4281,
                                                     male ? 149.1 : 140.9,armAdult);
    // Neonatal study ends at the malleolus, not the floor. Add a separately
    // extrapolated adult ankle-to-floor estimate at 50 cm; this is a design
    // assumption, not an observed neonatal whole-leg measurement.
    const double infantLegRatio=male ? 0.4102+2.291499801728912/50 :
                                       0.4150+2.1843400961893407/50;
    const auto leg=height_body_detail::infantBridge(h,infantLegRatio,
                                                     male ? 149.1 : 140.9,legAdult);
    if (!head || !neck || !torso || !arm || !leg) return std::nullopt;
    return HeightBodyReference{*head,*neck,*torso,*arm,*leg};
}
} // namespace gtc::native
