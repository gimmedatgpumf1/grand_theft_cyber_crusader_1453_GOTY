#include "client/body_proportion_curves.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>

int main() {
    using gtc::native::BodyProportionCurves;
    BodyProportionCurves curves; std::string error;
    assert(curves.load("tools/skeleton-source/anthropometry/BODY_PART_CURVE_KNOTS.tsv",error));
    const BodyProportionCurves::Key sitting{"NHANES_III","male","under_18","sitting_height"};
    auto value=curves.dimensionCm(sitting,100.0);
    assert(value && std::abs(*value-55.049386632275706)<1e-8);
    assert(curves.dimensionCm(sitting,125.0));
    assert(curves.dimensionCm(sitting,250.0));
    assert(!curves.dimensionCm(sitting,std::numeric_limits<double>::quiet_NaN()));
    assert(!curves.dimensionCm({"NHANES_III","male","under_18","head_height"},100.0));
    const BodyProportionCurves::Key leg{"NHANES_III","male","under_18","height_minus_sitting"};
    auto legValue=curves.dimensionCm(leg,100.0);
    assert(legValue && std::abs(*value+*legValue-100.0)<1e-10);
    assert(curves.loadSelectedModels("tools/skeleton-source/anthropometry/BODY_PART_SELECTED_MODELS.tsv",error));
    const auto selected=curves.selectedDimensionCm(sitting,100.0);
    const auto selectedLeg=curves.selectedDimensionCm(leg,100.0);
    assert(selected && selectedLeg && std::abs(*selected+*selectedLeg-100.0)<1e-10);
    assert(curves.selectedDimensionCm(sitting,50.0));
    assert(curves.selectedDimensionCm(sitting,250.0));
    // Check every supported blended dimension at both transitions, including
    // adjacent representable heights and a 0.01 mm sweep across each blend.
    for (const std::string sex : {"male", "female"}) {
        const double low=sex=="male" ? 160.0 : 145.0;
        const double high=low+15.0;
        for (const std::string dim : {"sitting_height", "height_minus_sitting", "upper_arm_surface",
                                      "upper_leg_surface", "shoulder_breadth", "pelvis_surface_breadth"}) {
            const BodyProportionCurves::Key child{"NHANES_III",sex,"under_18",dim};
            const BodyProportionCurves::Key adult{"NHANES_III",sex,"adult_18_plus",dim};
            assert(curves.blendedNhanesDimensionCm(sex,dim,low)==curves.selectedDimensionCm(child,low));
            assert(curves.blendedNhanesDimensionCm(sex,dim,high)==curves.selectedDimensionCm(adult,high));
            assert(std::abs(*curves.blendedNhanesScale(sex,dim,170,170)-1)<1e-12);
            for (const double boundary : {50.0,100.0,low,high}) {
                auto left=curves.blendedNhanesDimensionCm(sex,dim,std::nextafter(boundary,-std::numeric_limits<double>::infinity()));
                auto right=curves.blendedNhanesDimensionCm(sex,dim,std::nextafter(boundary,std::numeric_limits<double>::infinity()));
                assert(left && right && std::abs(*right-*left)<1e-10);
                const double eps=1e-4;
                const double a=*curves.blendedNhanesDimensionCm(sex,dim,boundary-eps);
                const double b=*curves.blendedNhanesDimensionCm(sex,dim,boundary);
                const double c=*curves.blendedNhanesDimensionCm(sex,dim,boundary+eps);
                assert(std::abs((c-b)/eps-(b-a)/eps)<5e-4);
            }
            double last=*curves.blendedNhanesDimensionCm(sex,dim,low);
            for (int i=1;i<=15000;++i) {
                const double h=low+i*0.001;
                const auto current=curves.blendedNhanesDimensionCm(sex,dim,h);
                assert(current && *current>=last && *current-last<0.1);
                const auto scale=curves.blendedNhanesScale(sex,dim,h,170);
                assert(scale && *scale>0);
                last=*current;
            }
            for (double height : {20.0,50.0,100.0,200.0,250.0,500.0}) {
                const auto extrapolated=curves.blendedNhanesDimensionCm(sex,dim,height);
                assert(extrapolated && *extrapolated>0 && *extrapolated<height);
            }
            last=*curves.blendedNhanesDimensionCm(sex,dim,50);
            for (int i=1;i<=5000;++i) {
                const auto value=curves.blendedNhanesDimensionCm(sex,dim,50+i*0.01);
                assert(value && *value>=last && *value-last<0.1);
                last=*value;
            }
        }
        const auto s=curves.blendedNhanesDimensionCm(sex,"sitting_height",(low+high)/2);
        const auto l=curves.blendedNhanesDimensionCm(sex,"height_minus_sitting",(low+high)/2);
        assert(s && l && *s+*l==(low+high)/2);
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
        }
    }
    assert(!curves.blendedNhanesDimensionCm("other","upper_arm_surface",160));
    assert(!curves.blendedNhanesDimensionCm("male","hand_surface",160));
    assert(!curves.loadSelectedModels("does-not-exist.tsv",error));
    assert(curves.selectedDimensionCm(sitting,100.0));
    assert(!curves.load("does-not-exist.tsv",error));
    assert(curves.dimensionCm(sitting,100.0)); // failed reload leaves current curves intact
}
