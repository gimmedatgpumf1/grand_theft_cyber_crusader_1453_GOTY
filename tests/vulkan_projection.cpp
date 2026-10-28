// The Vulkan backend cannot be run on a machine without a driver, but the part
// most likely to be wrong can still be proven: the clip-space matrix. This
// takes the same view matrix the renderer is handed and checks that the Vulkan
// projection lands every point on the same pixel the CPU rasterizer would use,
// with depth increasing into the scene across Vulkan's [0, 1] range.
//
// If this passes, a Vulkan frame is framed like a CPU frame. If it fails, the
// picture would be flipped, mirrored, or inside-out, and nothing else would
// look wrong until the first draw.

#include "vulkan/vk_internal.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct Point {
    double x, y, z;
};

// The exact mapping the software rasterizer uses, in `Rasterizer::triangle`:
// screen x grows right, screen y grows down, and the perspective divide is by
// the view-space depth.
void referencePixel(const std::array<double, 16>& view, double focal, int width, int height,
                    Point point, double& px, double& py, double& depth) {
    const double x = view[0] * point.x + view[4] * point.y + view[8] * point.z + view[12];
    const double y = view[1] * point.x + view[5] * point.y + view[9] * point.z + view[13];
    const double z = view[2] * point.x + view[6] * point.y + view[10] * point.z + view[14];
    px = width * 0.5 + focal * x / z;
    py = height * 0.5 - focal * y / z;
    depth = z;
}

} // namespace

int main() {
    int failures = 0;
    const int width = 1568;
    const int height = 929;
    const double focal = height * 0.5 / std::tan(0.5 * 50.0 * 3.14159265358979323846 / 180.0);

    // A camera behind and above the room's centre, looking at the skeletons --
    // roughly the pose the test room uses.
    const Point eye{0.4, 1.6, -3.5};
    const Point target{0.0, 0.95, 0.0};
    Point forward{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    forward = {forward.x / length, forward.y / length, forward.z / length};
    Point right{forward.z, 0.0, -forward.x};
    const double rightLength = std::sqrt(right.x * right.x + right.z * right.z);
    right = {right.x / rightLength, 0.0, right.z / rightLength};
    const Point up{
        right.y * forward.z - right.z * forward.y,
        right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x,
    };
    const std::array<double, 16> view = {
        right.x, up.x, forward.x, 0,
        right.y, up.y, forward.y, 0,
        right.z, up.z, forward.z, 0,
        -(right.x * eye.x + right.y * eye.y + right.z * eye.z),
        -(up.x * eye.x + up.y * eye.y + up.z * eye.z),
        -(forward.x * eye.x + forward.y * eye.y + forward.z * eye.z),
        1,
    };

    gtc::native::GpuScene scene;
    scene.width = width;
    scene.height = height;
    scene.focal = focal;
    scene.viewMatrix = view;

    const gtc::native::detail::Matrix4 projection = gtc::native::detail::projectionFor(scene);

    const std::vector<Point> points = {
        {0.0, 0.95, 0.0}, {0.0, 0.0, 0.0}, {0.52, 1.2, 0.1}, {-0.52, 0.4, -0.2},
        {1.4, 2.4, 0.7}, {-1.2, 1.8, 1.5}, {0.2, 2.9, -0.6}, {-0.3, 0.05, 0.4},
    };

    for (const Point& point : points) {
        const double clipX = projection.cell[0][0] * point.x + projection.cell[0][1] * point.y +
                             projection.cell[0][2] * point.z + projection.cell[0][3];
        const double clipY = projection.cell[1][0] * point.x + projection.cell[1][1] * point.y +
                             projection.cell[1][2] * point.z + projection.cell[1][3];
        const double clipZ = projection.cell[2][0] * point.x + projection.cell[2][1] * point.y +
                             projection.cell[2][2] * point.z + projection.cell[2][3];
        const double clipW = projection.cell[3][0] * point.x + projection.cell[3][1] * point.y +
                             projection.cell[3][2] * point.z + projection.cell[3][3];

        double px = 0, py = 0, depth = 0;
        referencePixel(view, focal, width, height, point, px, py, depth);
        if (depth <= 0.0) {
            std::printf("point (%.2f %.2f %.2f) is behind the camera; the test is wrong\n",
                        point.x, point.y, point.z);
            return 1;
        }

        const double ndcX = clipX / clipW;
        const double ndcY = clipY / clipW;
        const double ndcZ = clipZ / clipW;
        // Vulkan's viewport transform: y runs down the image, depth runs 0..1.
        const double screenX = (ndcX * 0.5 + 0.5) * width;
        const double screenY = (ndcY * 0.5 + 0.5) * height;

        if (std::fabs(screenX - px) > 0.01 || std::fabs(screenY - py) > 0.01) {
            std::printf("mismatch at (%.2f %.2f %.2f): vulkan %.3f,%.3f cpu %.3f,%.3f\n",
                        point.x, point.y, point.z, screenX, screenY, px, py);
            ++failures;
        }
        if (!(ndcZ >= 0.0 && ndcZ <= 1.0)) {
            std::printf("depth %.4f outside Vulkan's 0..1 at z=%.2f\n", ndcZ, depth);
            ++failures;
        }
        // A perspective projection maps distance to depth by a known curve; if
        // this one matches, nearer geometry is nearer in the depth buffer too,
        // which is the property the depth test actually needs.
        const double expectedDepth = 70.0 / (70.0 - 0.035) * (1.0 - 0.035 / depth);
        if (std::fabs(ndcZ - expectedDepth) > 0.001) {
            std::printf("depth %.4f is not the perspective value %.4f at distance %.2f\n",
                        ndcZ, expectedDepth, depth);
            ++failures;
        }
    }

    // The near plane must map to 0 and the far plane to 1, or the depth buffer
    // would clip geometry the CPU path draws.
    for (const double distance : {0.035, 70.0}) {
        Point along{eye.x + forward.x * distance, eye.y + forward.y * distance, eye.z + forward.z * distance};
        const double clipZ = projection.cell[2][0] * along.x + projection.cell[2][1] * along.y +
                             projection.cell[2][2] * along.z + projection.cell[2][3];
        const double clipW = projection.cell[3][0] * along.x + projection.cell[3][1] * along.y +
                             projection.cell[3][2] * along.z + projection.cell[3][3];
        const double ndcZ = clipZ / clipW;
        const double expected = distance == 0.035 ? 0.0 : 1.0;
        if (std::fabs(ndcZ - expected) > 0.001) {
            std::printf("near/far plane at %.3f maps to %.4f, expected %.1f\n", distance, ndcZ, expected);
            ++failures;
        }
    }

    if (failures != 0) {
        std::printf("vulkan projection: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("vulkan projection: PASS (%zu points match the CPU rasterizer, depth 0..1)\n", points.size());
    return 0;
}
