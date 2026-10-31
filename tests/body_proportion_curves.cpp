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
    // The interpolation must reproduce a straight line and remain smooth at
    // every knot, including the shorter final female interval.
    gtc::native::SmoothHeightCurve linear;
    assert(linear.build(175,[](double h){return std::optional<double>(3+h*0.25);}));
    for (double h=100;h<=175;h+=0.25) {
        const auto v=linear.evaluate(h); assert(v);
        assert(std::abs(v->value-(3+h*0.25))<1e-9);
        assert(std::abs(v->slope-0.25)<1e-9 && std::abs(v->curvature)<1e-9);
    }
    assert(!linear.evaluate(99));
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
        for (const std::string dim : {"sitting_height", "height_minus_sitting", "upper_arm_surface",
                                      "upper_leg_surface", "shoulder_breadth", "pelvis_surface_breadth"}) {
            const double low=100.0, high=sex=="male" ? 190.0 : 175.0;
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
                // Unlike the old infant cubic bridge, the new joins also
                // match curvature rather than changing acceleration abruptly.
                const double d=0.01;
                const auto at=[&](double h){return *curves.blendedNhanesDimensionCm(sex,dim,h);};
                const double leftCurvature=(at(boundary)-2*at(boundary-d)+at(boundary-2*d))/(d*d);
                const double rightCurvature=(at(boundary+2*d)-2*at(boundary+d)+at(boundary))/(d*d);
                assert(std::abs(leftCurvature-rightCurvature)<0.001);
            }
            // Check C2 at the child smoothing knots too, including the
            // weighted age-group transition rather than only its ends.
            for (double knot=110;knot<high;knot+=10) {
                const double d=0.01;
                const auto at=[&](double h){return *curves.blendedNhanesDimensionCm(sex,dim,h);};
                const double a=(at(knot)-2*at(knot-d)+at(knot-2*d))/(d*d);
                const double b=(at(knot+2*d)-2*at(knot+d)+at(knot))/(d*d);
                assert(std::abs(a-b)<0.001);
            }
            double last=*curves.blendedNhanesDimensionCm(sex,dim,low);
            const int steps=static_cast<int>((high-low)*1000);
            for (int i=1;i<=steps;++i) {
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
            // No blocked height cutoffs and no backwards growth over the
            // sampled 0.5–1000 cm range, including both extrapolated tails.
            double previous=0;
            for (int i=1;i<=2000;++i) {
                const double h=i*0.5;
                const auto current=curves.blendedNhanesDimensionCm(sex,dim,h);
                assert(current && *current>previous && *current<h);
                previous=*current;
            }
            last=*curves.blendedNhanesDimensionCm(sex,dim,50);
            for (int i=1;i<=5000;++i) {
                const auto value=curves.blendedNhanesDimensionCm(sex,dim,50+i*0.01);
                assert(value && *value>=last && *value-last<0.1);
                last=*value;
            }
        }
        const double middle=sex=="male" ? 167.5 : 152.5;
        const auto s=curves.blendedNhanesDimensionCm(sex,"sitting_height",middle);
        const auto l=curves.blendedNhanesDimensionCm(sex,"height_minus_sitting",middle);
        assert(s && l && *s+*l==middle);
        // Compare the growth rates over the entire range, not only continuity
        // at the endpoints. The former 15 cm blend produced conspicuous spikes
        // in both widths, sitting height and upper-arm reference length.
        for (const std::string dim:{"pelvis_surface_breadth","shoulder_breadth","sitting_height","upper_arm_surface"}) {
            const BodyProportionCurves::Key child{"NHANES_III",sex,"under_18",dim};
            const BodyProportionCurves::Key adult{"NHANES_III",sex,"adult_18_plus",dim};
            const double oldLow=sex=="male" ? 160.0 : 145.0, oldHigh=oldLow+15;
            auto oldValue=[&](double h) {
                const double t=std::clamp((h-oldLow)/(oldHigh-oldLow),0.0,1.0);
                const double w=t*t*t*(10+t*(-15+6*t));
                return *curves.selectedDimensionCm(child,h)*(1-w)+*curves.selectedDimensionCm(adult,h)*w;
            };
            double oldPeak=0,newPeak=0;
            for (int i=10001;i<19999;++i) {
                const double h=i*0.01, delta=0.001;
                const double oldSlope=(oldValue(h+delta)-oldValue(h-delta))/(2*delta);
                const double newSlope=(*curves.blendedNhanesDimensionCm(sex,dim,h+delta)-
                                       *curves.blendedNhanesDimensionCm(sex,dim,h-delta))/(2*delta);
                assert(newSlope>=0);
                oldPeak=std::max(oldPeak,oldSlope); newPeak=std::max(newPeak,newSlope);
            }
            const double limit=dim=="pelvis_surface_breadth" ? 0.70 :
                               dim=="sitting_height" ? 0.85 : 0.80;
            assert(newPeak<limit*oldPeak);
            // Compare curvature against the previous broad blend of the raw
            // PCHIP model. Bound changes in dimension as well as rate spikes.
            const double high=sex=="male" ? 190.0 : 175.0;
            auto prior=[&](double h) {
                const double t=(h-100)/(high-100), w=t*t*t*(10+t*(-15+6*t));
                return *curves.selectedDimensionCm(child,h)*(1-w)+*curves.selectedDimensionCm(adult,h)*w;
            };
            double oldCurvature=0,newCurvature=0,maxDifference=0;
            constexpr double step=0.25;
            for (double h=100+step;h<high;h+=step) {
                const auto current=[&](double x){return *curves.blendedNhanesDimensionCm(sex,dim,x);};
                oldCurvature=std::max(oldCurvature,std::abs(prior(h+step)-2*prior(h)+prior(h-step))/(step*step));
                newCurvature=std::max(newCurvature,std::abs(current(h+step)-2*current(h)+current(h-step))/(step*step));
                maxDifference=std::max(maxDifference,std::abs(current(h)-prior(h)));
            }
            if (dim!="upper_arm_surface" || sex=="female") assert(newCurvature<0.65*oldCurvature);
            assert(maxDifference<0.30);
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
    assert(!curves.blendedNhanesDimensionCm("other","upper_arm_surface",160));
    assert(!curves.blendedNhanesDimensionCm("male","hand_surface",160));
    assert(!curves.loadSelectedModels("does-not-exist.tsv",error));
    assert(curves.selectedDimensionCm(sitting,100.0));
    assert(!curves.load("does-not-exist.tsv",error));
    assert(curves.dimensionCm(sitting,100.0)); // failed reload leaves current curves intact
}
