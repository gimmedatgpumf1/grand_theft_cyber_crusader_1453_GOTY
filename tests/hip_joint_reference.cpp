#include "client/hip_joint_reference.h"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    using namespace gtc::native;
    const auto h=haraHipOffsetMm(800); assert(h);
    assert(std::abs(h->anterior-(-39.4))<1e-10);
    assert(std::abs(h->lateral-76.8)<1e-10);
    assert(std::abs(h->superior-(-71.4))<1e-10);
    assert(std::abs(*haraInterHipLateralMm(800,800)-153.6)<1e-10);
    assert(std::abs(*haraInterHipLateralMm(790,810)-153.6)<1e-10);
    assert(!haraHipOffsetMm(0));
    assert(!haraHipOffsetMm(-1));
    assert(!haraHipOffsetMm(std::numeric_limits<double>::quiet_NaN()));
    assert(!haraInterHipLateralMm(800,std::numeric_limits<double>::infinity()));
}
