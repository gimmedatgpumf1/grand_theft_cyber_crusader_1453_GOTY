#include "client/body_proportion_curves.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <limits>
#include <string>

int main() {
    using namespace gtc::native;
    BodyProportionCurves curves; std::string error;
    assert(curves.load("tools/skeleton-source/anthropometry/BODY_PART_CURVE_KNOTS.tsv",error));
    const BodyProportionCurves::Key child{"NHANES_III","male","under_18","sitting_height"};
    assert(std::abs(*curves.dimensionCm(child,100)-55.049386632275706)<1e-8);
    assert(!curves.blendedNhanesDimensionCm("male","sitting_height",100));
    assert(curves.loadSelectedModels("tools/skeleton-source/anthropometry/BODY_PART_SELECTED_MODELS.tsv",error));
    for (const std::string sex:{"male","female"}) {
        for (const std::string dim:{"sitting_height","height_minus_sitting","upper_arm_surface",
                                    "upper_leg_surface","shoulder_breadth","pelvis_surface_breadth"}) {
            auto at=[&](double h) { const auto v=curves.blendedNhanesDimensionCm(sex,dim,h); assert(v); return *v; };
            assert(std::abs(*curves.blendedNhanesScale(sex,dim,170,170)-1)<1e-12);
            double previous=0;
            for (int i=1;i<=2000;++i) {
                const double h=i*0.5, current=at(h);
                assert(current>previous && current<h);
                previous=current;
            }
            // A single quadratic is used across the former child/adult joins,
            // so its third finite difference vanishes.
            for (double h:{60.,95.,140.,170.,185.,210.,235.}) {
                const double d=2;
                assert(std::abs(at(h+3*d)-3*at(h+2*d)+3*at(h+d)-at(h))<1e-9);
            }
            for (double h:{50.,100.,145.,175.,190.,250.}) {
                const double eps=1e-5;
                assert(std::abs(at(std::nextafter(h,0))-at(std::nextafter(h,INFINITY)))<1e-9);
                assert(std::abs((at(h+eps)-at(h))/eps-(at(h)-at(h-eps))/eps)<1e-3);
                const double d=0.01;
                const double left=(at(h)-2*at(h-d)+at(h-2*d))/(d*d);
                const double right=(at(h+2*d)-2*at(h+d)+at(h))/(d*d);
                assert(std::abs(left-right)<0.001);
            }
            for (double h=50;h<=250;h+=0.25) {
                const double d=0.25;
                if (h>50 && h<250)
                    assert(std::abs(at(h+d)-2*at(h)+at(h-d))/(d*d)<0.03);
                const auto scale=curves.blendedNhanesScale(sex,dim,h,170);
                assert(scale && *scale>0);
            }
        }
        for (double h:{0.5,50.,100.,175.,250.,1000.}) {
            const auto sitting=curves.blendedNhanesDimensionCm(sex,"sitting_height",h);
            const auto rest=curves.blendedNhanesDimensionCm(sex,"height_minus_sitting",h);
            assert(sitting && rest && *sitting+*rest==h);
        }
    }
    // Every selected source/sex/dimension continues at 2 m and beyond.
    std::ifstream models("tools/skeleton-source/anthropometry/BODY_PART_SELECTED_MODELS.tsv");
    std::string line; assert(std::getline(models,line)); int count=0;
    while (std::getline(models,line)) {
        std::istringstream row(line); BodyProportionCurves::Key k;
        assert(row>>k.source>>k.sex>>k.ageGroup>>k.dimension); ++count;
        for (double height : {20.0,50.0,100.0,200.0,250.0,500.0}) {
            const auto pchip=curves.dimensionCm(k,height);
            const auto fitted=curves.selectedDimensionCm(k,height);
            assert(pchip && fitted && *pchip>0 && *pchip<height && *fitted>0 && *fitted<height);
        }
    }
    assert(count==52);
    // Both selected-family and PCHIP tails meet their first and last knots.
    std::ifstream knots("tools/skeleton-source/anthropometry/BODY_PART_CURVE_KNOTS.tsv");
    assert(std::getline(knots,line));
    std::map<BodyProportionCurves::Key,std::pair<double,double>> ends;
    while (std::getline(knots,line)) {
        std::istringstream row(line); BodyProportionCurves::Key k;
        std::string column; double bin=0,n=0,x=0,y=0;
        assert(row>>k.source>>k.sex>>k.ageGroup>>k.dimension>>column>>bin>>n>>x>>y);
        auto pair=ends.emplace(k,std::make_pair(x,x));
        pair.first->second.first=std::min(pair.first->second.first,x);
        pair.first->second.second=std::max(pair.first->second.second,x);
    }
    assert(ends.size()==52);
    for (const auto& group:ends) for (double edge:{group.second.first,group.second.second}) {
        for (int selectedMethod=0;selectedMethod<2;++selectedMethod) {
            const auto calc=[&](double x) { return selectedMethod ? curves.selectedDimensionCm(group.first,x) : curves.dimensionCm(group.first,x); };
            const double eps=1e-5;
            const auto a=calc(edge-eps),b=calc(edge),c=calc(edge+eps);
            assert(a && b && c);
            assert(std::abs(*c-*a)<0.001);
            assert(std::abs((*c-*b)/eps-(*b-*a)/eps)<0.001);
            const double d=0.002;
            const double leftCurvature=(*calc(edge)-2*(*calc(edge-d))+*calc(edge-2*d))/(d*d);
            const double rightCurvature=(*calc(edge+2*d)-2*(*calc(edge+d))+*calc(edge))/(d*d);
            assert(std::abs(leftCurvature-rightCurvature)<0.002);
        }
    }
    assert(!curves.blendedNhanesDimensionCm("other","sitting_height",170));
    assert(!curves.blendedNhanesDimensionCm("male","head_height",170));
    assert(!curves.blendedNhanesDimensionCm("male","sitting_height",0));
    assert(!curves.blendedNhanesDimensionCm("male","sitting_height",std::numeric_limits<double>::quiet_NaN()));
    assert(!curves.loadSelectedModels("does-not-exist.tsv",error));
    assert(curves.selectedDimensionCm(child,100));
    assert(!curves.load("does-not-exist.tsv",error));
    assert(curves.dimensionCm(child,100));
}
