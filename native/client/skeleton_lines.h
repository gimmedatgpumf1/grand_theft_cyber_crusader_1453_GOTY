#pragma once

#include "client/raster.h"
#include "client/skeleton_body.h"
#include "client/skeleton_leg_frame.h"
#include <map>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace gtc::native {

// An inspection overlay generated offline from the registered source meshes.
// Links follow an explicit anatomical table; endpoints are estimates, not
// measured joint landmarks. Tubes and solid palms/feet use the GPU scene path.
class SkeletonLines {
public:
    struct Segment { Vec3 a, b; char kind; std::string first, second; int ownerA = -1, ownerB = -1; };
    struct Solid {
        std::vector<Vec3> points;
        std::vector<std::array<int, 3>> faces;
        std::vector<std::array<int, 2>> edges;
        Vec3 normal;
        double thickness = 0;
        bool volume = false;
        std::string name;
        std::vector<int> owners;
    };

    std::optional<Vec3> sourceBoneDirection(const std::string& name) const {
        for (const auto& segment:segments_) if (segment.kind=='B' && segment.first==name) {
            const Vec3 direction=segment.b-segment.a;
            if (length(direction)>1e-8) return normalized(direction);
        }
        return std::nullopt;
    }

    bool load(const std::string& path, std::string& error) {
        std::ifstream input(path);
        if (!input) { error = "Cannot open skeleton lines " + path; return false; }
        segments_.clear();
        dots_.clear();
        joints_.clear();
        solids_.clear(); groups_.clear();
        std::string line;
        while (std::getline(input, line)) {
            if (line.rfind("# Group ",0)==0) {
                std::istringstream group(line.substr(8)); std::string name, member;
                group >> name; while (group >> member) groups_[name].push_back(member);
                continue;
            }
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            char kind; std::string first, second; Segment s;
            row >> kind >> first;
            if (kind == 'S') {
                Joint joint; joint.name = first;
                row >> joint.center.x >> joint.center.y >> joint.center.z >> joint.radius;
                if (!row || !std::isfinite(joint.center.x) || !std::isfinite(joint.center.y) ||
                    !std::isfinite(joint.center.z) || !std::isfinite(joint.radius) ||
                    joint.radius <= 0 || joint.radius > 0.15) {
                    error = "Invalid skeleton joint sphere in " + path; return false;
                }
                joints_.push_back(joint);
                continue;
            }
            if (kind == 'H' || kind == 'F') {
                Solid solid; solid.name = first;
                solid.volume = true;
                const int count = kind == 'F' ? 10 : 8;
                for (int i = 0; i < count; ++i) {
                    Vec3 point;
                    row >> point.x >> point.y >> point.z;
                    if (!row || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
                        error = "Invalid skeleton foot volume in " + path; return false;
                    }
                    solid.points.push_back(point);
                }
                const double volume = std::abs(dot(solid.points[4]-solid.points[0],
                    cross(solid.points[kind == 'F' ? 1 : 2]-solid.points[0],
                          solid.points[kind == 'F' ? 2 : 1]-solid.points[0])));
                if (volume < 1e-10) { error = "Degenerate skeleton foot in " + path; return false; }
                if (kind == 'F') {
                    const int triangles[][3] = {{4,5,1},{4,1,0}, {0,1,3},{0,3,2}, {4,6,7},{4,7,5},
                        {6,8,9},{6,9,7}, {8,2,3},{8,3,9},
                        {4,0,2},{4,2,8},{4,8,6}, {5,7,9},{5,9,3},{5,3,1}};
                    for (const auto& t : triangles) solid.faces.push_back({t[0],t[1],t[2]});
                } else {
                    const int quads[][4] = {{0,1,3,2}, {4,6,7,5}, {0,4,5,1},
                                            {2,3,7,6}, {0,2,6,4}, {1,5,7,3}};
                    for (const auto& q : quads) {
                        solid.faces.push_back({q[0],q[1],q[2]});
                        solid.faces.push_back({q[0],q[2],q[3]});
                    }
                }
                solids_.push_back(std::move(solid));
                continue;
            }
            if (kind == 'M') {
                Solid solid; solid.name = first;
                int vertices = 0, faces = 0, edges = 0;
                row >> solid.thickness >> vertices >> faces >> edges;
                if (!row || !std::isfinite(solid.thickness) || solid.thickness <= 0 ||
                    solid.thickness > 0.1 || vertices < 3 || vertices > 4096 ||
                    faces < 1 || faces > 8192 || edges < 3 || edges > 8192) {
                    error = "Invalid skeleton solid in " + path; return false;
                }
                for (int i = 0; i < vertices; ++i) {
                    Vec3 point;
                    row >> point.x >> point.y >> point.z;
                    if (!row || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
                        error = "Invalid skeleton solid vertex in " + path; return false;
                    }
                    solid.points.push_back(point);
                }
                double largest = 0;
                for (int i = 0; i < faces; ++i) {
                    std::array<int, 3> face;
                    row >> face[0] >> face[1] >> face[2];
                    if (!row || face[0] < 0 || face[0] >= vertices || face[1] < 0 || face[1] >= vertices ||
                        face[2] < 0 || face[2] >= vertices) {
                        error = "Invalid skeleton solid face in " + path; return false;
                    }
                    const Vec3 normal = cross(solid.points[face[1]] - solid.points[face[0]],
                                              solid.points[face[2]] - solid.points[face[0]]);
                    if (dot(normal, normal) > largest) { largest = dot(normal, normal); solid.normal = normal; }
                    solid.faces.push_back(face);
                }
                if (largest < 1e-18) { error = "Degenerate skeleton solid in " + path; return false; }
                solid.normal = normalized(solid.normal);
                for (int i = 0; i < edges; ++i) {
                    std::array<int, 2> edge;
                    row >> edge[0] >> edge[1];
                    if (!row || edge[0] < 0 || edge[0] >= vertices || edge[1] < 0 || edge[1] >= vertices) {
                        error = "Invalid skeleton solid edge in " + path; return false;
                    }
                    solid.edges.push_back(edge);
                }
                solids_.push_back(std::move(solid));
                continue;
            }
            if (kind == 'J' || kind == 'C') row >> second;
            row >> s.a.x >> s.a.y >> s.a.z;
            if (kind != 'D') row >> s.b.x >> s.b.y >> s.b.z;
            s.kind = kind; s.first = first; s.second = second.empty() ? first : second;
            if (!row || (kind != 'D' && kind != 'B' && kind != 'J' && kind != 'C') ||
                !std::isfinite(s.a.x) || !std::isfinite(s.a.y) || !std::isfinite(s.a.z) ||
                !std::isfinite(s.b.x) || !std::isfinite(s.b.y) || !std::isfinite(s.b.z)) {
                error = "Invalid skeleton line in " + path; return false;
            }
            if (kind == 'D') dots_.push_back(s);
            else segments_.push_back(s);
        }
        if (segments_.empty()) { error = "No skeleton lines in " + path; return false; }
        return true;
    }

