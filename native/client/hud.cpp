#include "client/raster.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"

namespace gtc::native {
namespace {

std::array<unsigned char, 7> glyph(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
        case 'A': return {14, 17, 17, 31, 17, 17, 17};
        case 'B': return {30, 17, 17, 30, 17, 17, 30};
        case 'C': return {14, 17, 16, 16, 16, 17, 14};
        case 'D': return {30, 17, 17, 17, 17, 17, 30};
        case 'E': return {31, 16, 16, 30, 16, 16, 31};
        case 'F': return {31, 16, 16, 30, 16, 16, 16};
        case 'G': return {14, 17, 16, 23, 17, 17, 14};
        case 'H': return {17, 17, 17, 31, 17, 17, 17};
        case 'I': return {14, 4, 4, 4, 4, 4, 14};
        case 'J': return {7, 2, 2, 2, 18, 18, 12};
        case 'K': return {17, 18, 20, 24, 20, 18, 17};
        case 'L': return {16, 16, 16, 16, 16, 16, 31};
        case 'M': return {17, 27, 21, 21, 17, 17, 17};
        case 'N': return {17, 25, 21, 19, 17, 17, 17};
        case 'O': return {14, 17, 17, 17, 17, 17, 14};
        case 'P': return {30, 17, 17, 30, 16, 16, 16};
        case 'Q': return {14, 17, 17, 17, 21, 18, 13};
        case 'R': return {30, 17, 17, 30, 20, 18, 17};
        case 'S': return {15, 16, 16, 14, 1, 1, 30};
        case 'T': return {31, 4, 4, 4, 4, 4, 4};
        case 'U': return {17, 17, 17, 17, 17, 17, 14};
        case 'V': return {17, 17, 17, 17, 17, 10, 4};
        case 'W': return {17, 17, 17, 21, 21, 21, 10};
        case 'X': return {17, 17, 10, 4, 10, 17, 17};
        case 'Y': return {17, 17, 10, 4, 4, 4, 4};
        case 'Z': return {31, 1, 2, 4, 8, 16, 31};
        case '0': return {14, 17, 19, 21, 25, 17, 14};
        case '1': return {4, 12, 4, 4, 4, 4, 14};
        case '2': return {14, 17, 1, 2, 4, 8, 31};
        case '3': return {30, 1, 1, 14, 1, 1, 30};
        case '4': return {2, 6, 10, 18, 31, 2, 2};
        case '5': return {31, 16, 16, 30, 1, 1, 30};
        case '6': return {14, 16, 16, 30, 17, 17, 14};
        case '7': return {31, 1, 2, 4, 8, 8, 8};
        case '8': return {14, 17, 17, 14, 17, 17, 14};
        case '9': return {14, 17, 17, 15, 1, 1, 14};
        case ':': return {0, 4, 4, 0, 4, 4, 0};
        case '.': return {0, 0, 0, 0, 0, 6, 6};
        case ',': return {0, 0, 0, 0, 4, 4, 8};
        case '-': return {0, 0, 0, 31, 0, 0, 0};
        case '+': return {0, 4, 4, 31, 4, 4, 0};
        case '/': return {1, 2, 2, 4, 8, 8, 16};
        case '%': return {17, 2, 2, 4, 8, 8, 17};
        case '[': return {14, 8, 8, 8, 8, 8, 14};
        case ']': return {14, 2, 2, 2, 2, 2, 14};
        case '(': return {2, 4, 8, 8, 8, 4, 2};
        case ')': return {8, 4, 2, 2, 2, 4, 8};
        case '!': return {4, 4, 4, 4, 4, 0, 4};
        case '?': return {14, 17, 1, 2, 4, 0, 4};
        case '=': return {0, 0, 31, 0, 31, 0, 0};
        case '_': return {0, 0, 0, 0, 0, 0, 31};
        default: return {0, 0, 0, 0, 0, 0, 0};
    }
}

struct Font {
    stbtt_fontinfo info{};
    std::vector<unsigned char> bytes;
    bool ready = false;
    Font() {
        std::ifstream input("native/assets/ui-sans.ttf", std::ios::binary);
        if (!input) return;
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        ready = !bytes.empty() && stbtt_InitFont(&info, bytes.data(), 0) != 0;
    }
};

Font& font() {
    static Font instance;
    return instance;
}

struct Glyph {
    int width = 0;
    int height = 0;
    int xOffset = 0;
    int yOffset = 0;
    double advance = 0;
    std::vector<unsigned char> coverage;
};

const Glyph& cachedGlyph(char character, int pixelHeight) {
    const unsigned key = (static_cast<unsigned>(pixelHeight) << 8) | static_cast<unsigned char>(character);
    static std::map<unsigned, Glyph> cache;
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    Glyph glyph;
    const float fontScale = stbtt_ScaleForPixelHeight(&font().info, static_cast<float>(pixelHeight));
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBox(&font().info, character, fontScale, fontScale, &x0, &y0, &x1, &y1);
    glyph.width = std::max(0, x1 - x0);
    glyph.height = std::max(0, y1 - y0);
    glyph.xOffset = x0;
    glyph.yOffset = y0;
    glyph.coverage.resize(static_cast<std::size_t>(glyph.width) * glyph.height);
    if (!glyph.coverage.empty()) {
        stbtt_MakeCodepointBitmap(&font().info, glyph.coverage.data(), glyph.width, glyph.height,
                                  glyph.width, fontScale, fontScale, character);
    }
    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(&font().info, character, &advance, &bearing);
    glyph.advance = advance * fontScale;
    return cache.emplace(key, std::move(glyph)).first->second;
}

// The same glyphs, packed into one image for the device: white light and the
// coverage in the alpha channel, so a glyph is a quad with a texture coordinate
// and the blending is the hardware's job instead of the host's.
struct GlyphAtlas {
    Texture texture;
    std::map<char, std::array<float, 4>> uv;
};

// One attempt at packing the printable set into an image of this height. A shelf
// packer wastes the end of every row, so how much ink there is does not say how
// tall the image has to be: it either fits or it does not, and the caller grows
// it until it does.
bool packAtlas(GlyphAtlas& atlas, int pixelHeight, int width, int height) {
    atlas.texture.width = width;
    atlas.texture.height = height;
    atlas.texture.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
    atlas.uv.clear();
    int penX = 1, penY = 1, rowHeight = 0;
    for (char character = 32; character < 127; ++character) {
        const Glyph& glyph = cachedGlyph(character, pixelHeight);
        if (glyph.width <= 0 || glyph.height <= 0) continue;
        if (penX + glyph.width + 1 > width) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + glyph.height + 1 > height) return false;
        for (int row = 0; row < glyph.height; ++row) {
            for (int column = 0; column < glyph.width; ++column) {
                const unsigned coverage = glyph.coverage[static_cast<std::size_t>(row) * glyph.width + column];
                if (!coverage) continue;
                unsigned char* out = atlas.texture.pixels.data() +
                    (static_cast<std::size_t>(penY + row) * width + penX + column) * 4;
                out[0] = out[1] = out[2] = 255;
                out[3] = static_cast<unsigned char>(coverage);
            }
        }
        atlas.uv[character] = {static_cast<float>(penX) / width, static_cast<float>(penY) / height,
                               static_cast<float>(penX + glyph.width) / width,
                               static_cast<float>(penY + glyph.height) / height};
        penX += glyph.width + 1;
        rowHeight = std::max(rowHeight, glyph.height);
    }
    return true;
}

