#pragma once

// Check the offline font coverage against the same font's reference rasterizer
// for every shipped size and printable glyph. This is an asset regression test,
// not proof that the Vulkan pipeline drew anything.
inline void examineFontAtlas() {
    int glyphs = 0, mismatches = 0;
    const auto& atlas = gtc::native::uiGlyphAtlas();
    for (int size = 20; size <= 48; ++size) for (int code = 33; code <= 126; ++code) {
        gtc::native::UiQuad quad;
        if (!gtc::native::uiGlyphQuad(quad, static_cast<char>(code), size, 0, 0)) continue;
        int width = 0, height = 0, dx = 0, dy = 0;
        const unsigned char* pixels = nullptr;
        if (!gtc::native::glyphCoverage(static_cast<char>(code), size, width, height, dx, dy, &pixels)) {
            ++mismatches;
            continue;
        }
        ++glyphs;
        if (quad.left != dx || quad.top != dy || quad.right - quad.left != width ||
            quad.bottom - quad.top != height || quad.cellLeft < 0 || quad.cellTop < 0 ||
            quad.cellLeft + width > atlas.width || quad.cellTop + height > atlas.height) {
            ++mismatches;
            continue;
        }
        for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) {
            const auto offset = (static_cast<std::size_t>(quad.cellTop + row) * atlas.width + quad.cellLeft + col) * 4;
            if (atlas.pixels[offset + 3] != pixels[static_cast<std::size_t>(row) * width + col]) ++mismatches;
        }
    }
    check(glyphs > 2600 && mismatches == 0, "offline font atlas matches glyph coverage at all 29 UI sizes");
    std::printf("  font atlas: %d glyphs, %d differences\n", glyphs, mismatches);
}
