#include "client/height_body_reference.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>
#include <utility>
int main() {
    using namespace gtc::native;
    const auto m=estimateHeightBodyReference(BodyReferenceSex::male,170);
    const auto f=estimateHeightBodyReference(BodyReferenceSex::female,170.2);
    assert(m && f);
    assert(std::abs(m->neckCm-10.362830908355386)<1e-9);
    assert(std::abs(f->neckCm-11.110833494879504)<1e-9);
    assert(std::abs(m->armCm-76.0601095622328)<1e-9);
    assert(std::abs(f->legCm-88.92498426285331)<1e-9);
    for (auto sex:{BodyReferenceSex::male,BodyReferenceSex::female}) {
        for (double h:{20.,50.,100.,140.,150.,170.,200.,250.,500.}) {
            const auto v=estimateHeightBodyReference(sex,h); assert(v);
            for (double x:{v->headCm,v->neckCm,v->torsoCm,v->armCm,v->legCm})
                assert(std::isfinite(x) && x>0 && x<h);
        }
        for (double h:{50.,140.9,142.3,149.1,151.,154.7,163.,172.6,179.,182.9,187.7,194.,199.3}) {
            auto l=estimateHeightBodyReference(sex,h-1e-5), r=estimateHeightBodyReference(sex,h+1e-5);
            assert(l && r);
            assert(std::abs(l->headCm-r->headCm)<1e-3);
            assert(std::abs(l->neckCm-r->neckCm)<1e-3);
            assert(std::abs(l->torsoCm-r->torsoCm)<1e-3);
            assert(std::abs(l->armCm-r->armCm)<1e-3);
            assert(std::abs(l->legCm-r->legCm)<1e-3);
            const double d=0.01;
            const auto before2=estimateHeightBodyReference(sex,h-2*d);
            const auto before=estimateHeightBodyReference(sex,h-d);
            const auto centre=estimateHeightBodyReference(sex,h);
            const auto after=estimateHeightBodyReference(sex,h+d);
            const auto after2=estimateHeightBodyReference(sex,h+2*d);
            assert(before2 && before && centre && after && after2);
            for (auto field:{&HeightBodyReference::headCm,&HeightBodyReference::neckCm,
                             &HeightBodyReference::torsoCm,&HeightBodyReference::armCm,&HeightBodyReference::legCm}) {
                const double left=(centre.value().*field-2*(before.value().*field)+before2.value().*field)/(d*d);
                const double right=(after2.value().*field-2*(after.value().*field)+centre.value().*field)/(d*d);
                assert(std::abs(left-right)<0.001);
            }
        }
    }
    const auto babyM=estimateHeightBodyReference(BodyReferenceSex::male,50);
    const auto babyF=estimateHeightBodyReference(BodyReferenceSex::female,50);
    assert(babyM && babyF);
    assert(std::abs(babyM->armCm-21.43)<1e-10 && std::abs(babyM->legCm-22.801499801728912)<1e-10);
    assert(std::abs(babyF->armCm-21.405)<1e-10 && std::abs(babyF->legCm-22.93434009618934)<1e-10);
    for (auto sex:{BodyReferenceSex::male,BodyReferenceSex::female}) {
        auto previous=estimateHeightBodyReference(sex,50); assert(previous);
        for (int i=1; i<=200000; ++i) {
            const auto current=estimateHeightBodyReference(sex,50+i*0.001); assert(current);
            for (auto pair:{std::pair<double,double>{previous->headCm,current->headCm},
                           {previous->neckCm,current->neckCm},{previous->torsoCm,current->torsoCm},
                           {previous->armCm,current->armCm},{previous->legCm,current->legCm}}) {
                assert(pair.second>=pair.first && pair.second-pair.first<0.002);
            }
            previous=current;
        }
    }
    assert(!estimateHeightBodyReference(BodyReferenceSex::male,0));
    assert(!estimateHeightBodyReference(BodyReferenceSex::male,std::numeric_limits<double>::quiet_NaN()));
}
