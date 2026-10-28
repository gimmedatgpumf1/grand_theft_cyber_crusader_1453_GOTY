#pragma once

#include "client/raster.h"

#include <string>
#include <vector>

namespace gtc::native {

constexpr int kKeyEscape = 256;
constexpr int kKeyF1 = 257;
constexpr int kKeyF12 = 258;
constexpr int kKeyShift = 259;
constexpr int kKeyEnter = 260;
constexpr int kKeyLeft = 261;
constexpr int kKeyRight = 262;
constexpr int kKeyUp = 263;
constexpr int kKeyDown = 264;
constexpr int kKeyPageUp = 265;
constexpr int kKeyPageDown = 266;
constexpr int kKeyTab = 267;
constexpr int kKeyCapsLock = 268;
constexpr int kKeyLeftControl = 269;
constexpr int kKeyAlt = 270;

struct WindowEvent {
    enum class Type { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, Wheel, FocusLoss, Close } type;
    int key = 0;
    int button = 0;
    double x = 0;
    double y = 0;
    double dx = 0;
    double dy = 0;
    double wheel = 0;
    bool repeat = false;
};

class NativeWindow {
public:
    struct Impl;

    NativeWindow();
    ~NativeWindow();
    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool open(const std::string& title, std::string& error);
    void close();
    bool poll(std::vector<WindowEvent>& events);
    bool present(const NativeFrame& frame);
    // Draws one scene through whichever backend this window opened. This is the
    // exact call the game's frame loop ends up making, so timing it measures
    // the backend and nothing else -- no window creation, no scene building.
    bool renderScene(const GpuScene& scene, NativeFrame& frame);
    // Which backend opened, for reporting.
    const char* backend() const;
    bool save(const NativeFrame& frame, const std::string& path);
    void captureMouse(bool capture);
    void size(int& width, int& height) const;
    bool gpu() const;
    bool opened() const;

private:
    Impl* impl_ = nullptr;
};

} // namespace gtc::native
