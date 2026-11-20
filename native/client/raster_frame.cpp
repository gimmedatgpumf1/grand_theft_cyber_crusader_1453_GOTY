#include "client/raster.h"

#include "client/raster_color.h"
#include "client/threadpool.h"
#include "client/ui_layer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace gtc::native {

using namespace raster_detail;

void rgbToBgra(const NativeFrame& frame, std::vector<unsigned char>& out) {
    const std::size_t count = static_cast<std::size_t>(std::max(0, frame.width)) *
        static_cast<std::size_t>(std::max(0, frame.height));
    out.resize(count * 4);
    if (count == 0 || frame.rgb.size() < count * 3) return;
    const auto convert = [&](std::size_t begin, std::size_t end) {
        const unsigned char* source = frame.rgb.data() + begin * 3;
        unsigned char* target = out.data() + begin * 4;
        for (std::size_t index = begin; index < end; ++index, source += 3, target += 4) {
            target[0] = source[2];
            target[1] = source[1];
            target[2] = source[0];
            target[3] = 255;
        }
    };
    // Below about a quarter of a megapixel the split costs more than it saves.
    FramePool& pool = framePool();
    const std::size_t chunks = std::min<std::size_t>(pool.size(), std::max<std::size_t>(1, count / 131072));
    pool.ranges(chunks, count, convert);
}

void NativeFrame::syncToRgb() {
    if (!rgb.empty()) return;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const unsigned char* source = direct ? direct : bgra.data();
    if (!source) return;
    if (!direct && bgra.size() < count * 4) return;
    rgb.resize(count * 3);
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char* in = source + index * 4;
        unsigned char* out = rgb.data() + index * 3;
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
    }
}

bool NativeFrame::save(const std::string& path) const {
    const_cast<NativeFrame*>(this)->syncToRgb();
    std::error_code error;
    const std::filesystem::path destination(path);
    if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path(), error);
    return stbi_write_png(path.c_str(), width, height, 3, rgb.data(), width * 3) != 0;
}

void NativeFrame::logicalUi(int logicalWidth, int logicalHeight) {
    uiScale = std::min(1.0, std::min(width / static_cast<double>(logicalWidth), height / static_cast<double>(logicalHeight)));
    uiOffsetX = (width - logicalWidth * uiScale) * 0.5;
    uiOffsetY = 0.0;
}

void NativeFrame::makeOverlayWritable() {
    // A frame that arrived as a mapping is drawn on where it lies. Copying it
    // first cost 5.8 MB a frame at 1568x929 -- over the host, on the bus, one
    // memcpy -- so the overlay could be written into a second buffer and then
    // handed to the window anyway.
    if (direct != nullptr) return;
}

double rectangleMilliseconds = 0.0;
long rectanglePixels = 0;

void NativeFrame::rectangle(int x, int y, int w, int h, Color color) {
    if (ui != nullptr) {
        const int red = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
        const int green = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
        const int blue = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
        ui->rectangle(uiOffsetX + x * uiScale, uiOffsetY + y * uiScale, w * uiScale, h * uiScale, red, green,
                      blue, 1.0);
        return;
    }
    const auto rectangleStart = std::chrono::steady_clock::now();
    // A frame that arrived in window order is drawn on where it lies: converting
    // it to rgb and back is two passes over a million pixels for nothing.
    if (canvasIsBgra()) makeOverlayWritable();
    else syncToRgb();
    const bool bgra = canvasIsBgra();
    unsigned char* pixels = bgra ? canvasBgra() : rgb.data();
    const int r = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
    const int g = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
    const int b = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
    const int left = static_cast<int>(std::lround(uiOffsetX + x * uiScale));
    const int top = static_cast<int>(std::lround(uiOffsetY + y * uiScale));
    const int right = static_cast<int>(std::lround(uiOffsetX + (x + w) * uiScale));
    const int bottom = static_cast<int>(std::lround(uiOffsetY + (y + h) * uiScale));
    const int firstRow = std::max(0, top), lastRow = std::min(height, bottom);
    const int firstColumn = std::max(0, left), lastColumn = std::min(width, right);
    if (firstColumn >= lastColumn) return;
    // The panels are most of the screen: six hundred thousand pixels a frame,
    // filled one at a time through a bounds check and a stride multiply. A row
    // of one colour is a move, so each row is written as words here.
    if (bgra) {
        // Window order is blue, green, red, and the fourth byte says opaque:
        // the pixel being written is a colour, not a hole.
        const std::uint32_t word = (static_cast<std::uint32_t>(r) << 16) |
                                   (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b) |
                                   0xFF000000U;
        for (int py = firstRow; py < lastRow; ++py) {
            std::fill_n(reinterpret_cast<std::uint32_t*>(pixels) + static_cast<std::size_t>(py) * width +
                            firstColumn,
                        static_cast<std::size_t>(lastColumn - firstColumn), word);
        }
    } else {
        for (int py = firstRow; py < lastRow; ++py) {
            unsigned char* out = pixels + (static_cast<std::size_t>(py) * width + firstColumn) * 3U;
            for (int px = firstColumn; px < lastColumn; ++px, out += 3) {
                out[0] = static_cast<unsigned char>(r);
                out[1] = static_cast<unsigned char>(g);
                out[2] = static_cast<unsigned char>(b);
            }
        }
    }
    if (!alpha.empty()) {
        for (int py = firstRow; py < lastRow; ++py) {
            std::fill_n(alpha.begin() + static_cast<std::size_t>(py) * width + firstColumn,
                        static_cast<std::size_t>(lastColumn - firstColumn), static_cast<unsigned char>(255));
        }
    }
    rectanglePixels += static_cast<long>(lastRow - firstRow) * (lastColumn - firstColumn);
    rectangleMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - rectangleStart).count();
}

