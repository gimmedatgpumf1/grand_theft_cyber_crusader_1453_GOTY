#pragma once

#include "client/raster.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace gtc::native {

// An inspection overlay generated offline from the registered source meshes.
// Links follow an explicit anatomical table; endpoints are estimates, not
// measured joint landmarks. Tube triangles keep this on the existing GPU path.
class SkeletonLines {
public:
    struct Segment { Vec3 a, b; char kind; };

    bool load(const std::string& path, std::string& error) {
        std::ifstream input(path);
        if (!input) { error = "Cannot open skeleton lines " + path; return false; }
        segments_.clear();
        dots_.clear();
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            char kind; std::string first, second; Segment s;
            row >> kind >> first;
            if (kind == 'J' || kind == 'C') row >> second;
            row >> s.a.x >> s.a.y >> s.a.z;
            if (kind != 'D') row >> s.b.x >> s.b.y >> s.b.z;
            s.kind = kind;
            if (!row || (kind != 'D' && kind != 'B' && kind != 'J' && kind != 'C') ||
                !std::isfinite(s.a.x) || !std::isfinite(s.a.y) || !std::isfinite(s.a.z) ||
                !std::isfinite(s.b.x) || !std::isfinite(s.b.y) || !std::isfinite(s.b.z)) {
                error = "Invalid skeleton line in " + path; return false;
            }
            if (kind == 'D') dots_.push_back(s.a);
            else segments_.push_back(s);
        }
        if (segments_.empty()) { error = "No skeleton lines in " + path; return false; }
        return true;
    }

    void draw(Rasterizer& out, Vec3 origin, double yaw, std::uint32_t id) const {
        constexpr int sides = 6;
        constexpr double pi = 3.141592653589793;
        const double c = std::cos(yaw), s = std::sin(yaw);
        const auto place = [&](Vec3 p) { return Vec3{origin.x + c*p.x + s*p.z,
                                                     origin.y + p.y,
                                                     origin.z - s*p.x + c*p.z}; };
        out.setObject(id, {}, 0);
        Material materials[3];
        materials[0].color = colorHex("#eee6c7");
        materials[1].color = colorHex("#5cd6d0");
        materials[2].color = colorHex("#f6aa62");
        for (Material& material : materials) material.unlit = true;
        const char kinds[] = {'B', 'J', 'C'};
        for (int type = 0; type < 3; ++type) {
            const char kind = kinds[type];
            std::vector<Vec3> points;
            std::vector<std::array<int, 3>> faces;
            for (const auto& segment : segments_) {
                if (segment.kind != kind) continue;
                const Vec3 a = place(segment.a), b = place(segment.b);
                const Vec3 direction = normalized(b - a);
                if (length(direction) < 1e-9) continue;
                const Vec3 guide = std::abs(direction.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                const Vec3 u = normalized(cross(direction, guide)), v = cross(direction, u);
                const double radius = kind == 'B' ? 0.003 : 0.002;
                const int start = static_cast<int>(points.size());
                for (int end = 0; end < 2; ++end) for (int i = 0; i < sides; ++i) {
                    const double angle = 2 * pi * i / sides;
                    points.push_back((end ? b : a) + u * (std::cos(angle)*radius) + v * (std::sin(angle)*radius));
                }
                for (int i = 0; i < sides; ++i) {
                    const int next = (i + 1) % sides;
                    faces.push_back({start+i, start+next, start+sides+i});
                    faces.push_back({start+next, start+sides+next, start+sides+i});
                }
            }
            if (kind == 'B') {
                // A low-poly sphere reads as a dot from any camera angle.
                // It shares the bone material and the cached GPU batch.
                constexpr double phi = 1.618033988749895;
                const Vec3 vertices[] = {{-1,phi,0},{1,phi,0},{-1,-phi,0},{1,-phi,0},
                    {0,-1,phi},{0,1,phi},{0,-1,-phi},{0,1,-phi},
                    {phi,0,-1},{phi,0,1},{-phi,0,-1},{-phi,0,1}};
                const int triangles[][3] = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
                    {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
                    {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
                    {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
                for (Vec3 dot : dots_) {
                    const int start = static_cast<int>(points.size());
                    const Vec3 center = place(dot);
                    for (Vec3 vertex : vertices) points.push_back(center + normalized(vertex) * 0.007);
                    for (const auto& triangle : triangles)
                        faces.push_back({start+triangle[0], start+triangle[1], start+triangle[2]});
                }
            }
            out.mesh(points, faces, materials[type]);
        }
    }

private:
    std::vector<Segment> segments_;
    std::vector<Vec3> dots_;
};

} // namespace gtc::native
