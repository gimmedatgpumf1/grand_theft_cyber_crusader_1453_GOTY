// Check the game's actual test-room scene: female stays in source pose while
// the male remains animated. This exercises the GPU scene path without relying
// on a screenshot or claiming the fake callback renders anything.
#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
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
static bool maleDots, femaleDots, maleSolids, femaleSolids;
static bool checkDots(const GpuBatch& batch, const char* path, double x, double yaw) {
    constexpr std::size_t lineVertices = 210 * 12, dotVertices = 24 * 12;
    if (batch.vertices.size() != lineVertices + dotVertices || batch.indices.size() != 210 * 36 + 24 * 60)
        return false;
    std::ifstream file(path);
    std::string row, name;
    std::size_t dot = 0;
    while (std::getline(file, row)) {
        if (row.rfind("D ", 0) != 0) continue;
        std::istringstream input(row);
        char kind; Vec3 source; input >> kind >> name >> source.x >> source.y >> source.z;
        if (!input || dot >= 24) return false;
        const Vec3 expected{x + std::cos(yaw)*source.x + std::sin(yaw)*source.z,
                            source.y, -std::sin(yaw)*source.x + std::cos(yaw)*source.z};
        Vec3 center;
        for (std::size_t i = 0; i < 12; ++i) {
            const auto& v = batch.vertices[lineVertices + dot * 12 + i];
            const Vec3 vertex{v.x, v.y, v.z};
            if (std::abs(length(vertex - expected) - 0.007) > 1e-6) return false;
            center += vertex;
        }
        if (length(center / 12 - expected) > 1e-6) return false;
        ++dot;
    }
    return dot == 24;
}
static bool checkSolids(const GpuBatch& batch, const char* path, double x, double yaw) {
    std::ifstream file(path);
    std::string row;
    std::size_t solids = 0, vertexOffset = 0, indexOffset = 0;
    const double c = std::cos(yaw), s = std::sin(yaw);
    while (std::getline(file, row)) {
        const bool foot = row.rfind("F ", 0) == 0;
        if (!foot && row.rfind("M ", 0) != 0) continue;
        std::istringstream input(row);
        char kind; std::string name; double thickness = 0; int count = 10, faceCount = 0, edgeCount = 0;
        input >> kind >> name;
        if (!foot) input >> thickness >> count >> faceCount >> edgeCount;
        const std::size_t vertexCount = foot ? 10 : 2 * count;
        const std::size_t indexCount = foot ? 48 : (faceCount + edgeCount) * 6;
        if (!input || count < 3 || (!foot && (thickness <= 0 || thickness > 0.001)) ||
            vertexOffset + vertexCount > batch.vertices.size() || indexOffset + indexCount > batch.indices.size()) return false;
        for (int i = 0; i < count; ++i) {
            Vec3 p; input >> p.x >> p.y >> p.z;
            const Vec3 expected{x + c*p.x + s*p.z, p.y, -s*p.x + c*p.z};
            const auto& a = batch.vertices[vertexOffset+i];
            const Vec3 first{a.x,a.y,a.z};
            if (!input) return false;
            if (foot) {
                if (length(first - expected) > 1e-6) return false;
            } else {
                const auto& b = batch.vertices[vertexOffset+count+i];
                const Vec3 second{b.x,b.y,b.z};
                if (length((first + second) / 2 - expected) > 1e-6 ||
                    std::abs(length(second - first) - thickness) > 1e-6) return false;
            }
        }
        // Verify the real renderer emits the requested toe, heel, ankle and
        // instep faces, not just any closed surface over the ten points.
        if (foot) {
            const unsigned expected[] = {4,5,1, 4,1,0, 0,1,3, 0,3,2, 4,6,7, 4,7,5,
                6,8,9, 6,9,7, 8,2,3, 8,3,9, 4,0,2, 4,2,8, 4,8,6, 5,7,9, 5,9,3, 5,3,1};
            for (std::size_t i = 0; i < 48; ++i)
                if (batch.indices[indexOffset+i] != vertexOffset+expected[i]) return false;
        }
        // Foot volumes and both palm caps form closed meshes.
        std::map<std::pair<unsigned,unsigned>, int> edges;
        for (std::size_t i = indexOffset; i < indexOffset + indexCount; i += 3) {
            for (int corner = 0; corner < 3; ++corner) {
                auto a = batch.indices[i+corner], b = batch.indices[i+(corner+1)%3];
                if (a < vertexOffset || a >= vertexOffset + vertexCount || b < vertexOffset || b >= vertexOffset + vertexCount) return false;
                if (a > b) std::swap(a,b);
                ++edges[{a,b}];
            }
        }
        for (auto edge : edges) if (edge.second != 2) return false;
        vertexOffset += vertexCount; indexOffset += indexCount; ++solids;
    }
    return solids == 9 && vertexOffset == batch.vertices.size() && indexOffset == batch.indices.size();
}
static std::size_t maleLineIndices, femaleLineIndices, maleSolidIndices, femaleSolidIndices;
static bool capture(const GpuScene& scene, NativeFrame&) {
    female.clear(); male.clear();
    indicesValid = true;
    skullIndexCount = 0;
    lineObjectsOnly = true;
    maleDots = femaleDots = maleSolids = femaleSolids = false;
    maleLineIndices = femaleLineIndices = maleSolidIndices = femaleSolidIndices = 0;
    batchCount = scene.batches.size();
    capturedWidth = scene.width; capturedHeight = scene.height; capturedFocal = scene.focal;
    capturedView = scene.viewMatrix;
    capturedRevision = scene.revision;
    capturedCovered = scene.covered;
    for (const auto& batch : scene.batches) {
        if (batch.objectId == maleSkullId) skullIndexCount = batch.indices.size();
        if (batch.objectId == 9001 && checkDots(batch, "tools/skeleton-source/skeleton-male.lines", -0.52, 0)) maleDots = true;
        if (batch.objectId == 8001 && checkDots(batch, "tools/skeleton-source/skeleton-female.lines", 0.52, 3.14159265)) femaleDots = true;
        if (batch.objectId == 9001 && checkSolids(batch, "tools/skeleton-source/skeleton-male.lines", -0.52, 0)) { maleSolids = true; maleSolidIndices = batch.indices.size(); }
        if (batch.objectId == 8001 && checkSolids(batch, "tools/skeleton-source/skeleton-female.lines", 0.52, 3.14159265)) { femaleSolids = true; femaleSolidIndices = batch.indices.size(); }
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
           female.size() == 4 && male.size() == 4 && lineBatchCount < oldBatches && lineObjectsOnly &&
           maleDots && femaleDots && maleSolids && femaleSolids &&
           maleLineIndices <= 553 * 36 + 24 * 60 + maleSolidIndices && femaleLineIndices <= 553 * 36 + 24 * 60 + femaleSolidIndices &&
           maleLineIndices >= 210 * 36 + 24 * 60 + maleSolidIndices && femaleLineIndices >= 210 * 36 + 24 * 60 + femaleSolidIndices;
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
