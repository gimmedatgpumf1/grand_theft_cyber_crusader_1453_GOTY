#pragma once

#include "client/client.h"
#include "client/raster.h"
#include "hair.h"
#include "native.h"

#include <string>

namespace gtc::native {

enum class ViewMode { ThirdPerson, FirstPerson, God };

struct ViewState {
    ViewMode mode = ViewMode::ThirdPerson;
    float yaw = 0.0F;
    float pitch = -0.22F;
    float godX = 0.0F;
    float godZ = 0.0F;
};

class WorldRenderer {
public:
    bool load(const std::string& root, std::string& error);
    bool render(const ClientWorld& world, const ClientState& state, std::uint32_t localId,
                const ViewState& view, NativeFrame& frame, std::string& error) const;

private:
    std::string root_;
    bool ready_ = false;
    HumanModel male_;
    HumanModel female_;
    HairLibrary maleHair_;
    HairLibrary femaleHair_;
};

} // namespace gtc::native
