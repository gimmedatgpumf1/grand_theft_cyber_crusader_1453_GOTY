#pragma once

#include "client/window.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace gtc::native {

// Independent of the player's view and controls: inspection must not move,
// aim, attack, or change the player behind the room.
class RoomFreecam {
public:
    enum class Action { None, Capture, Release, Exit, Screenshot, ToggleLines, BendJoints, RestorePose, RestoreOriginal };

    Action handle(const WindowEvent& event, bool captured) {
        if (event.type == WindowEvent::Type::FocusLoss) { clear(); return Action::Release; }
        if (event.type == WindowEvent::Type::MouseMove && captured) {
            yaw_ += event.dx * 0.0022;
            pitch_ = std::clamp(pitch_ - event.dy * 0.0022, -1.5707, 1.5707);
        } else if (event.type == WindowEvent::Type::Wheel) {
            position_ += forward() * (event.wheel * 0.25);
        } else if (event.type == WindowEvent::Type::MouseDown && !captured && event.button == 1) {
            return Action::Capture;
        } else if (event.type == WindowEvent::Type::KeyUp) {
            keys_.erase(event.key);
        } else if (event.type == WindowEvent::Type::KeyDown) {
            if (event.key == 'c') return event.repeat ? Action::None : Action::Exit;
            if (event.key == 'v') return event.repeat ? Action::None : Action::ToggleLines;
            if ((event.key >= '0' && event.key <= '9') || event.key == 'q' || event.key == 'e' ||
                event.key == 'f' || event.key == 'g' || event.key == 'h' || event.key == 'j' ||
                event.key == 'k' || event.key == 'l' || event.key == 'z' || event.key == 'x' || event.key == 'n' || event.key == 'm') return Action::BendJoints;
            if (event.key == 'r') return event.repeat ? Action::None : Action::RestorePose;
            if (event.key == 'b') return event.repeat ? Action::None : Action::RestoreOriginal;
            if (event.key == kKeyEscape) { clear(); return Action::Release; }
            if (event.key == kKeyF12) return event.repeat ? Action::None : Action::Screenshot;
            if (event.key == 'w' || event.key == 'a' || event.key == 's' || event.key == 'd' ||
                event.key == ' ' || event.key == kKeyLeftControl || event.key == kKeyShift || event.key == kKeyAlt) {
                keys_.insert(event.key);
            }
        }
        return Action::None;
    }

    void clear() { keys_.clear(); }
    bool reverseBend() const { return keys_.count(kKeyShift) != 0; }
    // Momentary modifiers, not a retained selection mode.
    int fingerJoint() const {
        const bool control=keys_.count(kKeyLeftControl), alt=keys_.count(kKeyAlt);
        return control ? (alt ? 3 : 1) : (alt ? 2 : 0);
    }

    void move(double dt) {
        const double along = (keys_.count('w') ? 1 : 0) - (keys_.count('s') ? 1 : 0);
        const double side = (keys_.count('d') ? 1 : 0) - (keys_.count('a') ? 1 : 0);
        const double vertical = (keys_.count(' ') ? 1 : 0) - (keys_.count(kKeyLeftControl) ? 1 : 0);
        const Vec3 right{std::cos(yaw_), 0, std::sin(yaw_)};
        const Vec3 direction = forward() * along + right * side + Vec3{0, vertical, 0};
        const double magnitude = length(direction);
        if (magnitude > 1e-9) {
            const double speed = keys_.count(kKeyShift) ? 5.4 : 1.8;
            position_ += direction * (std::max(0.0, dt) * speed / magnitude);
        }
    }

    Camera camera(int width, int height, bool gpu) const {
        Camera result;
        result.width = std::max(1, width);
        result.height = std::max(1, height);
        result.fovDegrees = 50;
        result.position = position_;
        result.target = position_ + forward();
        result.worldSpaceGpu = gpu;
        return result;
    }

private:
    Vec3 forward() const {
        const double horizontal = std::cos(pitch_);
        return {std::sin(yaw_) * horizontal, std::sin(pitch_), -std::cos(yaw_) * horizontal};
    }

    Vec3 position_{0, 1.15, 3.5};
    double yaw_ = 0;
    double pitch_ = -std::atan2(0.20, 3.5);
    std::set<int> keys_;
};

} // namespace gtc::native
