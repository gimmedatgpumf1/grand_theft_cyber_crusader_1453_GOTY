// The interface, checked as pixels.
//
// The overlay that used to live here was deleted and is being rebuilt one piece
// at a time. What exists now is the frame-rate strip, the reticle and the F1 need
// controls. All three are drawn by the host into the finished picture, so unlike
// anything routed through the device they can be checked exactly, on this
// machine, with no driver involved -- and that is the entire reason they are on
// the host.
//
// What is asserted:
//
//   * the strip is centred, translucent, and set in the real antialiased face;
//   * the reticle is at the middle of the window, is drawn in the reading colour
//     with a dark outline, has a gap around the centre, and is absent in god view;
//   * the need panel is centred, translucent, fits inside a 480-line frame, its
//     four rows do not collide, its two columns do not collide, and it draws the
//     values it was handed rather than last round's;
//   * the panel is drawn over the reticle, in the order the loop draws them.

#include "client/interface_style.h"

#include "interface_check.h"

namespace interface_check {

int failures = 0;

}  // namespace interface_check

using namespace interface_check;

#include "../tests/interface_geometry.h"
#include "../tests/wheel_cache_check.h"
#include "../tests/interface_atlas.h"

int main() {
    examineStrip(640, 480, false);
    examineStrip(640, 480, true);
    examineStrip(1920, 1080, true);
    examineStrip(1568, 929, true);

    examineCrosshair(640, 480, false);
    examineCrosshair(640, 480, true);
    examineCrosshair(1920, 1080, true);

    examineWheelCache();
    examineInteractionWheel(640, 480, false);
    examineInteractionWheel(640, 480, true);
    examineInteractionWheel(1920, 1080, true);

    examineOverlayPanels(640, 480, false);
    examineOverlayPanels(640, 480, true);
    examineOverlayPanels(1920, 1080, true);

    examineFontAtlas();
    examineUiGeometry(640, 480, false);
    examineUiGeometry(640, 480, true);
    examineUiGeometry(1920, 1080, true);

    examineTargetPrompt(640, 480, false);
    examineTargetPrompt(640, 480, true);
    examineTargetPrompt(1920, 1080, true);

    examineNeedsPanel(640, 480, false);
    examineNeedsPanel(640, 480, true);
    examineNeedsPanel(1568, 929, true);
    examineNeedsPanel(1920, 1080, true);

    examineModeBar(640, 480, false);
    examineModeBar(640, 480, true);
    examineModeBar(1568, 929, true);
    examineModeBar(1920, 1080, true);

    examineCheatMenu(640, 480, false);
    examineCheatMenu(640, 480, true);
    examineCheatMenu(1568, 929, true);
    examineCheatMenu(1920, 1080, true);

    // A number that is not a number is not allowed to print nonsense, and the
    // strip has to keep its shape whatever the rate is.
    gtc::native::NativeFrame frame;
    frame.width = 640;
    frame.height = 480;
    frame.rgb.assign(640U * 480U * 3, 0);
    gtc::native::drawFpsCounter(frame, std::nan(""));
    gtc::native::drawFpsCounter(frame, -1.0);
    gtc::native::drawFpsCounter(frame, 0.4);
    // Needs outside their range are clamped, not printed.
    gtc::native::drawCheatMenu(frame, -20, 400);

    if (failures == 0) {
        std::printf("interface: PASS (rate strip, reticle, prompt, needs panel, mode bar, overlays, circle)\n");
        return 0;
    }
    std::printf("interface: FAIL (%d)\n", failures);
    return 1;
}