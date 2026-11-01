#include "client/height_body_reference.h"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace gtc::native;
    for (auto sex:{BodyReferenceSex::male,BodyReferenceSex::female}) {
        for (auto field:{&HeightBodyReference::headCm,&HeightBodyReference::neckCm,
                         &HeightBodyReference::torsoCm,&HeightBodyReference::armCm,&HeightBodyReference::legCm}) {
            auto at=[&](double h) { const auto v=estimateHeightBodyReference(sex,h); assert(v); return v.value().*field; };
            double previous=0;
            for (int i=1;i<=2000;++i) {
                const double h=i*0.5, current=at(h);
                assert(std::isfinite(current) && current>previous && current<h);
                previous=current;
            }
            for (double h:{60.,95.,140.,170.,185.,210.,235.}) {
                const double d=2;
                assert(std::abs(at(h+4*d)-4*at(h+3*d)+6*at(h+2*d)-4*at(h+d)+at(h))<1e-9);
            }
            for (double h:{50.,100.,140.9,149.1,175.,190.,199.3,250.}) {
                const double eps=1e-5;
                assert(std::abs(at(std::nextafter(h,0))-at(std::nextafter(h,INFINITY)))<1e-9);
                assert(std::abs((at(h+eps)-at(h))/eps-(at(h)-at(h-eps))/eps)<1e-3);
                const double d=0.01;
                const double left=(at(h)-2*at(h-d)+at(h-2*d))/(d*d);
                const double right=(at(h+2*d)-2*at(h+d)+at(h))/(d*d);
                assert(std::abs(left-right)<0.001);
            }
            for (double h=50.25;h<250;h+=0.25) {
                const double d=.25;
                assert(std::abs(at(h+d)-2*at(h)+at(h-d))/(d*d)<0.03);
            }
        }
    }
    const auto m=estimateHeightBodyReference(BodyReferenceSex::male,50);
    const auto f=estimateHeightBodyReference(BodyReferenceSex::female,50);
    assert(m && f && std::abs(m->headCm-f->headCm)>0.01 && std::abs(m->armCm-f->armCm)>0.01);
    const auto adultM=estimateHeightBodyReference(BodyReferenceSex::male,170);
    const auto adultF=estimateHeightBodyReference(BodyReferenceSex::female,170);
    assert(adultM && adultF && std::abs(adultM->armCm-adultF->armCm)>0.01);
    assert(!estimateHeightBodyReference(BodyReferenceSex::male,0));
    assert(!estimateHeightBodyReference(BodyReferenceSex::male,std::numeric_limits<double>::quiet_NaN()));
}
