#include "client/newborn_limb_profile.h"
#include <cassert>
#include <cmath>
#include <string>
#include <limits>

int main() {
    using gtc::native::NewbornLimbProfile;
    const std::string data="tools/skeleton-source/anthropometry/NEWBORN_SEGMENT_RATIO_REFERENCES.tsv";
    NewbornLimbProfile p; std::string error;
    assert(p.load(data,NewbornLimbProfile::Sex::male,error));
    const auto m=p.estimate(50.0); assert(m);
    assert(std::abs(m->upperArmCm-9.50)<1e-10);
    assert(std::abs(m->forearmCm-7.94)<1e-10);
    assert(std::abs(m->handCm-6.73)<1e-10);
    assert(std::abs(m->thighCm-8.825)<1e-10);
    assert(std::abs(m->lowerLegCm-9.785)<1e-10);
    assert(std::abs(m->footCm-7.98)<1e-10);
    assert(!p.estimate(47.99) && !p.estimate(52.01));
    assert(p.estimate(48.0) && p.estimate(52.0));
    assert(!p.estimate(std::numeric_limits<double>::quiet_NaN()));
    assert(!p.estimate(std::numeric_limits<double>::infinity()));
    assert(!p.load("missing.tsv",NewbornLimbProfile::Sex::female,error));
    assert(p.estimate(50.0)->handCm==m->handCm);
    assert(p.load(data,NewbornLimbProfile::Sex::female,error));
    const auto f=p.estimate(50.0); assert(f);
    assert(std::abs(f->upperArmCm-9.54)<1e-10);
    assert(std::abs(f->forearmCm-7.815)<1e-10);
    assert(std::abs(f->handCm-6.735)<1e-10);
    assert(std::abs(f->thighCm-8.855)<1e-10);
    assert(std::abs(f->lowerLegCm-9.695)<1e-10);
    assert(std::abs(f->footCm-8.00)<1e-10);
}
