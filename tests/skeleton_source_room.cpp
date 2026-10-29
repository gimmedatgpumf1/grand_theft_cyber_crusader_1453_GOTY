// Check the game's actual test-room scene: female stays in source pose while
// the male remains animated. This exercises the GPU scene path without relying
// on a screenshot or claiming the fake callback renders anything.
#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace gtc::native;
static std::vector<std::array<double, 16>> female, male;
static bool indicesValid;
static std::size_t batchCount;
static int capturedWidth, capturedHeight;
static double capturedFocal;
static std::array<double, 16> capturedView;
static std::uint64_t capturedRevision;
static std::uint32_t maleSkullId;
static std::size_t skullIndexCount;
static bool capturedCovered;
static bool lineObjectsOnly;
static std::size_t maleLineIndices, femaleLineIndices;
static bool capture(const GpuScene& scene, NativeFrame&) {
    female.clear(); male.clear();
    indicesValid = true;
    skullIndexCount = 0;
    lineObjectsOnly = true;
    maleLineIndices = femaleLineIndices = 0;
    batchCount = scene.batches.size();
    capturedWidth = scene.width; capturedHeight = scene.height; capturedFocal = scene.focal;
    capturedView = scene.viewMatrix;
    capturedRevision = scene.revision;
    capturedCovered = scene.covered;
    for (const auto& batch : scene.batches) {
        if (batch.objectId == maleSkullId) skullIndexCount = batch.indices.size();
        for (std::uint32_t index : batch.indices) if (index >= batch.vertices.size()) indicesValid = false;
        if (batch.objectId >= 9001 && batch.objectId < 10000) {
            male.push_back(batch.modelMatrix); maleLineIndices += batch.indices.size();
            if (batch.objectId != 9001) lineObjectsOnly = false;
        } else if (batch.objectId >= 8001 && batch.objectId < 9000) {
            female.push_back(batch.modelMatrix); femaleLineIndices += batch.indices.size();
            if (batch.objectId != 8001) lineObjectsOnly = false;
        }
    }
    return true;
}

static bool checkIndexedRecycle() {
    Camera camera;
    camera.width = 640; camera.height = 480; camera.worldSpaceGpu = true;
    Material material;
    const std::vector<Vec3> points{{0, 0, 1}, {1, 0, 1}, {0, 1, 1}};
    const std::vector<std::array<int, 3>> faces{{0, 1, 2}};
    Rasterizer first(640, 480, {}, camera.focal(), true);
    first.setCamera(camera); first.setObject(42, {}, 0);
    first.mesh(points, faces, material);
    GpuScene recycled = first.takeGpuScene();
    Rasterizer second(800, 600, {}, camera.focal(), true, std::move(recycled));
    second.setCamera(camera); second.setObject(42, {}, 0);
    second.mesh(points, faces, material);
    GpuScene result = second.takeGpuScene();
    for (const auto& batch : result.batches) {
        if (batch.objectId != 42) continue;
        return batch.vertices.size() == 3 && batch.indices == std::vector<std::uint32_t>{0, 1, 2};
    }
    return false;
}

