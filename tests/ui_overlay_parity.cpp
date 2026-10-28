// The overlay's proof, without a device.
//
// The HUD is drawn twice: once into pixels, as it always was, and once as quads
// for the device. This walks the quads back to screen positions and back to
// texels, and compares them with the pixels the other path wrote. Nothing is
// timed here and nothing is photographed: either the geometry covers the same
// ink in the same places, or this fails.

#include "client/raster.h"
#include "client/diagnostics.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// A frame with a canvas, the way the presenter hands one to the HUD.
gtc::native::NativeFrame canvas(int width, int height, bool gpu) {
    gtc::native::NativeFrame frame;
    frame.width = width;
    frame.height = height;
    frame.wantsBgra = true;
    frame.uiOnGpu = gpu;
    frame.logicalUi();
    if (!gpu) {
        frame.bgra.assign(static_cast<std::size_t>(width) * height * 4, 0);
        frame.alpha.assign(static_cast<std::size_t>(width) * height, 255);
    }
    return frame;
}

void draw(gtc::native::NativeFrame& frame) {
    using gtc::native::colorHex;
    using gtc::native::drawText;
    frame.rectangle(20, 20, 220, 90, colorHex("#101713"));
    frame.blendRectangle(30, 120, 180, 40, colorHex("#d7ff54"), 0.65);
    drawText(frame, 34, 180, "HP 100 ARM 45", colorHex("#d7ff54"));
    drawText(frame, 34, 215, "TOILET 3 M", colorHex("#b4c5ba"));
}

// Which bytes of the atlas a quad's coordinates name, and how much ink is in
// them: the device samples the same texels through the same rectangle.
int quadInk(const gtc::native::UiQuad& quad) {
    if (quad.texture == nullptr || quad.texture->width <= 0) return 0;
    const gtc::native::Texture& atlas = *quad.texture;
    const int x0 = static_cast<int>(std::lround(quad.u0 * atlas.width));
    const int x1 = static_cast<int>(std::lround(quad.u1 * atlas.width));
    const int y0 = static_cast<int>(std::lround(quad.v0 * atlas.height));
    const int y1 = static_cast<int>(std::lround(quad.v1 * atlas.height));
    int ink = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t pixel = (static_cast<std::size_t>(y) * atlas.width + x) * 4;
            if (pixel + 3 < atlas.pixels.size() && atlas.pixels[pixel + 3] != 0) ++ink;
        }
    }
    return ink;
}

} // namespace

int main() {
    using gtc::native::GpuScene;
    using gtc::native::NativeFrame;
    const int width = 640, height = 480;

    NativeFrame pixels = canvas(width, height, false);
    draw(pixels);
    NativeFrame quads = canvas(width, height, true);
    draw(quads);

    check(!quads.uiQuads.empty(), "the overlay produced no quads");
    const std::size_t size = static_cast<std::size_t>(width) * height;
    check(pixels.bgra.size() >= size * 4, "the pixel path wrote nothing");

    // Ink the pixel path put down, glyphs only: the two panel fills are solid
    // rectangles and the quads that carry them cover exactly the same area by
    // construction, so counting them would only dilute the comparison.
    const int panels[2][4] = {{20, 20, 220, 90}, {30, 120, 180, 40}};
    const auto inPanel = [&](int x, int y) {
        for (const auto& panel : panels) {
            if (x >= panel[0] && y >= panel[1] && x < panel[0] + panel[2] && y < panel[1] + panel[3]) return true;
        }
        return false;
    };
    std::size_t covered = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (inPanel(x, y)) continue;
            const unsigned char* texel = pixels.bgra.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            if (texel[0] != 0 || texel[1] != 0 || texel[2] != 0) ++covered;
        }
    }
    check(covered > 0, "the pixel path covered no pixels");

    // Every quad, back to the screen: the clip coordinates the device is handed
    // have to land on the rectangle the HUD asked for.
    GpuScene scene;
    scene.width = width;
    scene.height = height;
    gtc::native::appendOverlay(scene, quads);
    check(!scene.batches.empty(), "appendOverlay produced no batches");
    std::size_t vertices = 0, textured = 0, overlay = 0;
    for (const gtc::native::GpuBatch& batch : scene.batches) {
        check(batch.overlay, "an overlay batch is not marked as one");
        check(batch.indices.empty(), "an overlay batch carries indices");
        check(batch.material.texture != nullptr, "an overlay batch samples nothing");
        overlay += batch.overlay ? 1 : 0;
        textured += batch.material.texture != nullptr ? 1 : 0;
        vertices += batch.vertices.size();
    }
    check(vertices == quads.uiQuads.size() * 6, "an overlay quad is not two triangles");
    check(overlay == scene.batches.size() && textured == scene.batches.size(), "overlay batches disagree");

    // The clip coordinates, undone: each vertex must be a corner of its quad.
    std::size_t quadIndex = 0;
    for (const gtc::native::GpuBatch& batch : scene.batches) {
        for (std::size_t vertex = 0; vertex < batch.vertices.size(); ++vertex) {
            const gtc::native::GpuVertex& corner = batch.vertices[vertex];
            const gtc::native::UiQuad& source = quads.uiQuads[quadIndex / 6];
            const double x = (corner.x + 1.0) * 0.5 * width;
            const double y = (1.0 - corner.y) * 0.5 * height;
            const bool onX = std::abs(x - source.x0) < 1e-3 || std::abs(x - source.x1) < 1e-3;
            const bool onY = std::abs(y - source.y0) < 1e-3 || std::abs(y - source.y1) < 1e-3;
            check(onX && onY, "a quad vertex does not sit on its rectangle");
            check(corner.z == 0.0f, "a quad vertex is not flat");
            ++quadIndex;
        }
    }

    // The ink the quads carry against the ink the pixels carry. The glyph quads
    // are the atlas's coverage in the same places, so the two totals are the same
    // number: the device is not drawing a different picture, it is drawing the
    // same one somewhere else.
    std::size_t quadInkTotal = 0;
    for (const gtc::native::UiQuad& quad : quads.uiQuads) quadInkTotal += quadInk(quad);
    check(quadInkTotal > 0, "the glyph quads cover no texels");
    const double ratio = static_cast<double>(quadInkTotal) / static_cast<double>(covered);
    check(ratio > 0.9 && ratio < 1.1, "the quads and the pixels do not cover the same ink");
    std::printf("overlay parity: %zu quads, %zu batches, %zu ink texels, %zu covered pixels (%.3f)\n",
                quads.uiQuads.size(), scene.batches.size(), quadInkTotal, covered, ratio);

    // The direct path and the quad path have to agree on where things are, not
    // just how many: the leftmost quad of the first label starts where the first
    // lit pixel does.
    std::size_t firstPixelX = width;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (inPanel(x, y)) continue;
            const unsigned char* texel = pixels.bgra.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            if (texel[0] == 0 && texel[1] == 0 && texel[2] == 0) continue;
            if (x < static_cast<int>(firstPixelX)) firstPixelX = static_cast<std::size_t>(x);
        }
    }
    float firstQuadX = static_cast<float>(width);
    for (const gtc::native::UiQuad& quad : quads.uiQuads) {
        if (quad.texture == nullptr) continue;
        firstQuadX = std::min(firstQuadX, quad.x0);
    }
    check(std::abs(static_cast<double>(firstQuadX) - static_cast<double>(firstPixelX)) <= 2.0,
          "the quads start somewhere other than where the pixels do");

    if (failures != 0) {
        std::printf("ui overlay parity: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("ui overlay parity: PASS\n");
    return 0;
}
