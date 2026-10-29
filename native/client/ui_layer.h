#pragma once

// The interface, described as geometry instead of as pixels.
//
// The pieces of the interface -- the strip, the panel, the words -- used to be
// drawn straight into the picture on the host, which is why every frame had to
// come back out of the device and be blitted to the screen by the window. They
// write quads now. On a machine with a device the quads become batches and go
// through the same pipeline as the room; with no device they are composited by
// `compositeUi`, which is also what the checks measure, so the two paths are
// describing one picture rather than two opinions about it.

#include "client/raster.h"

#include <string>

namespace gtc::native {

// Pixel coordinates, top left origin, as the interface has always been laid out.
// A coloured quad has no texture; a glyph carries a rectangle into the atlas.
struct UiQuad {
    float left = 0, top = 0, right = 0, bottom = 0;
    // Already encoded to the bytes the screen takes, by the same conversion the
    // pixel blitter applied, so a quad and a pixel are the same number.
    unsigned char red = 0, green = 0, blue = 0;
    float opacity = 1.0f;   // 0..1, as a byte's worth of coverage
    const Texture* texture = nullptr;
    // Where the glyph sits in the atlas, in its pixels. Kept as pixels rather
    // than as texture coordinates because the atlas grows when a new text size is
    // first used, and a coordinate worked out before that growth would point at
    // the wrong part of the texture -- which is exactly what it did.
    int cellLeft = 0, cellTop = 0;
};

class UiLayer {
public:
    void clear(int frameWidth, int frameHeight);
    bool empty() const { return quads_.empty(); }
    const std::vector<UiQuad>& quads() const { return quads_; }
    int width() const { return width_; }
    int height() const { return height_; }
    void add(const UiQuad& quad) { quads_.push_back(quad); }
    // A flat rectangle in interface pixels, already through the frame transform.
    void rectangle(double x, double y, double width, double height, int red, int green, int blue, double opacity);

private:
    std::vector<UiQuad> quads_;
    int width_ = 0;
    int height_ = 0;
};

// The glyphs, packed once into one texture. Built lazily by size: the interface
// asks for two or three sizes and a size that has never been used is a repack,
// which happens on the frame a new window size first arrives and never again.
const Texture& uiGlyphAtlas();
// Bumped whenever the atlas is rebuilt, so a device knows to upload it again.
std::uint64_t uiGlyphAtlasRevision();

// The quad for one glyph, positioned like the pixel blitter would have drawn it.
// False when there is no face on disk or the character has no coverage.
bool uiGlyphQuad(UiQuad& quad, char character, int pixelHeight, int penX, int baselineY);

// The atlas as a device texture: white with the coverage in alpha, so the
// fragment stage only has to multiply by the colour of the quad.

// Composites the quads into the picture exactly as the host used to draw them.
// This is the fallback on a machine with no device, and the reference the checks
// compare the geometry against.
void compositeUi(NativeFrame& frame, const UiLayer& layer);

// One quad's worth of the blend, in pixels, with the same arithmetic the pixel
// blitter used. Exposed for the check that proves the two agree.
void compositeUiQuad(NativeFrame& frame, const UiQuad& quad);

}  // namespace gtc::native