    // Ownership candidates are explicit biological/group memberships. Proximity
    // only selects among members of an already declared group, never links bones.
    bool bind(const SkeletonModel& body, std::string& error) {
        auto owner = [&](const std::string& name, Vec3 p) -> int {
            std::vector<std::string> choices;
            const auto group = groups_.find(name);
            if (group == groups_.end()) choices.push_back(name);
            else choices = group->second;
            if (name == "RibCage") choices.push_back("Sternum");
            int best = -1; double bestDistance = 1e100;
            for (const auto& choice : choices) {
                int index = body.sourceStructure(choice); if (index < 0) continue;
                const auto& part = body.sourceStructures()[static_cast<std::size_t>(index)];
                const auto& points = body.bones()[part.group].points;
                for (std::size_t i = 0; i < part.pointCount; ++i) {
                    Vec3 d = points[part.firstPoint+i]-p; double distance = dot(d,d);
                    if (distance < bestDistance) { bestDistance=distance; best=index; }
                }
            }
            return best;
        };
        for (auto& segment : segments_) {
            segment.ownerA=owner(segment.first,segment.a); segment.ownerB=owner(segment.second,segment.b);
            if (segment.ownerA<0 || segment.ownerB<0) { error="Unknown line owner: "+segment.first+" / "+segment.second; return false; }
        }
        for (auto& dot : dots_) {
            dot.ownerA=owner(dot.first,dot.a);
            if (dot.ownerA<0) { error="Unknown dot owner: "+dot.first; return false; }
        }
        for (auto& solid : solids_) {
            if (solid.volume && solid.name.rfind("FootBase_",0)==0 && body.hasKneeAxes()) {
                const auto frame=sourceLegFrame(body,solid.name.back()=='L' ? 0 : 1);
                if (!frame) { error="Missing foot sole frame: "+solid.name; return false; }
                // The simplified four sole corners share the actual support
                // plane, while all six upper corners retain their source shape.
                for (int i : {0,1,4,5}) {
                    Vec3& p=solid.points[static_cast<std::size_t>(i)];
                    p+=frame->soleNormal*dot(frame->solePoint-p,frame->soleNormal);
                }
            }
            solid.owners.clear();
            for (Vec3 p : solid.points) {
                const int index=owner(solid.name,p);
                if (index<0) { error="Unknown solid owner: "+solid.name; return false; }
                solid.owners.push_back(index);
            }
        }
        for (auto& joint : joints_) {
            const std::string side=joint.name.substr(joint.name.size()-1);
            std::string name;
            if (joint.name.rfind("Shoulder_",0)==0) {
                name="Scapula_"+side;
                const int humerus=body.bone(side=="L" ? "humerus_l" : "humerus_r");
                if (humerus>=0) joint.center=body.bones()[static_cast<std::size_t>(humerus)].pivot;
            } else if (joint.name.rfind("Elbow_",0)==0) {
                name="Humerus_"+side;
                if (body.hasElbowAxes()) joint.center=body.elbowAxes()[side=="L" ? 0 : 1].origin;
            }
            else if (joint.name.rfind("Hip_",0)==0) name="Pelvis_"+side;
            // The knee ball includes the patella; follow it when the
            // source patella flexes, rather than leaving it at the femur.
            else if (joint.name.rfind("Knee_",0)==0) name="Patella_"+side;
            joint.owner=owner(name,joint.center);
            if (joint.owner<0) { error="Unknown joint sphere owner: "+joint.name; return false; }
        }
        return true;
    }

