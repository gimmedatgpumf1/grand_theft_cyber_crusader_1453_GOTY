#include "client/male_comparison_formulas.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using gtc::native::maleComparisonFormulas;
    for (double h:{20.,40.,50.,100.,170.,230.,250.,400.}) {
        const auto values=maleComparisonFormulas(h);
        assert(values);
        const std::array<double,7> expected{
            0.0561*h+12.179, .0193*h+7.4136, .3536*h+4.4647,
            .4515*h+1.4121, .5553*h-6.5708,
            7.6737*std::exp(.0075*h), 10.719*std::exp(.0077*h)};
        for (std::size_t i=0;i<expected.size();++i)
            assert(std::abs((*values)[i]-expected[i])<1e-11);
    }
    // Independent sample values guard the new linear head target.
    assert(std::abs((*maleComparisonFormulas(50.))[0]-14.984)<1e-10);
    assert(std::abs((*maleComparisonFormulas(170.))[0]-21.716)<1e-10);
    // The supplied leg equation becomes negative below ~11.83 cm, and the
    // exponentials eventually overflow. Reject those results without changing
    // the equations or introducing an arbitrary stature cutoff.
    assert(!maleComparisonFormulas(0));
    assert(!maleComparisonFormulas(10));
    assert(!maleComparisonFormulas(std::numeric_limits<double>::infinity()));
    assert(!maleComparisonFormulas(std::numeric_limits<double>::quiet_NaN()));
    assert(!maleComparisonFormulas(1e308));
}
