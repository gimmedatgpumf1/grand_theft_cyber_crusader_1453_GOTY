// Check the game's actual test-room scene: female stays in source pose while
// the male remains animated. This exercises the GPU scene path without relying
// on a screenshot or claiming the fake callback renders anything.
#include "client/skeleton_room.h"
#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace gtc::native;
static std::vector<std::array<double, 16>> female, male;
static bool capture(const GpuScene& scene, NativeFrame&) {
    female.clear(); male.clear();
    for (const auto& batch : scene.batches) {
        if (batch.objectId >= 9001 && batch.objectId < 10000) male.push_back(batch.modelMatrix);
        else if (batch.objectId >= 8001 && batch.objectId < 9000) female.push_back(batch.modelMatrix);
    }
    return true;
}

int main() {
    SkeletonRoom room;
    std::string error;
    if (!room.load(".", error)) { std::printf("room load failed: %s\n", error.c_str()); return 1; }
    setGpuRenderer(&capture);
    ViewState view;
    view.gpu = true;
    view.width = 640; view.height = 480;
    NativeFrame frame;
    room.render(frame, view, 0.75);
    const auto firstFemale = female, firstMale = male;
    room.render(frame, view, 1.75);
    const double c = std::cos(3.14159265), s = std::sin(3.14159265);
    const std::array<double, 16> placement{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0.52, 0, 0, 1};
    bool unposed = true;
    for (const auto& matrix : firstFemale) {
        for (std::size_t i = 0; i < matrix.size(); ++i) {
            if (std::abs(matrix[i] - placement[i]) > 1e-12) unposed = false;
        }
    }
    const bool pass = firstFemale.size() == 27 && firstMale.size() == 28 && unposed &&
                      firstFemale == female && firstMale != male;
    setGpuRenderer(nullptr);
    std::printf("test room source pose: %s (female %zu groups, male %zu groups)\n",
                pass ? "PASS" : "FAIL", female.size(), male.size());
    return pass ? 0 : 1;
}