    void draw(Rasterizer& out, Vec3 origin, double yaw, std::uint32_t id,
              const std::vector<std::array<double,16>>& pose = {}) const {
        constexpr int sides = 6;
        constexpr double pi = 3.141592653589793;
        const double c = std::cos(yaw), s = std::sin(yaw);
        const auto place = [&](Vec3 p) { return Vec3{origin.x + c*p.x + s*p.z,
                                                     origin.y + p.y,
                                                     origin.z - s*p.x + c*p.z}; };
        const auto moved = [&](Vec3 p, int owner) {
            if (owner < 0 || static_cast<std::size_t>(owner) >= pose.size()) return p;
            const auto& m=pose[static_cast<std::size_t>(owner)];
            return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
                        m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                        m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
        };
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
                const Vec3 a = place(moved(segment.a,segment.ownerA)), b = place(moved(segment.b,segment.ownerB));
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
                for (const auto& dot : dots_) {
                    const int start = static_cast<int>(points.size());
                    const Vec3 center = place(moved(dot.a,dot.ownerA));
                    for (Vec3 vertex : vertices) points.push_back(center + normalized(vertex) * 0.007);
                    for (const auto& triangle : triangles)
                        faces.push_back({start+triangle[0], start+triangle[1], start+triangle[2]});
                }
            }
            out.mesh(points, faces, materials[type]);
        }
        // One subdivided icosahedron per fitted joint, emitted into a single
        // cached GPU batch. Nothing is generated per frame in the room view.
        {
            constexpr double phi = 1.618033988749895;
            const Vec3 vertices[] = {{-1,phi,0},{1,phi,0},{-1,-phi,0},{1,-phi,0},
                {0,-1,phi},{0,1,phi},{0,-1,-phi},{0,1,-phi},
                {phi,0,-1},{phi,0,1},{-phi,0,-1},{-phi,0,1}};
            const int triangles[][3] = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
                {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
                {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
                {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
            std::vector<Vec3> points;
            std::vector<std::array<int, 3>> faces;
            // Share the twelve corners and thirty edge midpoints per sphere.
            std::vector<Vec3> unit;
            for (Vec3 vertex : vertices) unit.push_back(normalized(vertex));
            std::vector<std::array<int, 2>> edges;
            std::vector<std::array<int, 3>> unitFaces;
            auto midpoint = [&](int a, int b) {
                if (a > b) std::swap(a,b);
                const std::array<int, 2> edge{a,b};
                for (std::size_t i = 0; i < edges.size(); ++i)
                    if (edges[i] == edge) return static_cast<int>(12+i);
                edges.push_back(edge);
                unit.push_back(normalized(unit[a]+unit[b]));
                return static_cast<int>(unit.size()-1);
            };
            for (const auto& triangle : triangles) {
                const int a = triangle[0], b = triangle[1], vertexC = triangle[2];
                const int ab = midpoint(a,b), bc = midpoint(b,vertexC), ca = midpoint(vertexC,a);
                unitFaces.push_back({a,ab,ca}); unitFaces.push_back({ab,b,bc});
                unitFaces.push_back({ca,bc,vertexC}); unitFaces.push_back({ab,bc,ca});
            }
            for (const Joint& joint : joints_) {
                const int start = static_cast<int>(points.size());
                const Vec3 center = place(moved(joint.center,joint.owner));
                for (Vec3 p : unit) points.push_back(center + p*joint.radius);
                for (const auto& f : unitFaces) faces.push_back({start+f[0], start+f[1], start+f[2]});
            }
            Material material;
            material.color = colorHex("#e9dfbd");
            out.mesh(points, faces, material);
        }
        // Palms, paired limbs, shoulder girdle and rib cage are thin surfaces;
        // feet are fitted closed volumes. All use the cached GPU solid batch.
        std::vector<Vec3> points;
        std::vector<std::array<int, 3>> faces;
        for (const auto& solid : solids_) {
            const int start = static_cast<int>(points.size());
            const int count = static_cast<int>(solid.points.size());
            if (solid.volume) {
                for (std::size_t i=0; i<solid.points.size(); ++i)
                    points.push_back(place(moved(solid.points[i],solid.owners.empty() ? -1 : solid.owners[i])));
                for (const auto& face : solid.faces)
                    faces.push_back({start+face[0], start+face[1], start+face[2]});
                continue;
            }
            // Each surface vertex follows its associated source structure.
            // Recompute the surface normal after articulation for thickness.
            std::vector<Vec3> transformed;
            for (std::size_t i=0; i<solid.points.size(); ++i)
                transformed.push_back(moved(solid.points[i],solid.owners.empty() ? -1 : solid.owners[i]));
            Vec3 normal=solid.normal; double largest=0;
            for (const auto& f : solid.faces) {
                const Vec3 n=cross(transformed[f[1]]-transformed[f[0]],transformed[f[2]]-transformed[f[0]]);
                if (dot(n,n)>largest) { largest=dot(n,n); normal=normalized(n); }
            }
            for (int layer = 0; layer < 2; ++layer)
                for (Vec3 point : transformed)
                    points.push_back(place(point + normal * ((layer ? 0.5 : -0.5) * solid.thickness)));
            for (const auto& face : solid.faces) {
                faces.push_back({start+face[0], start+face[2], start+face[1]});
                faces.push_back({start+count+face[0], start+count+face[1], start+count+face[2]});
            }
            for (const auto& edge : solid.edges) {
                faces.push_back({start+edge[0], start+edge[1], start+count+edge[0]});
                faces.push_back({start+edge[1], start+count+edge[1], start+count+edge[0]});
            }
        }
        Material solid;
        solid.color = colorHex("#eee6c7");
        out.mesh(points, faces, solid);
    }

private:
    struct Joint { Vec3 center; double radius = 0; std::string name; int owner = -1; };
    std::vector<Joint> joints_;
    std::vector<Segment> segments_;
    std::vector<Segment> dots_;
    std::map<std::string,std::vector<std::string>> groups_;
    std::vector<Solid> solids_;
};

} // namespace gtc::native
