#include "client/ui_layer.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace gtc::native {
namespace {

// The antialiased font coverage is a build-time asset. Runtime layout computes
// glyph metrics and atlas positions only; it never rasterizes glyph pixels.
struct Cell { int left = 0, top = 0, width = 0, height = 0; };
constexpr int kAtlasWidth = 1024;
constexpr int kAtlasPadding = 2;
constexpr int kFirstSize = 20, kLastSize = 48;
unsigned keyOf(char c, int height) {
    return (static_cast<unsigned>(height) << 8) | static_cast<unsigned char>(c);
}

const std::map<unsigned, Cell>& atlasCells() {
    static const auto cells = [] {
        std::map<unsigned, Cell> result;
        int x = 0, y = 0, rowHeight = 0;
        for (int size = kFirstSize; size <= kLastSize; ++size) {
            for (int code = 32; code <= 126; ++code) {
                int width = 0, height = 0, xOffset = 0, yOffset = 0;
                if (!glyphMetrics(static_cast<char>(code), size, width, height, xOffset, yOffset)) continue;
                if (x + width + kAtlasPadding > kAtlasWidth) {
                    y += rowHeight + kAtlasPadding; x = kAtlasPadding; rowHeight = 0;
                }
                result[keyOf(static_cast<char>(code), size)] = {x, y, width, height};
                x += width + kAtlasPadding;
                rowHeight = std::max(rowHeight, height);
            }
            y += rowHeight + kAtlasPadding; x = kAtlasPadding; rowHeight = 0;
        }
        return result;
    }();
    return cells;
}

}  // namespace

const Texture& uiGlyphAtlas() {
    const Texture& texture = loadTexture("native/assets/ui-glyphs.png");
    if (texture.width != kAtlasWidth || texture.height != 1824 || texture.pixels.empty()) {
        std::fprintf(stderr, "[ui] missing or invalid native/assets/ui-glyphs.png\n");
        std::abort();
    }
    return texture;
}

std::uint64_t uiGlyphAtlasRevision() { return 1; }

bool uiGlyphQuad(UiQuad& quad, char character, int pixelHeight, int penX, int baselineY) {
    const auto& cells = atlasCells();
    const auto found = cells.find(keyOf(character, pixelHeight));
    if (found == cells.end()) return false;
    const Cell& cell = found->second;
    int width = 0, height = 0, xOffset = 0, yOffset = 0;
    if (!glyphMetrics(character, pixelHeight, width, height, xOffset, yOffset)) return false;
    quad.left = static_cast<float>(penX + xOffset);
    quad.top = static_cast<float>(baselineY + yOffset);
    quad.right = quad.left + static_cast<float>(cell.width);
    quad.bottom = quad.top + static_cast<float>(cell.height);
    quad.cellLeft = cell.left;
    quad.cellTop = cell.top;
    quad.texture = &uiGlyphAtlas();
    return true;
}

// One pixel, blended: the arithmetic the interface has always used, in the one
// place now, so a quad drawn on the host and a quad sent to the device cannot
// disagree about what a half-transparent panel looks like.
void NativeFrame::blendPixelAt(int x, int y, int red, int green, int blue, int coverage) {
    if (x < 0 || y < 0 || x >= width || y >= height || coverage <= 0) return;
    if (canvasIsBgra()) makeOverlayWritable();
    else if (rgb.empty()) return;
    const bool bgra = canvasIsBgra();
    unsigned char* pixels = bgra ? canvasBgra() : rgb.data();
    const std::size_t stride = bgra ? 4U : 3U;
    const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
    unsigned char* out = pixels + pixel * stride;
    const int sourceAlpha = std::min(255, coverage);
    if (alpha.empty()) {
        if (bgra) {
            out[0] = static_cast<unsigned char>((blue * sourceAlpha + out[0] * (255 - sourceAlpha)) / 255);
            out[1] = static_cast<unsigned char>((green * sourceAlpha + out[1] * (255 - sourceAlpha)) / 255);
            out[2] = static_cast<unsigned char>((red * sourceAlpha + out[2] * (255 - sourceAlpha)) / 255);
        } else {
            out[0] = static_cast<unsigned char>((red * sourceAlpha + out[0] * (255 - sourceAlpha)) / 255);
            out[1] = static_cast<unsigned char>((green * sourceAlpha + out[1] * (255 - sourceAlpha)) / 255);
            out[2] = static_cast<unsigned char>((blue * sourceAlpha + out[2] * (255 - sourceAlpha)) / 255);
        }
        return;
    }
    const int oldAlpha = alpha[pixel];
    const int weight = sourceAlpha * 255 + oldAlpha * (255 - sourceAlpha);
    if (!weight) return;
    const int redChannel = bgra ? 2 : 0, blueChannel = bgra ? 0 : 2;
    out[redChannel] = static_cast<unsigned char>((red * sourceAlpha * 255 + out[redChannel] * oldAlpha * (255 - sourceAlpha) + weight / 2) / weight);
    out[1] = static_cast<unsigned char>((green * sourceAlpha * 255 + out[1] * oldAlpha * (255 - sourceAlpha) + weight / 2) / weight);
    out[blueChannel] = static_cast<unsigned char>((blue * sourceAlpha * 255 + out[blueChannel] * oldAlpha * (255 - sourceAlpha) + weight / 2) / weight);
    alpha[pixel] = static_cast<unsigned char>((weight + 127) / 255);
}


