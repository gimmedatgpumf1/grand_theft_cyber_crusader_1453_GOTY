// Two questions about friction arrows, asked over play.
//
// 1. Is every friction arrow *along* the face it acts on? A friction that is not
//    along the surface is not a friction.
// 2. When the two ends of one contact are held in opposite directions, is the body
//    turning? A plate spinning on the floor really is held one way at one end and the
//    other way at the other; a plate sliding flat is not.
#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
namespace { constexpr double kDt = 1.0 / 20.0; }
int main() {
    int frictionSeen = 0, offFace = 0, oppositePairs = 0, oppositeWhileTurning = 0, floating = 0;
    double worstFloat = 0.0;
    double worstOffFace = 0.0;
    for (int trial = 0; trial < 60; ++trial) {
        PlaygroundRoom r;
        r.reset();
        for (int k = 0; k < 5; ++k) r.advance(kDt);
        auto& c = r.bodies_[trial % 2 == 0 ? 0 : 1];
        const std::size_t which = trial % 2 == 0 ? 0 : 1;
        c.centre = {-1.5495 + 0.7071 * 0.352 + 0.1 * double(trial % 4),
                    1.0495 + 0.7071 * 0.352 + 0.1 * double(trial % 3), 0};
        c.angle = -2.3561945;
        PlaygroundRoom::placeCorners(c);
        c.previous = c.position; c.previousCentre = c.centre; c.previousAngle = c.angle;
        c.velocity = {0.4 * double(trial % 5) - 0.8, -0.3, 0};
        c.spin = 0.3 * double(trial % 7) - 0.9;
        c.frozen = false;
        for (int k = 0; k < 80; ++k) {
            r.advance(kDt);
            const auto& b = r.bodies_[which];
            // Every friction arrow on this body: along the nearest face of what it is
            // pressed against, or square to it -- nothing else is a friction.
            double left = 0.0, right = 0.0;
            int ends = 0;
            for (std::size_t f = 0; f < r.forceCount_; ++f) {
                const auto& s = r.forces_[f];
                if (s.owner != which || s.kind != PlaygroundRoom::ForceKind::Friction || s.couple) continue;
                if (length(s.force) < 1e-6) continue;
                ++frictionSeen;
                // Every surface within a millimetre of the place it acts. The arrow is
                // right if it lies along *one* of them: a point where two surfaces
                // meet has two frictions, and each belongs to its own face.
                std::vector<Vec3> normals;
                if (s.at.y <= 3e-3) normals.push_back(Vec3{0.0, 1.0, 0.0});
                for (std::size_t other = 0; other < PlaygroundRoom::kBodyCount; ++other) {
                    if (other == which || r.bodies_[other].count == 0) continue;
                    const auto g = ball_shape::outlineGap(s.at, r.bodies_[other].position.data(),
                                                          r.bodies_[other].position.data(),
                                                          r.bodies_[other].count, 1.0);
                    if (std::abs(g.gap) > 3e-3) continue;
                    normals.push_back(g.normal);
                }
                if (normals.empty()) {
                    // How far is it from every surface? A contact arrow belongs on the
                    // surface it acts on; one drawn in mid-air acts nowhere.
                    double nearest = 1e30;
                    if (s.at.y > 0.0) nearest = s.at.y;
                    for (std::size_t other = 0; other < PlaygroundRoom::kBodyCount; ++other) {
                        if (other == which || r.bodies_[other].count == 0) continue;
                        const auto g = ball_shape::outlineGap(s.at, r.bodies_[other].position.data(),
                                                              r.bodies_[other].position.data(),
                                                              r.bodies_[other].count, 1.0);
                        nearest = std::min(nearest, std::abs(g.gap));
                    }
                    if (nearest > worstFloat) {
                        worstFloat = nearest;
                        std::printf("FLOATING friction (%+.2f,%+.2f) at (%+.4f,%+.4f): %.4f m from any surface\n",
                                    s.force.x, s.force.y, s.at.x, s.at.y, nearest);
                    }
                    ++floating;
                    continue;
                }
                double across = 1e30;
                Vec3 normal = normals[0];
                for (const Vec3& n : normals)
                    if (std::abs(s.force.x * n.x + s.force.y * n.y) / length(s.force) < across) {
                        across = std::abs(s.force.x * n.x + s.force.y * n.y) / length(s.force);
                        normal = n;
                    }
                if (across > 1e-3) {
                    ++offFace;
                    if (across > worstOffFace) {
                        worstOffFace = across;
                        std::printf("OFF-FACE: %s friction (%+.3f,%+.3f) at (%+.4f,%+.4f) | normal (%+.3f,%+.3f)"
                                    " | on floor? %d\n",
                                    which == 0 ? "cube" : "triangle", s.force.x, s.force.y, s.at.x, s.at.y,
                                    normal.x, normal.y, int(s.at.y <= 2e-3));
                        std::printf("      its corners:");
                        for (std::size_t q = 0; q < b.count; ++q)
                            std::printf(" (%+.4f,%+.4f)", b.position[q].x, b.position[q].y);
                        std::printf("  centre (%+.4f,%+.4f) angle %+.4f\n", b.centre.x, b.centre.y, b.angle);
                        std::printf("      all friction on it this tick:");
                        for (std::size_t f2 = 0; f2 < r.forceCount_; ++f2) {
                            const auto& s2 = r.forces_[f2];
                            if (s2.owner != which || s2.kind != PlaygroundRoom::ForceKind::Friction) continue;
                            std::printf(" (%+.2f,%+.2f)@(%+.4f,%+.4f)", s2.force.x, s2.force.y, s2.at.x, s2.at.y);
                        }
                        std::printf("\n");
                    }
                }
                if (s.at.y <= 2e-3) {
                    ++ends;
                    if (s.at.x < b.centre.x) left = s.force.x; else right = s.force.x;
                }
            }
            if (ends >= 2 && left * right < 0.0 && std::min(std::abs(left), std::abs(right)) > 0.05) {
                ++oppositePairs;
                if (std::abs(b.spin) > 1e-3) ++oppositeWhileTurning;
            }
        }
    }
    std::printf("friction arrows seen: %d\n", frictionSeen);
    std::printf("  not lying along the face they act on: %d (worst %.2e of their length)\n", offFace, worstOffFace);
    std::printf("  two ends of one floor contact held opposite ways: %d, of which the body was turning: %d\n",
                oppositePairs, oppositeWhileTurning);
    std::printf("  drawn in mid-air, away from every surface: %d of them (worst %.4f m off)\n", floating,
                worstFloat);
    return 0;
}
