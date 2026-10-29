#include "client/ui_layer.h"

#include "client/interface_style.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace gtc::native {
namespace {

// Glyphs are packed by a shelf packer into one texture of fixed width that grows
// downwards. The interface asks for two or three sizes, so the pack happens on
// the first frames at a new window size and never again.
struct Cell {
    int left = 0, top = 0, width = 0, height = 0;
};

struct Atlas {
    Texture texture;
    std::map<unsigned, Cell> cells;   // key is size << 8 | character
    std::vector<int> sizes;
    int cursorX = 0;      // packing cursor
    int cursorY = 0;
    int rowHeight = 0;
    int usedHeight = 0;
    bool dirty = true;
};

constexpr int kAtlasWidth = 1024;
constexpr int kAtlasPadding = 2;

Atlas& atlas() {
    static Atlas instance;
    return instance;
}

unsigned keyOf(char character, int pixelHeight) {
    return (static_cast<unsigned>(pixelHeight) << 8) | static_cast<unsigned>(character);
}

// Places one size's glyphs at the cursor. The width is fixed, so a size that does
// not fit on the current shelf starts a new one -- there is no bookkeeping per
// size and nothing to get wrong when a second size arrives after the first.
void addSize(Atlas& value, int pixelHeight) {
    constexpr int kFirst = 32, kLast = 126;
    for (int code = kFirst; code <= kLast; ++code) {
        const char character = static_cast<char>(code);
        int glyphWidth = 0, glyphHeight = 0, xOffset = 0, yOffset = 0;
        const unsigned char* coverage = nullptr;
        if (!glyphCoverage(character, pixelHeight, glyphWidth, glyphHeight, xOffset, yOffset, &coverage)) {
            continue;
        }
        if (value.cursorX + glyphWidth + kAtlasPadding > kAtlasWidth) {
            value.cursorY += value.rowHeight + kAtlasPadding;
            value.cursorX = kAtlasPadding;
            value.rowHeight = 0;
        }
        Cell cell;
        cell.left = value.cursorX;
        cell.top = value.cursorY;
        cell.width = glyphWidth;
        cell.height = glyphHeight;
        value.cells[keyOf(character, pixelHeight)] = cell;
        value.cursorX += glyphWidth + kAtlasPadding;
        value.rowHeight = std::max(value.rowHeight, glyphHeight);
        value.usedHeight = std::max(value.usedHeight, cell.top + glyphHeight + kAtlasPadding);
    }
    value.cursorY += value.rowHeight + kAtlasPadding;
    value.cursorX = kAtlasPadding;
    value.rowHeight = 0;
    value.texture.width = kAtlasWidth;
    value.texture.height = std::max(64, value.usedHeight + kAtlasPadding);
    value.dirty = true;
}

void rebuildPixels(Atlas& value) {
    value.texture.pixels.assign(static_cast<std::size_t>(value.texture.width) * value.texture.height * 4U, 0);
    for (const auto& entry : value.cells) {
        const int pixelHeight = static_cast<int>(entry.first >> 8);
        const char character = static_cast<char>(entry.first & 0xFFu);
        const Cell& cell = entry.second;
        int glyphWidth = 0, glyphHeight = 0, xOffset = 0, yOffset = 0;
        const unsigned char* coverage = nullptr;
        if (!glyphCoverage(character, pixelHeight, glyphWidth, glyphHeight, xOffset, yOffset, &coverage)) continue;
        for (int row = 0; row < cell.height; ++row) {
            if (cell.top + row >= value.texture.height) break;
            for (int column = 0; column < cell.width; ++column) {
                if (cell.left + column >= value.texture.width) break;
                const std::size_t at = (static_cast<std::size_t>(cell.top + row) * value.texture.width +
                                        (cell.left + column)) * 4U;
                const unsigned char covered = coverage[static_cast<std::size_t>(row) * cell.width + column];
                value.texture.pixels[at] = 255;       // white: the colour comes from the quad
                value.texture.pixels[at + 1] = 255;
                value.texture.pixels[at + 2] = 255;
                value.texture.pixels[at + 3] = covered;   // the bit that matters
            }
        }
    }
    value.dirty = false;
}

const Cell* findCell(char character, int pixelHeight) {
    Atlas& value = atlas();
    const auto found = value.cells.find(keyOf(character, pixelHeight));
    if (found != value.cells.end()) return &found->second;
    if (std::find(value.sizes.begin(), value.sizes.end(), pixelHeight) != value.sizes.end()) return nullptr;
    value.sizes.push_back(pixelHeight);
    addSize(value, pixelHeight);
    const auto again = value.cells.find(keyOf(character, pixelHeight));
    return again == value.cells.end() ? nullptr : &again->second;
}

std::uint64_t& revisionRef() {
    static std::uint64_t revision = 1;
    return revision;
}

}  // namespace

const Texture& uiGlyphAtlas() {
    Atlas& value = atlas();
    if (value.texture.width == 0) {
        value.texture.width = kAtlasWidth;
        value.texture.height = 64;
        value.cursorX = kAtlasPadding;
        value.cursorY = kAtlasPadding;
        value.dirty = true;
    }
    if (value.dirty) {
        rebuildPixels(value);
        ++revisionRef();
    }
    return value.texture;
}

std::uint64_t uiGlyphAtlasRevision() {
    uiGlyphAtlas();   // makes sure a never-used atlas has a revision and pixels
    return revisionRef();
}

bool uiGlyphQuad(UiQuad& quad, char character, int pixelHeight, int penX, int baselineY) {
    if (pixelHeight <= 0) return false;
    const Cell* cell = findCell(character, pixelHeight);
    if (cell == nullptr) return false;
    if (cell->width <= 0 || cell->height <= 0) return false;
    const Texture& texture = uiGlyphAtlas();
    if (texture.width <= 0 || texture.height <= 0) return false;
    // The pixel blitter placed a glyph at round(penX) + xOffset and
    // round(baseline) + yOffset; this places the same rectangle.
    int glyphWidth = 0, glyphHeight = 0, xOffset = 0, yOffset = 0;
    const unsigned char* coverage = nullptr;
    if (!glyphCoverage(character, pixelHeight, glyphWidth, glyphHeight, xOffset, yOffset, &coverage)) return false;
    quad.left = static_cast<float>(penX + xOffset);
    quad.top = static_cast<float>(baselineY + yOffset);
    quad.right = quad.left + static_cast<float>(cell->width);
    quad.bottom = quad.top + static_cast<float>(cell->height);
    quad.cellLeft = cell->left;
    quad.cellTop = cell->top;
    quad.texture = &texture;
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

void compositeUi(NativeFrame& frame, const UiLayer& layer) {
    for (const UiQuad& quad : layer.quads()) compositeUiQuad(frame, quad);
}

}  // namespace gtc::native
