#pragma once

// Bind mesh vertices to the existing source-structure matrices. This is a
// deformation primitive, not an anatomical surface generator: it does not
// invent flesh, change the bones, or create a second skeleton.
#include "client/raster.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace gtc::native {
struct SkinInfluence {
    std::size_t bone=0;
    double weight=0;
};
struct SkinVertex {
    Vec3 position{}; // position in the skeleton's bind-pose space
    std::array<SkinInfluence,4> influences{};
    std::size_t count=0;
};

class SkeletonSkinBinding {
public:
    using Matrix=std::array<double,16>;
    // Matrices must be rigid, as are SkeletonModel::sourceMatrices. Positions
    // are transformed into each actual bone's local bind frame once; later
    // reference-pose or ragdoll matrices move the same points with those bones.
    bool bind(const std::vector<SkinVertex>& vertices,const std::vector<Matrix>& pose,std::string& error) {
        std::vector<BoundVertex> result;
        result.reserve(vertices.size());
        for (const auto& vertex:vertices) {
            if (!finite(vertex.position) || vertex.count==0 || vertex.count>vertex.influences.size()) {
                error="Invalid skin vertex"; return false;
            }
            BoundVertex bound; bound.count=vertex.count; double total=0;
            for (std::size_t i=0;i<vertex.count;++i) {
                const auto influence=vertex.influences[i];
                if (influence.bone>=pose.size() || !std::isfinite(influence.weight) || influence.weight<=0 ||
                    !rigid(pose[influence.bone])) { error="Invalid skin influence or bind matrix"; return false; }
                for (std::size_t j=0;j<i;++j) if (bound.influences[j].bone==influence.bone) {
                    error="Duplicate skin influence"; return false;
                }
                const Matrix& m=pose[influence.bone]; const Vec3 q=vertex.position-Vec3{m[12],m[13],m[14]};
                bound.influences[i]={influence.bone,influence.weight,
                    {m[0]*q.x+m[1]*q.y+m[2]*q.z, m[4]*q.x+m[5]*q.y+m[6]*q.z,
                     m[8]*q.x+m[9]*q.y+m[10]*q.z}};
                total+=influence.weight;
            }
            if (std::abs(total-1.0)>1e-8) { error="Skin weights must sum to one"; return false; }
            result.push_back(bound);
        }
        vertices_=std::move(result); error.clear(); return true;
    }
    bool deform(const std::vector<Matrix>& pose,std::vector<Vec3>& points,std::string& error) const {
        // Reject bad input before changing the caller's output.
        for (const auto& vertex:vertices_) for (std::size_t i=0;i<vertex.count;++i) {
            const auto bone=vertex.influences[i].bone;
            if (bone>=pose.size() || !rigid(pose[bone])) { error="Invalid skin pose matrix"; return false; }
        }
        std::vector<Vec3> result; result.reserve(vertices_.size());
        for (const auto& vertex:vertices_) {
            Vec3 p;
            for (std::size_t i=0;i<vertex.count;++i) {
                const auto& influence=vertex.influences[i]; const auto& m=pose[influence.bone];
                const Vec3 q=influence.local;
                p+=Vec3{m[0]*q.x+m[4]*q.y+m[8]*q.z+m[12],
                        m[1]*q.x+m[5]*q.y+m[9]*q.z+m[13],
                        m[2]*q.x+m[6]*q.y+m[10]*q.z+m[14]}*influence.weight;
            }
            result.push_back(p);
        }
        points=std::move(result); error.clear(); return true;
    }
    std::size_t size() const { return vertices_.size(); }
private:
    struct BoundInfluence { std::size_t bone=0; double weight=0; Vec3 local{}; };
    struct BoundVertex { std::array<BoundInfluence,4> influences{}; std::size_t count=0; };
    std::vector<BoundVertex> vertices_;
    static bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
    static bool rigid(const Matrix& m) {
        for (double x:m) if (!std::isfinite(x)) return false;
        if (std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-8) return false;
        const Vec3 a{m[0],m[1],m[2]},b{m[4],m[5],m[6]},c{m[8],m[9],m[10]};
        return std::abs(dot(a,a)-1)<1e-6 && std::abs(dot(b,b)-1)<1e-6 && std::abs(dot(c,c)-1)<1e-6 &&
            std::abs(dot(a,b))<1e-6 && std::abs(dot(b,c))<1e-6 && std::abs(dot(c,a))<1e-6 &&
            dot(cross(a,b),c)>0.999999;
    }
};
} // namespace gtc::native
