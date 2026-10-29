#include "client/interaction_wheel.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gtc::native {
namespace {

constexpr const char* kLabels[kWheelOptionCount] = {"SIT", "SHIT", "PISS"};
// Sector middles: up, lower right, lower left. Each sector is 120 degrees wide,
// so together they are the whole circle with nothing left over.
constexpr double kAngles[kWheelOptionCount] = {-90.0, 30.0, 150.0};
constexpr double kSectorSpan = 120.0;

double degreesOf(int dx, int dy) {
    double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) * 180.0 / 3.141592653589793;
    if (angle < 0.0) angle += 360.0;
    return angle;
}

int sectorOf(double angle) {
    // The signed difference to each sector's middle, wrapped into -180..180 so a
    // sector that straddles zero degrees is not split in half.
    for (int index = 0; index < kWheelOptionCount; ++index) {
        const double delta = std::fmod(angle - kAngles[index] + 540.0, 360.0) - 180.0;
        if (std::abs(delta) <= kSectorSpan / 2.0) return index;
    }
    return 0;
}

// A run with nothing in it. Not -1: the cancel is -1 and it is a real answer.
constexpr int kNoSector = -99;

// Cache the exact horizontal runs of the original disc construction. The
// pointer only changes their colour, not their geometry or ordering.
struct WheelRun { int sector, x, y, width; };
const std::vector<WheelRun>& wheelRuns(int radius, int cancelRadius) {
    struct Cache { int radius = -1, cancelRadius = -1; std::vector<WheelRun> runs; };
    static thread_local Cache cache;
    if (cache.radius == radius && cache.cancelRadius == cancelRadius) return cache.runs;
    cache.radius = radius;
    cache.cancelRadius = cancelRadius;
    cache.runs.clear();
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = static_cast<int>(std::sqrt(static_cast<double>(radius) * radius - dy * dy));
        int runStart = -half, runSector = kNoSector;
        for (int dx = -half; dx <= half; ++dx) {
            const long distanceSquared = static_cast<long>(dx) * dx + static_cast<long>(dy) * dy;
            const int sector = distanceSquared <= static_cast<long>(cancelRadius) * cancelRadius
                ? kWheelCancel : sectorOf(degreesOf(dx, dy));
            if (sector == runSector) continue;
            if (runSector != kNoSector) cache.runs.push_back({runSector, runStart, dy, dx - runStart});
            runStart = dx;
            runSector = sector;
        }
        cache.runs.push_back({runSector, runStart, dy, half + 1 - runStart});
    }
    return cache.runs;
}

}  // namespace

InteractionWheelLayout layoutInteractionWheel(int frameWidth, int frameHeight) {
    InteractionWheelLayout layout;
    layout.textHeight = interfaceTextHeight(frameHeight, 15.0);
    const int smaller = std::min(frameWidth, frameHeight);
    // A third of the smaller side: big enough to aim at, small enough that the
    // world is still visible around it.
    layout.radius = std::max(layout.textHeight * 4, smaller * 27 / 100);
    layout.cancelRadius = std::max(layout.textHeight, layout.radius / 5);
    layout.stroke = std::max(2, layout.textHeight / 6);
    layout.centreX = frameWidth / 2;
    layout.centreY = frameHeight / 2;
    const int labelRadius = layout.radius * 3 / 5;
    for (int index = 0; index < kWheelOptionCount; ++index) {
        const double radians = kAngles[index] * 3.141592653589793 / 180.0;
        layout.labelX[index] = layout.centreX + static_cast<int>(std::lround(std::cos(radians) * labelRadius));
        layout.labelY[index] = layout.centreY + static_cast<int>(std::lround(std::sin(radians) * labelRadius));
    }
    return layout;
}

int interactionWheelHit(const InteractionWheelLayout& layout, int x, int y) {
    const long dx = x - layout.centreX, dy = y - layout.centreY;
    const long distanceSquared = dx * dx + dy * dy;
    if (distanceSquared > static_cast<long>(layout.radius) * layout.radius) return kWheelOutside;
    // The middle is the cancel: the three sectors meet there, so it is the one
    // place in the disc that belongs to none of them.
    if (distanceSquared <= static_cast<long>(layout.cancelRadius) * layout.cancelRadius) return kWheelCancel;
    return sectorOf(degreesOf(static_cast<int>(dx), static_cast<int>(dy)));
}

