#pragma once

#include "client/renderer.h"

namespace gtc::native {

// Third-person middle-mouse orbit only previews the camera. Walking, body
// yaw, pitch, and crosshair aim keep the exact controls captured on press.
struct OrbitPreview {
    bool active = false;
    bool wasCaptured = false;
    float yaw = 0.0F;
    float pitch = 0.0F;

    bool begin(const ViewState& view, bool captured) {
        if (active || view.mode != ViewMode::ThirdPerson) return false;
        active = true;
        yaw = view.yaw;
        pitch = view.pitch;
        wasCaptured = captured;
        return !captured;
    }

    bool end(ViewState& view) {
        if (!active) return false;
        active = false;
        view.yaw = yaw;
        view.pitch = pitch;
        view.orbitPreview = false;
        return !wasCaptured;
    }

    ViewState controls(const ViewState& view) const {
        ViewState control = view;
        if (active) {
            control.yaw = yaw;
            control.pitch = pitch;
        }
        control.orbitPreview = false;
        return control;
    }
};

} // namespace gtc::native
