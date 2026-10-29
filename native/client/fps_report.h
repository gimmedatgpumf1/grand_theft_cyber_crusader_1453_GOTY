#pragma once

#include <chrono>
#include <cstdio>

namespace gtc::native {

// The interactive game always reports its real frame-loop average, including
// in the test room. No launch option or environment variable is needed.
class FrameRateReporter {
public:
    using Clock = std::chrono::steady_clock;

    explicit FrameRateReporter(Clock::time_point start = Clock::now()) : window_(start) {}

    void record(Clock::time_point now, double renderMs, double presentMs, double logicMs,
                std::FILE* output = stderr) {
        ++frames_;
        renderTotal_ += renderMs;
        presentTotal_ += presentMs;
        logicTotal_ += logicMs;
        const double span = std::chrono::duration<double>(now - window_).count();
        if (span < 1.0) return;
        std::fprintf(output, "[fps] %.1f (%.2f ms/frame: draw %.2f, present %.2f, logic %.2f)\n", frames_ / span,
                     1000.0 * span / frames_, renderTotal_ / frames_, presentTotal_ / frames_, logicTotal_ / frames_);
        window_ = now;
        frames_ = 0;
        renderTotal_ = presentTotal_ = logicTotal_ = 0.0;
    }

private:
    Clock::time_point window_{};
    int frames_ = 0;
    double renderTotal_ = 0.0, presentTotal_ = 0.0, logicTotal_ = 0.0;
};

} // namespace gtc::native