void drawInteractionWheel(NativeFrame& frame, const InteractionWheelLayout& layout, int pointerX, int pointerY) {
    if (layout.radius <= 0) return;
    const double scale = frame.uiScale, offsetX = frame.uiOffsetX, offsetY = frame.uiOffsetY;
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;

    const int hovered = pointerX < 0 || pointerY < 0 ? kWheelOutside
                                                     : interactionWheelHit(layout, pointerX, pointerY);

    const Color ink = colorHex(kInkColour), muted = colorHex(kMutedColour);
    const Color accent = colorHex("#d7ff54"), cancel = colorHex("#ff7a5e");
    const Color shell = colorHex(kPanelColour);

    // The disc, filled a row at a time: one fill per run of the same sector, so
    // the whole circle costs a few hundred fills and its edges are exactly radial.
    const int radius = layout.radius;
    const auto paint = [&](int sector, int left, int y, int width) {
        if (width <= 0) return;
        if (sector == kWheelCancel) {
            frame.blendRectangle(left, y, width, 1, shell, 0.38);   // the cancel's own well
        } else if (sector == hovered) {
            frame.blendRectangle(left, y, width, 1, accent, 0.22);
        } else {
            // Faint enough that the room is still visible through the disc: this
            // sits in the middle of the screen, not in a corner.
            frame.blendRectangle(left, y, width, 1, shell, 0.26);
        }
    };
    for (const WheelRun& run : wheelRuns(radius, layout.cancelRadius)) {
        paint(run.sector, layout.centreX + run.x, layout.centreY + run.y, run.width);
    }

    // The rim, and the three cuts: the lines are what make it read as divided.
    ringDisc(frame, layout.centreX, layout.centreY, radius, std::max(1, layout.stroke / 2),
             hovered >= 0 ? accent : muted, hovered >= 0 ? 0.8 : 0.5);
    for (int index = 0; index < kWheelOptionCount; ++index) {
        const double radians = (kAngles[index] + kSectorSpan / 2.0) * 3.141592653589793 / 180.0;
        for (int step = layout.cancelRadius; step <= radius; ++step) {
            const int x = layout.centreX + static_cast<int>(std::lround(std::cos(radians) * step));
            const int y = layout.centreY + static_cast<int>(std::lround(std::sin(radians) * step));
            const int cut = std::max(1, layout.stroke / 2);
            frame.rectangle(x - cut / 2, y - cut / 2, cut, cut, muted);
        }
    }

    // The names, out along their own sectors. The one under the pointer is in the
    // reading colour; that and the lit sector are the whole of the feedback.
    for (int index = 0; index < kWheelOptionCount; ++index) {
        const int width = textWidthPixels(kLabels[index], layout.textHeight);
        const int left = layout.labelX[index] - width / 2;
        const int top = layout.labelY[index] - textHeightPixels(layout.textHeight) / 2;
        drawTextPixels(frame, left, top, kLabels[index], index == hovered ? ink : muted, layout.textHeight);
    }

    // The middle: the cancel. Warm when the pointer is on it, because a cross is
    // not obviously a button.
    const bool cancelLit = hovered == kWheelCancel;
    if (cancelLit) fillDisc(frame, layout.centreX, layout.centreY, layout.cancelRadius, cancel, 0.5);
    ringDisc(frame, layout.centreX, layout.centreY, layout.cancelRadius, layout.stroke,
             cancelLit ? cancel : muted, cancelLit ? 1.0 : 0.75);
    const int reach = layout.cancelRadius * 3 / 5;
    for (int step = -reach; step <= reach; step += std::max(1, layout.stroke / 2)) {
        const int thickness = std::max(2, layout.stroke / 2);
        frame.rectangle(layout.centreX + step, layout.centreY + step, thickness, thickness,
                        cancelLit ? ink : muted);
        frame.rectangle(layout.centreX - step, layout.centreY + step, thickness, thickness,
                        cancelLit ? ink : muted);
    }

    frame.uiScale = scale;
    frame.uiOffsetX = offsetX;
    frame.uiOffsetY = offsetY;
}

}  // namespace gtc::native