const GlyphAtlas* atlasFor(int pixelHeight) {
    static std::map<int, GlyphAtlas> atlases;
    const auto found = atlases.find(pixelHeight);
    if (found != atlases.end()) return found->second.texture.width > 0 ? &found->second : nullptr;
    GlyphAtlas& atlas = atlases[pixelHeight];
    for (int height = 64; height <= 4096; height *= 2) {
        if (packAtlas(atlas, pixelHeight, 512, height)) return &atlas;
    }
    // Nothing was written: the entry says so, and the next caller gets the same
    // answer instead of packing it all again.
    atlas.texture.width = 0;
    atlas.texture.height = 0;
    atlas.texture.pixels.clear();
    atlas.uv.clear();
    return nullptr;
}

unsigned char srgbChannel(double value) {
    value = std::max(0.0, std::min(1.0, value));
    return static_cast<unsigned char>((value <= 0.0031308 ? value * 12.92 :
        1.055 * std::pow(value, 1.0 / 2.4) - 0.055) * 255);
}

} // namespace

double textMilliseconds = 0.0;
long textPixels = 0;

void drawText(NativeFrame& frame, int x, int y, const std::string& text, Color color, int scale) {
    const auto textStart = std::chrono::steady_clock::now();
    scale = std::max(3, scale);
    // Glyphs go into whatever this frame is made of. A frame that came back from
    // the device in window order is written in window order, so the presenter has
    // nothing left to convert.
    // A frame that goes to the window from the device gets the overlay as quads:
    // glyphs are boxes with a coverage mask, and the device blends them while it
    // still owns the picture.
    if (frame.uiOnGpu) {
        const int pixelHeight = std::max(20, static_cast<int>(std::lround(9 * scale * frame.uiScale)));
        const GlyphAtlas* atlas = font().ready ? atlasFor(pixelHeight) : nullptr;
        if (atlas != nullptr) {
            const float fontScale = stbtt_ScaleForPixelHeight(&font().info, static_cast<float>(pixelHeight));
            int ascent = 0, descent = 0, gap = 0;
            stbtt_GetFontVMetrics(&font().info, &ascent, &descent, &gap);
            double penX = frame.uiOffsetX + x * frame.uiScale;
            const double baseline = frame.uiOffsetY + y * frame.uiScale + ascent * fontScale;
            const int red = srgbChannel(color.r), green = srgbChannel(color.g), blue = srgbChannel(color.b);
            for (char character : text) {
                const Glyph& glyph = cachedGlyph(character, pixelHeight);
                const auto uv = atlas->uv.find(character);
                if (uv != atlas->uv.end() && glyph.width > 0 && glyph.height > 0) {
                    const int left = static_cast<int>(std::lround(penX)) + glyph.xOffset;
                    const int top = static_cast<int>(std::lround(baseline)) + glyph.yOffset;
                    frame.pushUi(left, top, left + glyph.width, top + glyph.height, red, green, blue, 255,
                                 &atlas->texture, uv->second[0], uv->second[1], uv->second[2], uv->second[3]);
                }
                penX += glyph.advance;
            }
            return;
        }
    }
    // A frame whose overlay is drawn by the device has no pixels of its own: no
    // atlas means the blocky font below, which is geometry like everything else,
    // and never the loop that would write into a canvas that does not exist.
    if (frame.uiOnGpu) {
        for (char character : text) {
            const auto rows = glyph(character);
            for (int row = 0; row < 7; ++row) for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (1U << (4 - column))) != 0) {
                    frame.rectangle(x + column * scale, y + row * scale, scale, scale, color);
                }
            }
            x += 6 * scale;
        }
        textMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - textStart).count();
        return;
    }
    if (frame.canvasIsBgra()) frame.makeOverlayWritable();
    const bool bgraCanvas = frame.canvasIsBgra();
    if (font().ready) {
        const int pixelHeight = std::max(20, static_cast<int>(std::lround(9 * scale * frame.uiScale)));
        const float fontScale = stbtt_ScaleForPixelHeight(&font().info, static_cast<float>(pixelHeight));
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&font().info, &ascent, &descent, &gap);
        double penX = frame.uiOffsetX + x * frame.uiScale;
        const double baseline = frame.uiOffsetY + y * frame.uiScale + ascent * fontScale;
        const unsigned char red = srgbChannel(color.r), green = srgbChannel(color.g), blue = srgbChannel(color.b);
        // The canvas and the frame's size are read once, outside the glyph loop.
        // Inside it they were a call and a member load per pixel -- and because
        // the writes go through a pointer the compiler cannot prove does not
        // alias the frame, it could not keep either of them in a register.
        unsigned char* const canvas = bgraCanvas ? frame.canvasBgra() : frame.rgb.data();
        unsigned char* const mask = frame.alpha.empty() ? nullptr : frame.alpha.data();
        const int frameWidth = frame.width, frameHeight = frame.height;
        for (char character : text) {
            const Glyph& glyph = cachedGlyph(character, pixelHeight);
            const int left = static_cast<int>(std::lround(penX)) + glyph.xOffset;
            const int top = static_cast<int>(std::lround(baseline)) + glyph.yOffset;
            for (int row = 0; row < glyph.height; ++row) for (int column = 0; column < glyph.width; ++column) {
                const int px = left + column, py = top + row;
                if (px < 0 || py < 0 || px >= frameWidth || py >= frameHeight) continue;
                const unsigned coverage = glyph.coverage[static_cast<std::size_t>(row) * glyph.width + column];
                if (!coverage) continue;
                const std::size_t pixel = static_cast<std::size_t>(py) * frameWidth + px;
                ++textPixels;
                if (bgraCanvas) {
                    unsigned char* out = canvas + pixel * 4;
                    out[0] = static_cast<unsigned char>((blue * coverage + out[0] * (255 - coverage)) / 255);
                    out[1] = static_cast<unsigned char>((green * coverage + out[1] * (255 - coverage)) / 255);
                    out[2] = static_cast<unsigned char>((red * coverage + out[2] * (255 - coverage)) / 255);
                    continue;
                }
                const std::size_t offset = pixel * 3;
                if (mask == nullptr) {
                    canvas[offset] = static_cast<unsigned char>((red * coverage + canvas[offset] * (255 - coverage)) / 255);
                    canvas[offset + 1] = static_cast<unsigned char>((green * coverage + canvas[offset + 1] * (255 - coverage)) / 255);
                    canvas[offset + 2] = static_cast<unsigned char>((blue * coverage + canvas[offset + 2] * (255 - coverage)) / 255);
                } else {
                    const int sourceAlpha = static_cast<int>(coverage);
                    const int oldAlpha = mask[pixel];
                    const int weight = sourceAlpha * 255 + oldAlpha * (255 - sourceAlpha);
                    if (!weight) continue;
                    canvas[offset] = static_cast<unsigned char>((red * sourceAlpha * 255 + canvas[offset] * oldAlpha * (255 - sourceAlpha) + weight / 2) / weight);
                    canvas[offset + 1] = static_cast<unsigned char>((green * sourceAlpha * 255 + canvas[offset + 1] * oldAlpha * (255 - sourceAlpha) + weight / 2) / weight);
                    canvas[offset + 2] = static_cast<unsigned char>((blue * sourceAlpha * 255 + canvas[offset + 2] * oldAlpha * (255 - sourceAlpha) + weight / 2) / weight);
                    mask[pixel] = static_cast<unsigned char>((weight + 127) / 255);
                }
            }
            penX += glyph.advance;
        }
        textMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - textStart).count();
        return;
    }
    for (char character : text) {
        const auto rows = glyph(character);
        for (int row = 0; row < 7; ++row) for (int column = 0; column < 5; ++column) {
            if ((rows[row] & (1U << (4 - column))) != 0) {
                frame.rectangle(x + column * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 6 * scale;
    }
    textMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - textStart).count();
}

} // namespace gtc::native
