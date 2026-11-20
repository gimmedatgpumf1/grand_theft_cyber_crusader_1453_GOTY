#include "client/benchmark_scene.h"

#include "client/threadpool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gtc::native::benchmark_detail {

// Builds the one-skull scene the benchmark draws: a skull alone, the camera on
// it, everything else absent so both backends see exactly the same triangles.
bool buildSkull(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
                std::string& error) {
    gtc::native::SkeletonModel model;
    if (!model.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return false;
    const int skull = model.bone("skull");
    if (skull < 0) {
        error = "no skull bone in the skeleton";
        return false;
    }
    model.poseIdle(0.75, 0.0);
    out.camera.width = std::max(1, view.width);
    out.camera.height = std::max(1, view.height);
    out.camera.fovDegrees = 50;
    // Eye height on the skull, a metre and a half away: the skull fills the
    // frame, so the measurement is rasterization and not scene traversal.
    out.camera.position = {0.0, 1.62, -1.35};
    out.camera.target = {0.0, 1.60, 0.0};
    gtc::native::Rasterizer rasterizer(out.camera.width, out.camera.height, gtc::native::colorHex("#0d1210"),
                                      out.camera.focal(), true, gtc::native::GpuScene{});
    rasterizer.setCamera(out.camera);
    model.draw(rasterizer, out.camera, {0, 0, 0}, 0.0, skull);
    out.scene = rasterizer.takeGpuScene();
    out.scene.width = out.camera.width;
    out.scene.height = out.camera.height;
    out.scene.focal = out.camera.focal();
    out.scene.viewMatrix = out.camera.viewMatrix();
    out.scene.revision = 1;
    for (gtc::native::GpuBatch& batch : out.scene.batches) batch.revision = 1;
    out.ready = true;
    return true;
}




// Two whole bodies in one scene, every bone in both, framed the way the test room
// frames them -- because that is the scene the frame rate has to survive, and one
// body on its own was flattering it. They are emitted into a single scene and a
// single rasterizer: male and female at once, not one after the other.
bool buildBody(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
               std::string& error) {
    out.camera.width = std::max(1, view.width);
    out.camera.height = std::max(1, view.height);
    out.camera.fovDegrees = 50;
    // Three and a half metres back at chest height, as the room stands: both
    // bodies in shot, both head to foot.
    out.camera.position = {0.0, 1.15, 3.5};
    out.camera.target = {0.0, 0.95, 0.0};
    if (!out.male.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return false;
    if (!out.female.load(root + "/tools/skeleton-source/skeleton-female.obj", error)) return false;
    out.male.poseIdle(0.75, 0.0);
    gtc::native::Rasterizer rasterizer(out.camera.width, out.camera.height, gtc::native::colorHex("#0d1210"),
                                      out.camera.focal(), true, gtc::native::GpuScene{});
    rasterizer.setCamera(out.camera);
    // Rest position, once: one batch per bone, numbered so the two bodies cannot
    // collide. The world renderer and the test room hold a character this way.
    out.male.buildRestGeometry(rasterizer, out.camera, static_cast<int>(kMaleId));
    out.female.buildRestGeometry(rasterizer, out.camera, static_cast<int>(kFemaleId));
    out.scene = rasterizer.takeGpuScene();
    out.boneBatches.clear();
    for (std::size_t index = 0; index < out.scene.batches.size(); ++index) {
        const std::uint32_t id = out.scene.batches[index].objectId;
        if (id == 0) continue;
        out.boneBatches.emplace_back(id, index);
    }
    out.rest = true;
    out.scene.width = out.camera.width;
    out.scene.height = out.camera.height;
    out.scene.focal = out.camera.focal();
    out.scene.viewMatrix = out.camera.viewMatrix();
    out.scene.revision = 1;
    for (gtc::native::GpuBatch& batch : out.scene.batches) batch.revision = 1;
    out.ready = true;
    return true;
}


// One whole body, every bone in it, framed the way a character in the street is:
// close enough to fill the frame and no closer. The skull is a fortieth of this
// and the two bodies are twice it, so this is the measurement in between --
// a single character, which is what the game draws.
bool buildWholeBody(const std::string& root, const gtc::native::ViewState& view, SkullScene& out,
                    std::string& error) {
    out.camera.width = std::max(1, view.width);
    out.camera.height = std::max(1, view.height);
    out.camera.fovDegrees = 50;
    out.camera.position = {0.0, 1.15, 2.2};
    out.camera.target = {0.0, 0.95, 0.0};
    if (!out.male.load(root + "/tools/skeleton-source/skeleton-male.obj", error)) return false;
    out.male.poseIdle(0.75, 0.0);
    gtc::native::Rasterizer rasterizer(out.camera.width, out.camera.height, gtc::native::colorHex("#0d1210"),
                                      out.camera.focal(), true, gtc::native::GpuScene{});
    rasterizer.setCamera(out.camera);
    out.male.buildRestGeometry(rasterizer, out.camera, static_cast<int>(kMaleId));
    out.scene = rasterizer.takeGpuScene();
    out.boneBatches.clear();
    for (std::size_t index = 0; index < out.scene.batches.size(); ++index) {
        const std::uint32_t id = out.scene.batches[index].objectId;
        if (id == 0) continue;
        out.boneBatches.emplace_back(id, index);
    }
    out.rest = true;
    out.scene.width = out.camera.width;
    out.scene.height = out.camera.height;
    out.scene.focal = out.camera.focal();
    out.scene.viewMatrix = out.camera.viewMatrix();
    out.scene.revision = 1;
    for (gtc::native::GpuBatch& batch : out.scene.batches) batch.revision = 1;
    out.ready = true;
    return true;
}

// Turning a batch about the vertical axis through a point: the body spins where
// it stands instead of orbiting the middle of the scene.
std::array<double, 16> turnAbout(double radians, double pivotX) {
    const double cosine = std::cos(radians), sine = std::sin(radians);
    return {cosine, 0, -sine, 0, 0, 1, 0, 0, sine, 0, cosine, 0, pivotX * (1.0 - cosine), 0, sine * pivotX, 1};
}

std::array<double, 16> multiply(const std::array<double, 16>& left, const std::array<double, 16>& right) {
    std::array<double, 16> out{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += left[k * 4 + row] * right[column * 4 + k];
            out[column * 4 + row] = sum;
        }
    }
    return out;
}

// Always from the base matrices, never by turning the last frame's: a hundred
// thousand frames of accumulated rounding is a slow drift of the body, and a
// benchmark that slowly walks its scene out of frame is measuring itself.
// One frame of the world's two characters: solve both chains, write one matrix
// per bone. No vertex data is touched, which is the whole reason the room holds
// rest geometry -- and it is what the game does per character per frame.
void posePair(SkullScene& bodies, double turn) {
    const std::vector<std::array<double, 16>> maleMatrices =
        bodies.male.boneMatrices({kMaleX, 0.0, 0.0}, kFaceCamera + turn);
    const std::vector<std::array<double, 16>> femaleMatrices =
        bodies.female.boneMatrices({kFemaleX, 0.0, 0.0}, kFaceCamera - turn);
    for (const std::pair<std::uint32_t, std::size_t>& entry : bodies.boneBatches) {
        const std::vector<std::array<double, 16>>* matrices = nullptr;
        std::size_t bone = 0;
        if (entry.first >= kMaleId && entry.first - kMaleId < maleMatrices.size()) {
            matrices = &maleMatrices;
            bone = entry.first - kMaleId;
        } else if (entry.first >= kFemaleId && entry.first - kFemaleId < femaleMatrices.size()) {
            matrices = &femaleMatrices;
            bone = entry.first - kFemaleId;
        }
        if (matrices == nullptr) continue;
        bodies.scene.batches[entry.second].modelMatrix = (*matrices)[bone];
    }
}

// The skull on its own, turning about the axis through the origin: it sits
// there, so the origin is the only pivot it has.
void turnScene(const GpuScene& base, GpuScene& turned, double radians) {
    const std::array<double, 16> turn = turnAbout(radians, 0.0);
    for (std::size_t index = 0; index < turned.batches.size() && index < base.batches.size(); ++index) {
        turned.batches[index].modelMatrix = multiply(turn, base.batches[index].modelMatrix);
    }
}

// The same scene with every triangle collapsed to a single point. The vertex
// stage still runs over every vertex and every draw is still submitted; the
// rasteriser then has nothing to fill. What is left is the cost of moving the
// geometry, which is the half of the frame the fragment shader hides.
void collapseTriangles(gtc::native::GpuScene& scene) {
    for (gtc::native::GpuBatch& batch : scene.batches) {
        for (gtc::native::GpuVertex& vertex : batch.vertices) {
            vertex.x = vertex.y = vertex.z = 0.0F;
        }
    }
}

}  // namespace gtc::native::benchmark_detail