int main() {
    SkeletonModel maleModel;
    std::string error;
    if (!maleModel.load("tools/skeleton-source/skeleton-male.obj", error)) {
        std::printf("male load failed: %s\n", error.c_str()); return 1;
    }
    const int skull = maleModel.bone("skull");
    if (skull < 0) { std::puts("male skull missing"); return 1; }
    maleSkullId = 9001u + static_cast<std::uint32_t>(skull);
    const std::size_t expectedSkullIndices = maleModel.bones()[skull].triangles.size() * 3;
    SkeletonRoom room;
    if (!room.load(".", error)) { std::printf("room load failed: %s\n", error.c_str()); return 1; }
    setGpuRenderer(&capture);
    ViewState view;
    view.gpu = true;
    view.width = 640; view.height = 480;
    NativeFrame frame;
    room.render(frame, view, 0.75);
    const auto firstFemale = female, firstMale = male;
    room.render(frame, view, 1.75);
    const double c = std::cos(3.14159265), s = std::sin(3.14159265);
    const std::array<double, 16> placement{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0.52, 0, 0, 1};
    bool unposed = true;
    for (const auto& matrix : firstFemale) {
        for (std::size_t i = 0; i < matrix.size(); ++i) {
            if (std::abs(matrix[i] - placement[i]) > 1e-12) unposed = false;
        }
    }
    bool pass = checkIndexedRecycle() && firstFemale.size() == 27 && firstMale.size() == 27 && unposed &&
                firstFemale == female && firstMale != male && indicesValid &&
                expectedSkullIndices > 0 && skullIndexCount == expectedSkullIndices;
    const auto oldFemale = female, oldMale = male;
    const auto oldView = capturedView;
    const auto oldRevision = capturedRevision;
    const std::size_t oldBatches = batchCount;
    // Multiple resizes in the same pose tick: keep the skeletons' matrices and
    // world geometry, but update the actual camera and render dimensions.
    for (const auto size : {std::pair<int, int>{800, 600}, {320, 240}, {1920, 1080}, {640, 480}}) {
        view.width = size.first; view.height = size.second; view.yaw = 0.35f;
        room.render(frame, view, 1.75);
        pass = pass && indicesValid && skullIndexCount == expectedSkullIndices && female == oldFemale && male == oldMale &&
               capturedWidth == size.first && capturedHeight == size.second && capturedView != oldView &&
               std::abs(capturedFocal - size.second / (2.0 * std::tan(50.0 * 3.141592653589793 / 360.0))) < 1e-9 &&
               capturedRevision == oldRevision && batchCount == oldBatches;
    }
    // A resize on the same call as an animation tick must also stay valid.
    view.width = 1000; view.height = 700;
    room.render(frame, view, 1.90);
    pass = pass && indicesValid && skullIndexCount == expectedSkullIndices && female == oldFemale && male != oldMale &&
           capturedWidth == 1000 && capturedHeight == 700 && batchCount == oldBatches;
    // The interactive camera updates the scene even within a single pose tick,
    // without rebuilding geometry or changing either skeleton's pose.
    RoomFreecam freecam;
    view.width = 640; view.height = 480;
    room.render(frame, view, 1.90, &freecam);
    const auto freecamView = capturedView;
    const auto femaleBeforeMove = female, maleBeforeMove = male;
    WindowEvent forwardKey{WindowEvent::Type::KeyDown, 'w'};
    freecam.handle(forwardKey, true);
    freecam.move(0.5);
    WindowEvent look{WindowEvent::Type::MouseMove}; look.dx = 90; look.dy = -60;
    freecam.handle(look, true);
    room.render(frame, view, 1.90, &freecam);
    pass = pass && capturedView != freecamView && capturedView == freecam.camera(640, 480, true).viewMatrix() &&
           female == femaleBeforeMove && male == maleBeforeMove && indicesValid && !capturedCovered &&
           capturedRevision == oldRevision && batchCount == oldBatches && skullIndexCount == expectedSkullIndices;
    // V replaces both mesh bodies with static, cached line geometry. Camera
    // changes and time changes do not rebuild or pose those lines.
    room.toggleLineView();
    room.render(frame, view, 1.90, &freecam);
    const auto lineRevision = capturedRevision;
    const auto lineView = capturedView;
    const auto lineBatchCount = batchCount;
    pass = pass && room.lineView() && indicesValid && lineRevision != oldRevision &&
           female.size() == 3 && male.size() == 3 && lineBatchCount < oldBatches && lineObjectsOnly &&
           maleLineIndices <= 751 * 36 && femaleLineIndices <= 751 * 36 &&
           maleLineIndices >= 542 * 36 && femaleLineIndices >= 542 * 36;
    WindowEvent moreLook{WindowEvent::Type::MouseMove}; moreLook.dx = 25;
    freecam.handle(moreLook, true);
    room.render(frame, view, 4.90, &freecam);
    pass = pass && capturedRevision == lineRevision && capturedView != lineView &&
           batchCount == lineBatchCount && indicesValid;
    room.toggleLineView();
    room.render(frame, view, 1.90, &freecam);
    pass = pass && !room.lineView() && capturedRevision == oldRevision &&
           female.size() == 27 && male.size() == 27 && skullIndexCount == expectedSkullIndices;
    setGpuRenderer(nullptr);
    std::printf("test room source pose and resize: %s (female %zu groups, male %zu groups)\n",
                pass ? "PASS" : "FAIL", female.size(), male.size());
    return pass ? 0 : 1;
}
