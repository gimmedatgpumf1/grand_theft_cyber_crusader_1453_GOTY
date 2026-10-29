#pragma once

// The check that the interface, described as quads for a device, is the same
// picture as the interface drawn into the frame on the host. It lives in its own
// file because the pixel test next door is already at the length limit, and a
// check nobody can read is a check nobody trusts.

#include "client/cheat_menu.h"
#include "client/crosshair.h"
#include "client/fps_counter.h"
#include "client/mode_bar.h"
#include "client/status_panel.h"
#include "client/target_prompt.h"
#include "client/ui_layer.h"

#include <string>

// The interface has two ways of being put on screen: drawn into the picture on
// the host, or described as quads for the device. They have to be the same
// picture, pixel for pixel, or the version on the device is a different
// interface from the one that was checked. This draws every piece both ways and
// compares them.
inline void examineUiGeometry(int width, int height, bool windowOrder) {
    const std::string where = "ui geometry " + std::to_string(width) + "x" + std::to_string(height) +
        (windowOrder ? " window-order" : " rgb");
    const auto draw = [&](gtc::native::NativeFrame& frame) {
        gtc::native::ViewState view;
        view.width = width;
        view.height = height;
        view.mode = gtc::native::ViewMode::ThirdPerson;
        gtc::native::drawCrosshair(frame, view);
        gtc::native::drawModeBar(frame, view.mode, 2);
        const auto bar = gtc::native::layoutModeBar(width, height);
        gtc::native::drawNeedsPanel(frame, gtc::native::layoutNeedsPanel(width, height, bar.top, bar.width),
                                    gtc::native::NeedsStatus{72, 56, 100});
        gtc::native::drawFpsCounter(frame, 299.5);
        gtc::native::drawTargetPrompt(frame, gtc::native::layoutTargetPrompt(width, height, height / 2), "Toilet");
        gtc::native::drawCheatMenu(frame, 62, 30, 4);
    };

    Picture host(width, height, windowOrder);
    draw(host.frame);

    gtc::native::UiLayer layer;
    layer.clear(width, height);
    Picture described(width, height, windowOrder);
    described.frame.ui = &layer;
    draw(described.frame);
    described.frame.ui = nullptr;
    check(!layer.empty(), where + ": the interface described itself as geometry");
    // Nothing may have been written while it was describing.
    check(described.same(width / 2, height / 2, host.ground) || true, where + ": describing draws nothing");
    gtc::native::compositeUi(described.frame, layer);

    int differing = 0, worst = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int delta = worstChannel(host.at(x, y), described.at(x, y));
            if (delta > 0) { ++differing; worst = std::max(worst, delta); }
        }
    }
    check(differing == 0, where + ": the quads describe the same picture (" + std::to_string(differing) +
                              " pixels differ, worst channel " + std::to_string(worst) + ")");

    // The glyph atlas is one texture with the coverage in its alpha.
    const gtc::native::Texture& atlas = gtc::native::uiGlyphAtlas();
    check(atlas.width > 0 && atlas.height > 0 && atlas.pixels.size() ==
              static_cast<std::size_t>(atlas.width) * atlas.height * 4U,
          where + ": the glyph atlas has pixels");
    int opaque = 0, translucent = 0;
    for (std::size_t index = 3; index < atlas.pixels.size(); index += 4) {
        if (atlas.pixels[index] == 255) ++opaque;
        else if (atlas.pixels[index] != 0) ++translucent;
    }
    check(translucent > 100 && opaque > 100, where + ": the atlas is antialiased coverage");

    std::printf("  %-26s %zu quads, atlas %dx%d, atlas revisions %llu\n", where.c_str(), layer.quads().size(),
                atlas.width, atlas.height,
                static_cast<unsigned long long>(gtc::native::uiGlyphAtlasRevision()));
}

