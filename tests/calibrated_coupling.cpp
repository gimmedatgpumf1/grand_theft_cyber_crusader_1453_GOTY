// Checks the reference shoulder/wrist couplers against OpenSim, not the
// uncalibrated OpenSkeleton subjects.
#include "client/skeleton_coupling.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
using namespace gtc::native;

int main() {
    std::ifstream file("tests/aah_coupling_cases.txt");
    assert(file);
    std::string line, name, driver, dependent;
    int n = 0, count = 0, samples = 0;
    std::vector<CouplingKnot> knots;
    std::optional<CalibratedJointCoupling> coupling;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string type; in >> type;
        if (type == "coupling") {
            assert(!coupling);
            in >> name >> driver >> dependent >> n;
            assert(in && n >= 2);
            knots.clear(); ++count;
        } else if (type == "knot") {
            CouplingKnot k;
            in >> k.x >> k.y >> k.slope >> k.quadratic >> k.cubic;
            assert(in); knots.push_back(k);
        } else if (type == "sample") {
            if (!coupling) {
                assert(static_cast<int>(knots.size()) == n);
                coupling = CalibratedJointCoupling::create(knots);
                assert(coupling);
            }
            double x, expected;
            in >> x >> expected; assert(in);
            const auto y = coupling->value(x);
            if (!y || std::abs(*y-expected) > 1e-9) {
                std::cerr << "OpenSim coupling mismatch: " << name << " at " << x << '\n';
                return 1;
            }
            ++samples;
        } else if (type == "end") {
            assert(coupling);
            assert(!coupling->value(std::numeric_limits<double>::quiet_NaN()));
            assert(!coupling->value(std::numeric_limits<double>::infinity()));
            coupling.reset();
        } else assert(false);
    }
    assert(count == 26 && samples == 234 && !coupling);
    assert(!CalibratedJointCoupling::create({}));
    assert(!CalibratedJointCoupling::create({{0,0,1,0,0}, {0,0,1,0,0}}));
    assert(!CalibratedJointCoupling::create({{0,0,1,0,0}, {1,2,1,0,0}}));
    assert(!CalibratedJointCoupling::create({{0,0,1,0,0}, {1,1,2,0,0}}));
    assert(!CalibratedJointCoupling::create({{0,0,1,0,0}, {1,1,std::numeric_limits<double>::infinity(),0,0}}));
    std::cout << "AAH reference shoulder/wrist coupling: PASS (26 splines, 234 OpenSim values; not fitted to OpenSkeleton)\n";
}
