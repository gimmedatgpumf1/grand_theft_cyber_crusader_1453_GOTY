// How batches become draws.
//
// Two jobs live here. The first is turning a batch's own indices into offsets in
// the one shared vertex stream. The second is the opposite of it: a whole
// skeleton arrives as fifty-six rigid batches that share a material, and fifty-
// six draw calls cost 0.7 ms of a 3 ms frame. So consecutive batches that differ
// only by their matrix are drawn once, with the matrix read out of an array.

#include "vk_internal.h"

#include <cstdio>
#include <cstring>

namespace gtc::native::detail {

// The scene's indices, shifted into the shared vertex stream. A wrong shift
// draws the wrong triangle from this batch's own vertices, which looks like a
// modelling fault rather than an offset one, so it is checked at upload.
bool Renderer::fillIndices(const GpuScene& scene, VkDeviceSize bytes) {
    const bool reallocate = !index_.buffer || bytes > index_.capacity;
    if (!reserveArray(index_, bytes)) return false;
    const std::size_t first = reallocate ? 0 : dirtyBatch_;
    std::uint32_t running = 0, start = 0;
    firstIndex_.assign(scene.batches.size(), 0);
    indexCount_.assign(scene.batches.size(), 0);
    for (std::size_t batch = 0; batch < scene.batches.size(); ++batch) {
        if (batch == first) start = running;
        firstIndex_[batch] = running;
        indexCount_[batch] = static_cast<std::uint32_t>(scene.batches[batch].indices.size());
        running += indexCount_[batch];
        if (batch < first) continue;
        for (std::uint32_t value : scene.batches[batch].indices) {
            if (value >= scene.batches[batch].vertices.size()) {
                std::fprintf(stderr, "[vk] index %u is past the end of batch %zu\n", value, batch);
                return false;
            }
        }
    }
    if (first >= scene.batches.size()) start = running;
    const VkDeviceSize offset = static_cast<VkDeviceSize>(start) * sizeof(std::uint32_t);
    if (offset == bytes) return true;
    const auto write = [&](char* cursor) {
        std::uint32_t at = 0;
        for (std::size_t batch = first; batch < scene.batches.size(); ++batch) {
            for (std::uint32_t value : scene.batches[batch].indices) {
                const std::uint32_t global = value + firstVertex_[batch];
                std::memcpy(cursor + static_cast<VkDeviceSize>(at++) * sizeof(global), &global, sizeof(global));
            }
        }
    };
    return uploadArray(index_, bytes - offset, write, offset);
}

namespace {

// Whether this batch can be folded into a run with its neighbours: it names
// vertices instead of repeating them, it samples no image (a texture set is a
// bind per draw), and it is actually shaded -- a flat batch is one colour and
// costs nothing to draw on its own.
bool coalescable(const GpuBatch& batch) {
    if (batch.indices.empty()) return false;
    if (texturedMaterial(batch.material)) return false;
    if (batch.material.constantLighting || batch.material.unlit) return false;
    return true;
}

// Two batches in one run are pushed with one colour, so they have to agree on
// the colour that push block carries.
bool sameColour(const Material& a, const Material& b) {
    return a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b &&
           a.opacity == b.opacity;
}

} // namespace

// The runs, and the slot each of their batches writes its matrix into. Called
// before the vertex upload, because the slot has to travel with the vertex.
void Renderer::findRuns(const GpuScene& scene) {
    planRuns(scene, runs_, slots_);
}

void planRuns(const GpuScene& scene, std::vector<BatchRun>& runs, std::vector<float>& slots) {
    runs.clear();
    slots.assign(scene.batches.size(), -1.0F);
    std::size_t slot = 0;
    std::size_t index = 0;
    while (index < scene.batches.size()) {
        if (!coalescable(scene.batches[index])) {
            ++index;
            continue;
        }
        const Material& material = scene.batches[index].material;
        std::size_t last = index;
        while (last + 1 < scene.batches.size() && coalescable(scene.batches[last + 1]) &&
               sameColour(material, scene.batches[last + 1].material)) {
            ++last;
        }
        // One batch on its own is a draw either way, and there is no point
        // spending a slot on it. A run that would run past the array is left
        // alone rather than split: a body of bones is one draw or no draw.
        if (last == index || slot + (last - index + 1) > kBatchSlots) {
            index = last + 1;
            continue;
        }
        for (std::size_t batch = index; batch <= last; ++batch) slots[batch] = static_cast<float>(slot++);
        BatchRun run;
        run.first = index;
        run.last = last;
        runs.push_back(run);
        index = last + 1;
    }
}

// This frame's matrices, at the slots their batches were given. A pose is fifty-
// six of these, so this is a few kilobytes of memory a frame rather than a
// command per bone.
void Renderer::fillBones(const GpuScene& scene) {
    if (boneMap_ == nullptr) return;
    for (const BatchRun& run : runs_) {
        for (std::size_t batch = run.first; batch <= run.last; ++batch) {
            const std::array<double, 16>& matrix = scene.batches[batch].modelMatrix;
            float* out = boneMap_ + static_cast<std::size_t>(slots_[batch]) * 16;
            for (int word = 0; word < 16; ++word) out[word] = static_cast<float>(matrix[word]);
        }
    }
}

} // namespace gtc::native::detail