void UiLayer::rectangle(double x, double y, double width, double height, int red, int green, int blue,
                        double opacity) {
    UiQuad quad;
    quad.left = static_cast<float>(x);
    quad.top = static_cast<float>(y);
    quad.right = static_cast<float>(x + width);
    quad.bottom = static_cast<float>(y + height);
    quad.red = static_cast<unsigned char>(red);
    quad.green = static_cast<unsigned char>(green);
    quad.blue = static_cast<unsigned char>(blue);
    quad.opacity = static_cast<float>(opacity);
    quads_.push_back(quad);
}

void UiLayer::clear(int frameWidth, int frameHeight) {
    quads_.clear();
    width_ = frameWidth;
    height_ = frameHeight;
}

void compositeUiQuad(NativeFrame& frame, const UiQuad& quad) {
    const int firstColumn = std::max(0, static_cast<int>(std::lround(quad.left)));
    const int lastColumn = std::min(frame.width, static_cast<int>(std::lround(quad.right)));
    const int firstRow = std::max(0, static_cast<int>(std::lround(quad.top)));
    const int lastRow = std::min(frame.height, static_cast<int>(std::lround(quad.bottom)));
    if (firstColumn >= lastColumn || firstRow >= lastRow) return;
    const int alpha = static_cast<int>(std::clamp(static_cast<double>(quad.opacity), 0.0, 1.0) * 255);
    const int red = quad.red;
    const int green = quad.green;
    const int blue = quad.blue;
    const bool textured = quad.texture != nullptr && quad.texture->width > 0;
    if (textured) {
        const Texture& texture = *quad.texture;
        const int cellWidth = static_cast<int>(std::lround(quad.right - quad.left));
        const int cellHeight = static_cast<int>(std::lround(quad.bottom - quad.top));
        const int cellLeft = quad.cellLeft;
        const int cellTop = quad.cellTop;
        for (int row = 0; row < cellHeight; ++row) {
            const int y = firstRow + row;
            if (y >= lastRow) break;
            for (int column = 0; column < cellWidth; ++column) {
                const int x = firstColumn + column;
                if (x >= lastColumn) break;
                const int sourceX = cellLeft + column;
                const int sourceY = cellTop + row;
                if (sourceX < 0 || sourceY < 0 || sourceX >= texture.width || sourceY >= texture.height) continue;
                const unsigned char coverage =
                    texture.pixels[(static_cast<std::size_t>(sourceY) * texture.width + sourceX) * 4U + 3U];
                if (!coverage) continue;
                frame.blendPixelAt(x, y, red, green, blue, coverage);
            }
        }
        return;
    }
    for (int y = firstRow; y < lastRow; ++y) {
        for (int x = firstColumn; x < lastColumn; ++x) frame.blendPixelAt(x, y, red, green, blue, alpha);
    }
}

namespace {

// The interface draws in pixels that are already display-encoded; the device
// wants a linear value and tone maps it on the way out. This is that curve
// inverted, so the byte that arrives on screen is the byte the interface was
// designed with. Bisection, because the curve is monotone and small.
double inverseTone(int channel) {
    static const auto table = [] {
        std::array<double, 256> values{};
        for (int i = 0; i < 256; ++i) {
            const double encoded = i / 255.0;
            double low = 0.0, high = 16.0;
            for (int step = 0; step < 40; ++step) {
                const double middle = (low + high) * 0.5;
                const double tone = (middle * (2.51 * middle + 0.03)) / (middle * (2.43 * middle + 0.59) + 0.14);
                const double srgb = tone <= 0.0031308 ? tone * 12.92 : 1.055 * std::pow(tone, 1.0 / 2.4) - 0.055;
                if (srgb < encoded) low = middle; else high = middle;
            }
            values[i] = (low + high) * 0.5;
        }
        return values;
    }();
    return table[std::clamp(channel, 0, 255)];
}

struct BatchKey {
    const Texture* texture = nullptr;
    int red = 0, green = 0, blue = 0;
    int opacity = 0;
    bool operator==(const BatchKey& other) const {
        return texture == other.texture && red == other.red && green == other.green && blue == other.blue &&
               opacity == other.opacity;
    }
};

}  // namespace

