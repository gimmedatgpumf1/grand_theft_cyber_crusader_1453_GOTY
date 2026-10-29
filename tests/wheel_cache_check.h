#pragma once
#include "../tests/wheel_reference.h"

inline void examineWheelCache() {
    int comparisons = 0, differing = 0;
    for (const auto& size : {std::pair<int, int>{640, 480}, {1920, 1080}, {640, 480}}) {
        auto layout = gtc::native::layoutInteractionWheel(size.first, size.second);
        for (int variant = 0; variant < 2; ++variant) {
        if (variant == 1) { layout.cancelRadius += 7; layout.centreX += 3; layout.centreY -= 2; }
        for (int hover = -2; hover < 3; ++hover) {
            const int x = hover == -2 ? -1 : hover == -1 ? layout.centreX : layout.labelX[hover];
            const int y = hover == -2 ? -1 : hover == -1 ? layout.centreY : layout.labelY[hover];
            gtc::native::NativeFrame a, b;
            a.width = b.width = size.first; a.height = b.height = size.second;
            gtc::native::UiLayer actual, expected;
            actual.clear(a.width, a.height); expected.clear(b.width, b.height);
            a.ui = &actual; b.ui = &expected;
            gtc::native::drawInteractionWheel(a, layout, x, y);
            gtc::native::referenceDrawWheel(b, layout, x, y);
            ++comparisons;
            if (actual.quads().size() != expected.quads().size()) { ++differing; continue; }
            for (std::size_t i = 0; i < actual.quads().size(); ++i) {
                const auto& p = actual.quads()[i]; const auto& q = expected.quads()[i];
                if (p.left != q.left || p.top != q.top || p.right != q.right || p.bottom != q.bottom ||
                    p.red != q.red || p.green != q.green || p.blue != q.blue || p.opacity != q.opacity ||
                    p.texture != q.texture || p.cellLeft != q.cellLeft || p.cellTop != q.cellTop) {
                    ++differing; break;
                }
            }
        }
        }
    }
    check(differing == 0, "cached wheel quads match reference for every hover and resize");
    std::printf("  wheel cache: %d comparisons, %d differences\n", comparisons, differing);
}
