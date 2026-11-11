// The core test runner: every group is called from here, in order.
//
// The checks themselves live in the four tests_*.cpp files beside this one. This
// file only decides what order they run in, because "which test failed" is
// easier to answer when the order is one short list in one place.

#include "harness.h"

void tests_core();
void tests_character();
void tests_needs();
void tests_combat();

int main() {
    tests_core();
    tests_character();
    tests_needs();
    tests_combat();
    if (test_support::failures > 0) {
        std::cerr << test_support::failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
