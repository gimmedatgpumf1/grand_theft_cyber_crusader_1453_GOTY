#pragma once

#include "client/client.h"
#include "client/raster.h"
#include "client/human_renderer.h"
#include "native.h"

#include <string>
#include <unordered_map>

namespace gtc::native {

enum class ViewMode { ThirdPerson, FirstPerson, God };

struct ViewState {
    ViewMode mode = ViewMode::ThirdPerson;
    float yaw = 0.0F;
    float pitch = -0.22F;
    float distance = 5.1F;
    float godX = 0.0F;
    float godZ = 0.0F;
    float godYaw = 0.72F;
    float godDistance = 16.0F;
    int width = 480;
    int height = 640;
    double fps = 0.0;
    double frameMilliseconds = 0.0;
    bool gpu = false;
    bool highlightToilet = false;
};

class WorldRenderer {
public:
    bool load(const std::string& root, std::string& error);
    bool render(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                const ViewState& view, NativeFrame& frame, std::string& error) const;
    Camera viewCamera(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                      const ViewState& view) const;
    const std::vector<MorphSpec>& morphSpecs(bool male) const;

private:
    std::string root_;
    bool ready_ = false;
    HumanModel male_;
    HumanModel female_;
    HairLibrary maleHair_;
    HairLibrary femaleHair_;
    mutable std::unordered_map<std::uint32_t, HumanRenderCache> humanCache_;
    mutable GpuScene gpuScene_;
    mutable bool gpuValid_ = false;
    mutable ClientState gpuGeometryState_;
    mutable std::uint64_t gpuRevision_ = 0;
    mutable std::uint32_t gpuLocalId_ = 0;
    mutable int gpuMode_ = -1;
    mutable bool gpuHighlight_ = false;
};

double headAngleForGaze(double gazeAngle);
void rotateView(ViewState& view, double mouseX, double mouseY);
void interpolateCharacters(const ClientState& target, ClientState& current, float dt);

} // namespace gtc::native
