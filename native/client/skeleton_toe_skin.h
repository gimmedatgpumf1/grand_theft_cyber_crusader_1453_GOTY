#pragma once

// Construct a single connected exterior for one toe. Rings on either side of
// a joint belong to different bones; the triangles between them deform on the
// GPU. The buried prism caps and rear half of the tip are not emitted.
#include "client/raster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace gtc::native {
struct ToeSkinMesh {
    std::vector<Vec3> points;
    std::vector<std::array<int,3>> faces;
    std::vector<int> owners;

    int vertex(Vec3 p, int owner) {
        points.push_back(p); owners.push_back(owner);
        return static_cast<int>(points.size()-1);
    }
    void triangle(int a,int b,int c) { if (a!=b && b!=c && c!=a) faces.push_back({a,b,c}); }
};

inline ToeSkinMesh makeToeSkin(const std::vector<std::vector<Vec3>>& prisms,
                               const std::vector<int>& boneIds,
                               const std::vector<Vec3>& tip, int tipId) {
    ToeSkinMesh out;
    if (prisms.empty() || prisms.size()!=boneIds.size() || tip.size()!=12) return out;
    for (const auto& prism:prisms) if (prism.size()!=16) return {};
    auto ring=[&](const std::vector<Vec3>& prism, double t, int owner) {
        std::array<int,8> result{};
        for (int k=0;k<8;++k) result[k]=out.vertex(prism[k]*(1-t)+prism[k+8]*t,owner);
        return result;
    };
    auto join=[&](const std::array<int,8>& a,const std::array<int,8>& b) {
        for (int k=0;k<8;++k) {
            const int j=(k+1)%8;
            out.triangle(a[k],a[j],b[j]); out.triangle(a[k],b[j],b[k]);
        }
    };
    std::array<int,8> last{};
    for (std::size_t i=0;i<prisms.size();++i) {
        const auto rear=ring(prisms[i],i==0 ? 0.0 : 0.12,boneIds[i]);
        const auto front=ring(prisms[i],i+1==prisms.size() ? 0.88 : 0.85,boneIds[i]);
        if (i==0) for (int k=1;k<7;++k) out.triangle(rear[0],rear[k+1],rear[k]);
        else join(last,rear);
        join(rear,front);
        last=front;
    }
    // Clip the icosahedron at its centre, retaining its visible forward half.
    // Shared edge intersections ensure adjacent triangles use the same vertex.
    constexpr std::array<std::array<int,3>,20> ico={{{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},
        {3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}}};
    Vec3 rear{},plane{};
    for (int k=0;k<8;++k) { rear+=prisms.back()[k]; plane+=prisms.back()[k+8]; }
    rear=rear/8.0; plane=plane/8.0;
    const Vec3 axis=normalized(plane-rear);
    if (length(axis)<1e-8) return {};
    const Vec3 u=normalized(prisms.back()[8]-plane);
    const Vec3 v=normalized(cross(axis,u));
    std::array<double,12> distance{};
    std::array<int,12> originals{}; originals.fill(-1);
    for (int i=0;i<12;++i) distance[i]=dot(tip[i]-plane,axis);
    std::map<std::pair<int,int>,int> edges;
    std::vector<int> boundary;
    const double epsilon=1e-9;
    auto original=[&](int i) {
        if (originals[i]<0) originals[i]=out.vertex(tip[i],tipId);
        if (std::abs(distance[i])<epsilon) boundary.push_back(originals[i]);
        return originals[i];
    };
    auto crossing=[&](int a,int b) {
        if (std::abs(distance[a])<epsilon) return original(a);
        if (std::abs(distance[b])<epsilon) return original(b);
        const auto key=std::minmax(a,b);
        const std::pair<int,int> edge{key.first,key.second};
        auto found=edges.find(edge);
        if (found!=edges.end()) return found->second;
        const double t=distance[a]/(distance[a]-distance[b]);
        const int id=out.vertex(tip[a]+(tip[b]-tip[a])*t,tipId);
        edges.emplace(edge,id); boundary.push_back(id); return id;
    };
    for (const auto& face:ico) {
        std::vector<int> polygon;
        for (int k=0;k<3;++k) {
            const int a=face[k], b=face[(k+1)%3];
            const bool insideA=distance[a]>=-epsilon, insideB=distance[b]>=-epsilon;
            if (insideA) polygon.push_back(original(a));
            if (insideA!=insideB) polygon.push_back(crossing(a,b));
        }
        for (std::size_t k=1;k+1<polygon.size();++k) out.triangle(polygon[0],polygon[k],polygon[k+1]);
    }
    std::sort(boundary.begin(),boundary.end());
    boundary.erase(std::unique(boundary.begin(),boundary.end()),boundary.end());
    if (boundary.size()<3 || length(u)<1e-8 || length(v)<1e-8) return {};
    constexpr double twoPi=6.28318530717958647692;
    auto angle=[&](int id) {
        const Vec3 p=out.points[std::size_t(id)]-plane;
        double a=std::atan2(dot(p,v),dot(p,u));
        return a<0 ? a+twoPi : a;
    };
    std::sort(boundary.begin(),boundary.end(),[&](int a,int b) {return angle(a)<angle(b);});
    std::vector<int> back(last.begin(),last.end());
    std::sort(back.begin(),back.end(),[&](int a,int b) {return angle(a)<angle(b);});
    // Stitch two loops, advancing whichever next vertex has the smaller angle.
    std::size_t a=0,b=0;
    while (a<back.size() || b<boundary.size()) {
        const double nextA=a+1<back.size() ? angle(back[a+1]) : twoPi+angle(back[0]);
        const double nextB=b+1<boundary.size() ? angle(boundary[b+1]) : twoPi+angle(boundary[0]);
        const int av=back[a%back.size()], bv=boundary[b%boundary.size()];
        if (a<back.size() && (b==boundary.size() || nextA<=nextB)) {
            out.triangle(av,back[(a+1)%back.size()],bv); ++a;
        } else {
            out.triangle(av,boundary[(b+1)%boundary.size()],bv); ++b;
        }
    }
    return out;
}
} // namespace gtc::native
