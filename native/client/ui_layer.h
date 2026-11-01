#pragma once

// The interface, described as geometry instead of as pixels.
//
// The pieces of the interface -- the strip, the panel, the words -- used to be
// drawn straight into the picture on the host, which is why every frame had to
// come back out of the device and be blitted to the screen by the window. They
// write quads now. The interactive client submits them to Vulkan with the
// world; `compositeUi` is a CPU reference for tests and headless captures.

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
    // Where the glyph sits in the immutable atlas, in atlas pixels.
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

// Antialiased coverage for the game's proportional face, prebuilt offline.
// Runtime layout uses font metrics only; Vulkan samples this immutable asset.
const Texture& uiGlyphAtlas();
// Stable revision of the immutable font asset.
std::uint64_t uiGlyphAtlasRevision();

// The quad for one glyph, positioned like the pixel blitter would have drawn it.
// False when there is no face on disk or the character has no coverage.
bool uiGlyphQuad(UiQuad& quad, char character, int pixelHeight, int penX, int baselineY);


// Composites the quads into the picture exactly as the host used to draw them.
// CPU reference for checks and explicitly CPU-rendered headless captures.
void compositeUi(NativeFrame& frame, const UiLayer& layer);

// One quad's worth of the blend, in pixels, with the same arithmetic the pixel
// blitter used. Exposed for the check that proves the two agree.
void compositeUiQuad(NativeFrame& frame, const UiQuad& quad);

// Turns the quads into batches the device draws: screen pixels become world
// points a fixed distance in front of the eye, through the camera the frame was
// built with, so the existing projection puts them exactly where the host would
// have drawn them. Consecutive quads that share a colour and a texture go in one
// batch; the interface is a few dozen of them.
void appendUiBatches(const UiLayer& layer, const Camera& camera, GpuScene& scene);

}  // namespace gtc::native
