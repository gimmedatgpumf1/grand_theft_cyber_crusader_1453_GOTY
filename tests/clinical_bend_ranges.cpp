#include "client/skeleton_rom.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace gtc::native;
int main() {
    const auto knee = referenceBendRange("knee_modified_hinge");
    assert(knee && knee->extension == 0 && knee->flexion == 135);
    assert(*clampReferenceBend("knee_modified_hinge", 200) == 135);
    assert(*clampReferenceBend("knee_modified_hinge", -20) == 0);
    assert(*clampReferenceBend("elbow_hinge", 170) == 150);
    assert(*clampReferenceBend("finger_pip_hinge", 130) == 100);
    assert(*clampReferenceBend("wrist_condyloid", -90) == -70);
    assert(*clampReferenceBend("hip_ball_socket", 150) == 120);
    assert(*clampReferenceBend("ankle_hinge", -90) == -20);
    assert(*clampReferenceBend("shoulder_ball_socket", 180) == 120);
    assert(*clampReferenceBend("toe_ip_hinge", 90, "FootProxP2_L", "FootMidP2_L") == 35);
    assert(*clampReferenceBend("toe_ip_hinge", 90, "FootMidP2_L", "FootDistP2_L") == 60);
    assert(*clampReferenceBend("toe_ip_hinge", 90, "FootProxP1_L", "FootDistP1_L") == 50);
    assert(!clampReferenceBend("toe_ip_hinge", 90));
    assert(!clampReferenceBend("costal_cartilage", 1));
    assert(!clampReferenceBend("not_an_anatomical_joint", 1));
    assert(!clampReferenceBend("elbow_hinge", std::numeric_limits<double>::quiet_NaN()));
    assert(!clampReferenceBend("knee_modified_hinge", std::numeric_limits<double>::infinity()));
    std::cout << "Clinical reference bending limits: PASS (not individual ROM)\n";
}