void NativeFrame::blendRectangle(int x, int y, int w, int h, Color color, double opacity) {
    if (ui != nullptr) {
        const int red = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
        const int green = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
        const int blue = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
        ui->rectangle(uiOffsetX + x * uiScale, uiOffsetY + y * uiScale, w * uiScale, h * uiScale, red, green,
                      blue, opacity);
        return;
    }
    if (canvasIsBgra()) makeOverlayWritable();
    else syncToRgb();
    const bool bgra = canvasIsBgra();
    unsigned char* pixels = bgra ? canvasBgra() : rgb.data();
    const std::size_t stride = bgra ? 4U : 3U;
    const int r = static_cast<int>(std::clamp(toSrgb(color.r), 0.0, 1.0) * 255);
    const int g = static_cast<int>(std::clamp(toSrgb(color.g), 0.0, 1.0) * 255);
    const int b = static_cast<int>(std::clamp(toSrgb(color.b), 0.0, 1.0) * 255);
    const int alpha = static_cast<int>(std::clamp(opacity, 0.0, 1.0) * 255);
    const int left = static_cast<int>(std::lround(uiOffsetX + x * uiScale));
    const int top = static_cast<int>(std::lround(uiOffsetY + y * uiScale));
    const int right = static_cast<int>(std::lround(uiOffsetX + (x + w) * uiScale));
    const int bottom = static_cast<int>(std::lround(uiOffsetY + (y + h) * uiScale));
    const int firstRow = std::max(0, top), lastRow = std::min(height, bottom);
    const int firstColumn = std::max(0, left), lastColumn = std::min(width, right);
    if (firstColumn < lastColumn && this->alpha.empty()) {
        // Two panels of translucent colour are nine hundred thousand blended
        // pixels a frame, a third of the street's host time. The colour and the
        // opacity are fixed for the call, so the blend is a byte-to-byte table
        // that is built once: no multiply, no divide, one store a channel.
        unsigned char red_[256], green_[256], blue_[256];
        for (int value = 0; value < 256; ++value) {
            red_[value] = static_cast<unsigned char>((r * alpha + value * (255 - alpha)) / 255);
            green_[value] = static_cast<unsigned char>((g * alpha + value * (255 - alpha)) / 255);
            blue_[value] = static_cast<unsigned char>((b * alpha + value * (255 - alpha)) / 255);
        }
        // The stride is the frame's, not four: an rgb canvas is three bytes a
        // pixel, and walking it four at a time wrote the panel diagonally across
        // the picture instead of into the rectangle that was asked for.
        for (int py = firstRow; py < lastRow; ++py) {
            unsigned char* out = pixels + static_cast<std::size_t>(py) * width * stride + firstColumn * stride;
            for (int px = firstColumn; px < lastColumn; ++px, out += stride) {
                out[0] = bgra ? blue_[out[0]] : red_[out[0]];
                out[1] = green_[out[1]];
                out[2] = bgra ? red_[out[2]] : blue_[out[2]];
            }
        }
        return;
    }
    for (int py = firstRow; py < lastRow; ++py) {
        for (int px = firstColumn; px < lastColumn; ++px) {
            const std::size_t pixel = static_cast<std::size_t>(py) * width + px;
            unsigned char* out = pixels + pixel * stride;
            if (this->alpha.empty()) {
                if (bgra) {
                    out[0] = static_cast<unsigned char>((b * alpha + out[0] * (255 - alpha)) / 255);
                    out[1] = static_cast<unsigned char>((g * alpha + out[1] * (255 - alpha)) / 255);
                    out[2] = static_cast<unsigned char>((r * alpha + out[2] * (255 - alpha)) / 255);
                } else {
                    out[0] = static_cast<unsigned char>((r * alpha + out[0] * (255 - alpha)) / 255);
                    out[1] = static_cast<unsigned char>((g * alpha + out[1] * (255 - alpha)) / 255);
                    out[2] = static_cast<unsigned char>((b * alpha + out[2] * (255 - alpha)) / 255);
                }
            } else {
                const int oldAlpha = this->alpha[pixel];
                const int weight = alpha * 255 + oldAlpha * (255 - alpha);
                if (!weight) continue;
                const int red = bgra ? 2 : 0, blue = bgra ? 0 : 2;
                out[red] = static_cast<unsigned char>((r * alpha * 255 + out[red] * oldAlpha * (255 - alpha) + weight / 2) / weight);
                out[1] = static_cast<unsigned char>((g * alpha * 255 + out[1] * oldAlpha * (255 - alpha) + weight / 2) / weight);
                out[blue] = static_cast<unsigned char>((b * alpha * 255 + out[blue] * oldAlpha * (255 - alpha) + weight / 2) / weight);
                this->alpha[pixel] = static_cast<unsigned char>((weight + 127) / 255);
            }
        }
    }
}

}  // namespace gtc::native