void appendUiBatches(const UiLayer& layer, const Camera& camera, GpuScene& scene) {
    const int width = layer.width() > 0 ? layer.width() : scene.width;
    const int height = layer.height() > 0 ? layer.height() : scene.height;
    if (width <= 0 || height <= 0) return;
    const double focal = camera.focal();
    if (focal <= 0.0) return;
    // A hand's width in front of the eye. The interface has no depth of its own;
    // the distance only has to be inside the projection's range and in front of
    // everything the scene draws.
    const double distance = 0.35;
    const auto project = [&](float x, float y) {
        const double viewX = (x - width * 0.5) * distance / focal;
        const double viewY = (height * 0.5 - y) * distance / focal;
        return camera.world({viewX, viewY, distance});
    };

    std::size_t start = 0;
    BatchKey running;
    bool haveRunning = false;
    const auto flush = [&](std::size_t begin, std::size_t end, const BatchKey& key) {
        if (begin >= end) return;
        GpuBatch batch;
        batch.interface = true;
        batch.material.kind = key.texture != nullptr ? Material::Kind::Textured : Material::Kind::Solid;
        batch.material.texture = key.texture;
        batch.material.unlit = true;
        batch.material.constantLighting = true;
        // The inverse of the tone curve, so what the device writes is the byte
        // that was asked for. A flat batch is tone mapped again by the draw call
        // itself, which is the same curve, so this is its pre-image.
        batch.material.color = {inverseTone(key.red), inverseTone(key.green), inverseTone(key.blue)};
        batch.material.opacity = key.opacity / 255.0;
        // A revision of its own: the interface changes every frame, and a batch
        // whose revision never changes is a batch the device draws from memory
        // that no longer matches it.
        static std::uint64_t interfaceRevision = 0;
        batch.revision = ++interfaceRevision;
        for (std::size_t index = begin; index < end; ++index) {
            const UiQuad& quad = layer.quads()[index];
            const Vec3 a = project(quad.left, quad.top), b = project(quad.right, quad.top);
            const Vec3 c = project(quad.right, quad.bottom), d = project(quad.left, quad.bottom);
            // Texture coordinates: the atlas is read as coverage, so the whole
            // cell is sampled and its alpha does the work.
            const float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
            const auto vertex = [&](const Vec3& point, float u, float v) {
                GpuVertex out;
                out.x = static_cast<float>(point.x);
                out.y = static_cast<float>(point.y);
                out.z = static_cast<float>(point.z);
                out.u = u;
                out.v = v;
                return out;
            };
            float quadU0 = u0, quadV0 = v0, quadU1 = u1, quadV1 = v1;
            if (quad.texture != nullptr && quad.texture->width > 0 && quad.texture->height > 0) {
                quadU0 = static_cast<float>(quad.cellLeft) / static_cast<float>(quad.texture->width);
                quadV0 = static_cast<float>(quad.cellTop) / static_cast<float>(quad.texture->height);
                const float cellWidth = quad.right - quad.left;
                const float cellHeight = quad.bottom - quad.top;
                quadU1 = static_cast<float>(quad.cellLeft + cellWidth) / static_cast<float>(quad.texture->width);
                quadV1 = static_cast<float>(quad.cellTop + cellHeight) / static_cast<float>(quad.texture->height);
            }
            batch.vertices.push_back(vertex(a, quadU0, quadV0));
            batch.vertices.push_back(vertex(b, quadU1, quadV0));
            batch.vertices.push_back(vertex(c, quadU1, quadV1));
            batch.vertices.push_back(vertex(a, quadU0, quadV0));
            batch.vertices.push_back(vertex(c, quadU1, quadV1));
            batch.vertices.push_back(vertex(d, quadU0, quadV1));
        }
        scene.batches.push_back(std::move(batch));
    };

    for (std::size_t index = 0; index < layer.quads().size(); ++index) {
        const UiQuad& quad = layer.quads()[index];
        BatchKey key;
        key.texture = quad.texture;
        key.red = quad.red;
        key.green = quad.green;
        key.blue = quad.blue;
        key.opacity = static_cast<int>(std::lround(std::clamp(static_cast<double>(quad.opacity), 0.0, 1.0) * 255));
        if (!haveRunning) {
            running = key;
            haveRunning = true;
            start = index;
            continue;
        }
        if (!(key == running)) {
            flush(start, index, running);
            running = key;
            start = index;
        }
    }
    flush(start, layer.quads().size(), running);
}

void compositeUi(NativeFrame& frame, const UiLayer& layer) {
    for (const UiQuad& quad : layer.quads()) compositeUiQuad(frame, quad);
}

}  // namespace gtc::native
