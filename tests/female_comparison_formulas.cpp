#include "client/female_comparison_formulas.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using gtc::native::femaleComparisonFormulas;
    for (double h:{20.,40.,50.,100.,170.,230.,250.,400.}) {
        const auto values=femaleComparisonFormulas(h);
        assert(values);
        const std::array<double,7> expected{
            .0568*h+11.832, .0213*h+7.1832, .3446*h+4.5758,
            .4734*h-1.3187, .568*h-6.9588,
            6.7179*std::exp(.0089*h), 10.87*std::exp(.0075*h)};
        for (std::size_t i=0;i<expected.size();++i)
            assert(std::abs((*values)[i]-expected[i])<1e-11);
    }
    const auto shortBody=femaleComparisonFormulas(50.);
    const auto adult=femaleComparisonFormulas(170.);
    const std::array<double,7> shortExpected{14.672,8.2482,21.8058,22.3513,21.4412,10.483217086584272,15.815756676899847};
    const std::array<double,7> adultExpected{21.488,10.8042,63.1578,79.1593,89.6012,30.501492160042954,38.900484327804165};
    for (std::size_t i=0;i<shortExpected.size();++i) {
        assert(std::abs((*shortBody)[i]-shortExpected[i])<1e-10);
        assert(std::abs((*adult)[i]-adultExpected[i])<1e-10);
    }
    // The leg request is non-positive at or below 6.9588/0.568 cm;
    // exponentials can also overflow for extreme inputs.
    assert(!femaleComparisonFormulas(0));
    assert(!femaleComparisonFormulas(12.));
    assert(femaleComparisonFormulas(12.3));
    assert(!femaleComparisonFormulas(std::numeric_limits<double>::infinity()));
    assert(!femaleComparisonFormulas(std::numeric_limits<double>::quiet_NaN()));
    assert(!femaleComparisonFormulas(1e308));
}
