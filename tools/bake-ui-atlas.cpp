// Offline only: generate the antialiased glyph coverage asset from ui-sans.ttf.
// Build from the repository root with:
//   c++ -O2 -Inative tools/bake-ui-atlas.cpp -o /tmp/bake-ui-atlas
//   /tmp/bake-ui-atlas
// The game only loads this asset and computes metrics; it never runs this tool.
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

struct Cell { int code, size, x, y, width, height; };
int main() {
    std::ifstream file("native/assets/ui-sans.ttf", std::ios::binary);
    std::vector<unsigned char> fontBytes((std::istreambuf_iterator<char>(file)), {});
    stbtt_fontinfo font{};
    if (fontBytes.empty() || !stbtt_InitFont(&font, fontBytes.data(), 0)) return 1;
    constexpr int width = 1024, padding = 2;
    int x = 0, y = 0, rowHeight = 0, usedHeight = 0;
    std::vector<Cell> cells;
    for (int size = 20; size <= 48; ++size) {
        const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(size));
        for (int code = 32; code <= 126; ++code) {
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&font, code, scale, scale, &x0, &y0, &x1, &y1);
            const int w = std::max(0, x1 - x0), h = std::max(0, y1 - y0);
            if (!w || !h) continue;
            if (x + w + padding > width) { y += rowHeight + padding; x = padding; rowHeight = 0; }
            cells.push_back({code, size, x, y, w, h});
            x += w + padding;
            rowHeight = std::max(rowHeight, h);
            usedHeight = std::max(usedHeight, y + h + padding);
        }
        y += rowHeight + padding; x = padding; rowHeight = 0;
    }
    const int height = std::max(64, usedHeight + padding);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (const Cell& cell : cells) {
        const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(cell.size));
        std::vector<unsigned char> coverage(static_cast<std::size_t>(cell.width) * cell.height);
        stbtt_MakeCodepointBitmap(&font, coverage.data(), cell.width, cell.height, cell.width, scale, scale, cell.code);
        for (int row = 0; row < cell.height; ++row) for (int col = 0; col < cell.width; ++col) {
            const std::size_t dst = (static_cast<std::size_t>(cell.y + row) * width + cell.x + col) * 4;
            pixels[dst] = pixels[dst + 1] = pixels[dst + 2] = 255;
            pixels[dst + 3] = coverage[static_cast<std::size_t>(row) * cell.width + col];
        }
    }
    std::printf("ui glyph atlas: %dx%d, %zu cells\n", width, height, cells.size());
    return stbi_write_png("native/assets/ui-glyphs.png", width, height, 4, pixels.data(), width * 4) ? 0 : 1;
}
