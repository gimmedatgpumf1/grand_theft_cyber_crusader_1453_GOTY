// The scenes the benchmark draws: one skull, two whole bodies, and one body on
// its own -- each built exactly the way the game builds it -- plus the turning
// and collapsing that let the same scene be measured posed, spun or empty.
//
// They were in an anonymous namespace in benchmark.cpp. They are here because
// benchmark.cpp measures them and this file builds them, and a scene builder is
// long enough on its own to be worth reading without the timing around it.

#ifndef GTC_CLIENT_BENCHMARK_SCENE_H
#define GTC_CLIENT_BENCHMARK_SCENE_H

#include "client/renderer.h"
#include "client/skeleton_body.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gtc::native::benchmark_detail {
struct SkullScene {
    gtc::native::GpuScene scene;
    gtc::native::Camera camera;
    bool ready = false;
    // Both bodies are emitted once, in rest position, one batch per bone -- which
    // is how the test room and the world renderer hold a character. A frame then
    // places each bone with a matrix, so a body turning is its yaw changing and
    // not its geometry being rebuilt. `rest` says which of the two kinds of scene
    // this is; the skull is drawn posed and is turned about the origin instead.
    gtc::native::SkeletonModel male;
    gtc::native::SkeletonModel female;
    bool rest = false;
    std::vector<std::pair<std::uint32_t, std::size_t>> boneBatches;
};

inline constexpr double kMaleX = -0.52;
inline constexpr double kFemaleX = 0.52;
inline constexpr double kFaceCamera = 3.141592653589793;
inline constexpr std::uint32_t kMaleId = 9001;
inline constexpr std::uint32_t kFemaleId = 8001;
// A quarter of a second a frame's worth of rotation at three hundred frames a
// second, so the measurement spans a full turn and every frame sees a side of
// the body the one before it did not.
inline constexpr double kTurnPerFrame = 6.283185307179586 / 240.0;

bool buildSkull(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
                std::string& error);

bool buildBody(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
               std::string& error);

bool buildWholeBody(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
                    std::string& error);

std::array<double, 16> turnAbout(double radians, double pivotX);

void posePair(SkullScene& bodies, double turn);

void turnScene(const GpuScene& base, GpuScene& turned, double radians);

void collapseTriangles(gtc::native::GpuScene& scene);

}  // namespace gtc::native::benchmark_detail

#endif
