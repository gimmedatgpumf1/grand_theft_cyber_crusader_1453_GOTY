// The overlay as geometry.
//
// Every panel, label and crosshair pixel the HUD draws is a rectangle of the
// screen with a colour and -- for text -- a rectangle of an image. When the frame
// is going to the window from the device those rectangles are collected here and
// handed over as geometry, so the device blends them into the picture it is
// already drawing. That is the difference between a frame that is finished on the
// device and a frame that has to be copied back to the host for a speedometer.

#include "client/raster.h"

#include <cmath>
#include <map>

namespace gtc::native {
namespace {

// Every overlay quad samples something, so the panel fills and the crosshair
// sample this one white texel: one pipeline, one path, no second way to be wrong.
const Texture& whiteTexel() {
    static const Texture white = [] {
        Texture texture;
        texture.width = 1;
        texture.height = 1;
        texture.pixels = {255, 255, 255, 255};
        return texture;
    }();
    return white;
}

// The device tone maps whatever colour it is handed, because every other batch in
// the frame is linear light on its way into an sRGB attachment. An overlay colour
// is already the window's colour, so it is handed the value that the same curve
// turns back into it -- and the attachment's encode finishes the job the CPU path
// does by hand.
double inverseTone(double value) {
    const double v = std::max(0.0, std::min(1.0, value));
    // v = x(2.51x + 0.03) / (x(2.43x + 0.59) + 0.14) solved for x.
    const double a = 2.51 - 2.43 * v;
    const double b = 0.03 - 0.59 * v;
    const double c = -0.14 * v;
    if (std::abs(a) < 1e-9) return b > 1e-12 ? std::max(0.0, -c / b) : 0.0;
    const double root = std::sqrt(std::max(0.0, b * b - 4.0 * a * c));
    return std::max(0.0, (-b + root) / (2.0 * a));
}

// Quads that share a batch: they have to agree on everything a batch's push block
// carries, which is the image they sample, the colour and the alpha.
struct QuadKey {
    const Texture* texture = nullptr;
    float r = 0, g = 0, b = 0, a = 1;
    bool operator<(const QuadKey& other) const {
        if (texture != other.texture) return texture < other.texture;
        if (r != other.r) return r < other.r;
        if (g != other.g) return g < other.g;
        if (b != other.b) return b < other.b;
        return a < other.a;
    }
};

// Six vertices, no indices: an overlay quad is two triangles and the index array
// belongs to the scene.
void emitQuad(GpuBatch& batch, const UiQuad& source, int width, int height) {
    const float x0 = 2.0f * (source.x0 / static_cast<float>(width)) - 1.0f;
    const float x1 = 2.0f * (source.x1 / static_cast<float>(width)) - 1.0f;
    const float y0 = 1.0f - 2.0f * (source.y0 / static_cast<float>(height));
    const float y1 = 1.0f - 2.0f * (source.y1 / static_cast<float>(height));
    const float corners[6][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
    const float uvs[6][2] = {{source.u0, source.v0}, {source.u1, source.v0}, {source.u1, source.v1},
                             {source.u0, source.v0}, {source.u1, source.v1}, {source.u0, source.v1}};
    for (int corner = 0; corner < 6; ++corner) {
        GpuVertex vertex;
        // The vertex is the clip coordinate the quad wants: the batch's model
        // matrix is the projection undone, so the vertex stage hands it back
        // unchanged. Depth is off, a HUD being flat and in front.
        vertex.x = corners[corner][0];
        vertex.y = corners[corner][1];
        vertex.z = 0.0f;
        vertex.u = uvs[corner][0];
        vertex.v = uvs[corner][1];
        batch.vertices.push_back(vertex);
    }
}

} // namespace

void appendOverlay(GpuScene& scene, const NativeFrame& frame) {
    if (frame.uiQuads.empty()) return;
    const int width = std::max(1, frame.width);
    const int height = std::max(1, frame.height);
    std::map<QuadKey, std::size_t> batches;
    for (const UiQuad& source : frame.uiQuads) {
        QuadKey key;
        key.texture = source.texture != nullptr ? source.texture : &whiteTexel();
        key.r = source.r;
        key.g = source.g;
        key.b = source.b;
        key.a = source.alpha;
        auto found = batches.find(key);
        if (found == batches.end()) {
            GpuBatch batch;
            batch.overlay = true;
            batch.material.kind = Material::Kind::Textured;
            batch.material.texture = key.texture;
            batch.material.color = {inverseTone(key.r), inverseTone(key.g), inverseTone(key.b)};
            batch.material.opacity = key.a;
            batch.material.unlit = true;
            found = batches.emplace(key, scene.batches.size()).first;
            scene.batches.push_back(std::move(batch));
        }
        emitQuad(scene.batches[found->second], source, width, height);
    }
}

} // namespace gtc::native
