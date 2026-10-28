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

    // `preferOpenGl` exists for one caller: the benchmark's reminder of what the
    // other backend costs. The game itself is Vulkan or nothing, which is why
    // there is no environment variable for it any more -- a hidden switch that
    // quietly ran the whole session on the other backend is exactly how the
    // frames being optimised stopped being the frames on screen.
    bool open(const std::string& title, std::string& error, bool preferOpenGl = false);
    void close();
    bool poll(std::vector<WindowEvent>& events);
    bool present(const NativeFrame& frame);
    // Draws one scene through whichever backend this window opened. This is the
    // exact call the game's frame loop ends up making, so timing it measures
    // the backend and nothing else -- no window creation, no scene building.
    bool renderScene(const GpuScene& scene, NativeFrame& frame);
    // Which backend opened, for reporting.
    const char* backend() const;
    // Waits for the device to finish, and brings the finished frame back to
    // host memory. Both exist for measurement: they are what a Vulkan frame
    // already pays and an OpenGL submit does not.
    void awaitGpu();
    bool readBack(int width, int height, const GpuScene&, NativeFrame& frame);
    bool save(const NativeFrame& frame, const std::string& path);
    void captureMouse(bool capture);
    // Sets the client area to a size. The benchmark uses it to measure at the
    // size it reports, because a swapchain frame is rendered at the size of the
    // window and nothing else.
    void resize(int width, int height);
    void size(int& width, int& height) const;
    bool gpu() const;
    bool opened() const;

private:
    Impl* impl_ = nullptr;
};

} // namespace gtc::native
