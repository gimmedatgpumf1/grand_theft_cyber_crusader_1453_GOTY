#include "client/body_proportion_curves.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

int main() {
    using gtc::native::BodyProportionCurves;
    BodyProportionCurves curves; std::string error;
    assert(curves.load("tools/skeleton-source/anthropometry/BODY_PART_CURVE_KNOTS.tsv",error));
    const BodyProportionCurves::Key sitting{"NHANES_III","male","under_18","sitting_height"};
    auto value=curves.dimensionCm(sitting,100.0);
    assert(value && std::abs(*value-55.049386632275706)<1e-8);
    assert(curves.dimensionCm(sitting,125.0));
    assert(!curves.dimensionCm(sitting,250.0));
    assert(!curves.dimensionCm(sitting,std::numeric_limits<double>::quiet_NaN()));
    assert(!curves.dimensionCm({"NHANES_III","male","under_18","head_height"},100.0));
    const BodyProportionCurves::Key leg{"NHANES_III","male","under_18","height_minus_sitting"};
    auto legValue=curves.dimensionCm(leg,100.0);
    assert(legValue && std::abs(*value+*legValue-100.0)<1e-10);
    assert(!curves.load("does-not-exist.tsv",error));
    assert(curves.dimensionCm(sitting,100.0)); // failed reload leaves current curves intact
}
