#pragma once

// Comparison-only bilateral reconstruction. The two registered source meshes
// need not have the same triangulation. Align their centroids, average the
// reflected surface displacement, and regularise it over the template mesh.
// This is a geometric estimate, not a medically validated mean bone shape.
#include "client/skeleton_body.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace gtc::native::bilateral {
class PointTree {
    struct Node { int point=-1, left=-1, right=-1, axis=0; };
    const std::vector<Vec3>& points_;
    std::vector<int> order_;
    std::vector<Node> nodes_;
    static double coord(Vec3 p,int axis) { return axis==0?p.x:axis==1?p.y:p.z; }
    int build(int lo,int hi,int depth) {
        if (lo>=hi) return -1;
        int axis=depth%3, mid=lo+(hi-lo)/2;
        std::nth_element(order_.begin()+lo,order_.begin()+mid,order_.begin()+hi,
                         [&](int a,int b) { return coord(points_[a],axis)<coord(points_[b],axis); });
        int id=static_cast<int>(nodes_.size()); nodes_.push_back({order_[mid],-1,-1,axis});
        int left=build(lo,mid,depth+1), right=build(mid+1,hi,depth+1);
        nodes_[id].left=left; nodes_[id].right=right; return id;
    }
    void search(int id,Vec3 p,int& best,double& distance) const {
        if (id<0) return;
        const Node& node=nodes_[id]; const Vec3 q=points_[node.point];
        const double d=dot(p-q,p-q);
        if (d<distance) { distance=d; best=node.point; }
        const double delta=coord(p,node.axis)-coord(q,node.axis);
        const int near=delta<0?node.left:node.right, far=delta<0?node.right:node.left;
        search(near,p,best,distance);
        if (delta*delta<distance) search(far,p,best,distance);
    }
public:
    explicit PointTree(const std::vector<Vec3>& points):points_(points),order_(points.size()) {
        std::iota(order_.begin(),order_.end(),0); nodes_.reserve(points.size());
        build(0,static_cast<int>(points.size()),0);
    }
    Vec3 nearest(Vec3 p) const { int best=0; double d=1e100; search(0,p,best,d); return points_[best]; }
};

// Non-adjacent triangle crossings are checked for the thin scapula before it
// is accepted. The sweep avoids the quadratic all-pairs comparison; shared
// vertices are legitimate mesh neighbours, not self-intersections.
inline bool segmentTriangle(Vec3 p,Vec3 q,Vec3 a,Vec3 b,Vec3 c) {
    const Vec3 d=q-p,e=b-a,f=c-a,h=cross(d,f);
    const double det=dot(e,h); if (std::abs(det)<1e-15) return false;
    const double inv=1/det; const Vec3 v=p-a; const double u=dot(v,h)*inv;
    if (u<0 || u>1) return false;
    const Vec3 t=cross(v,e); const double w=dot(d,t)*inv;
    if (w<0 || u+w>1) return false;
    const double along=dot(f,t)*inv; return along>=0 && along<=1;
}
inline std::vector<std::pair<int,int>> selfIntersections(
        const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces) {
    struct Box { double minX,maxX,minY,maxY,minZ,maxZ; int face; };
    std::vector<Box> boxes; boxes.reserve(faces.size());
    for (std::size_t i=0;i<faces.size();++i) {
        const auto& f=faces[i]; const Vec3 a=points[f[0]],b=points[f[1]],c=points[f[2]];
        boxes.push_back({std::min({a.x,b.x,c.x}),std::max({a.x,b.x,c.x}),
                         std::min({a.y,b.y,c.y}),std::max({a.y,b.y,c.y}),
                         std::min({a.z,b.z,c.z}),std::max({a.z,b.z,c.z}),static_cast<int>(i)});
    }
    std::sort(boxes.begin(),boxes.end(),[](const Box& a,const Box& b){return a.minX<b.minX;});
    std::vector<std::pair<int,int>> result;
    for (std::size_t i=0;i<boxes.size();++i) for (std::size_t j=i+1;j<boxes.size() && boxes[j].minX<=boxes[i].maxX;++j) {
        const Box& a=boxes[i]; const Box& b=boxes[j];
        if (a.maxY<b.minY || b.maxY<a.minY || a.maxZ<b.minZ || b.maxZ<a.minZ) continue;
        const auto& fa=faces[a.face]; const auto& fb=faces[b.face]; bool shared=false;
        for (int x:fa) for (int y:fb) if (x==y) shared=true;
        if (shared) continue;
        bool crossing=false;
        for (int k=0;k<3 && !crossing;++k)
            crossing=segmentTriangle(points[fa[k]],points[fa[(k+1)%3]],points[fb[0]],points[fb[1]],points[fb[2]]) ||
                     segmentTriangle(points[fb[k]],points[fb[(k+1)%3]],points[fa[0]],points[fa[1]],points[fa[2]]);
        if (crossing) result.emplace_back(std::min(a.face,b.face),std::max(a.face,b.face));
    }
    std::sort(result.begin(),result.end()); return result;
}

inline bool average(std::vector<Vec3>& left,const std::vector<Vec3>& reflectedRight,
                    const std::vector<std::array<int,3>>& faces, bool checkSelf=false) {
    if (left.empty() || reflectedRight.empty()) return false;
    Vec3 a{},b{}; for (Vec3 p:left) a+=p; for (Vec3 p:reflectedRight) b+=p;
    a=a/static_cast<double>(left.size()); b=b/static_cast<double>(reflectedRight.size());
    const Vec3 offset=b-a;
    std::vector<Vec3> aligned; aligned.reserve(reflectedRight.size());
    for (Vec3 p:reflectedRight) aligned.push_back(p-offset);
    PointTree tree(aligned);
    std::vector<Vec3> delta(left.size()),next(left.size());
    std::vector<int> counts(left.size());
    for (std::size_t i=0;i<left.size();++i) delta[i]=(tree.nearest(left[i])-left[i])*0.5;
    for (const auto& f:faces) for (int j=0;j<3;++j) {
        ++counts[f[j]]; ++counts[f[(j+1)%3]];
    }
    for (int pass=0;pass<180;++pass) {
        std::fill(next.begin(),next.end(),Vec3{});
        for (const auto& f:faces) for (int j=0;j<3;++j) {
            const int u=f[j],v=f[(j+1)%3]; next[u]+=delta[v]; next[v]+=delta[u];
        }
        for (std::size_t i=0;i<left.size();++i)
            next[i]=counts[i]?(delta[i]+next[i]/static_cast<double>(counts[i]))*0.5:delta[i];
        delta.swap(next);
    }
    // Prevent a smoothed displacement from turning any nondegenerate face
    // inside out. Decrease only the local shape deformation, never the 50/50
    // centroid translation. A rare fallback retains the original template.
    const auto baseline=checkSelf?selfIntersections(left,faces):std::vector<std::pair<int,int>>{};
    bool valid=false;
    for (int pass=0;pass<32 && !valid;++pass) {
        valid=true; std::vector<bool> bad(left.size());
        for (const auto& f:faces) {
            const Vec3 normal=cross(left[f[1]]-left[f[0]],left[f[2]]-left[f[0]]);
            if (dot(normal,normal)<1e-24) continue;
            const Vec3 p0=left[f[0]]+delta[f[0]],p1=left[f[1]]+delta[f[1]],p2=left[f[2]]+delta[f[2]];
            if (dot(normal,cross(p1-p0,p2-p0))<=0) {
                valid=false; for (int index:f) bad[index]=true;
            }
        }
        if (checkSelf) {
            std::vector<Vec3> candidate; candidate.reserve(left.size());
            for (std::size_t i=0;i<left.size();++i) candidate.push_back(left[i]+delta[i]);
            for (const auto& pair:selfIntersections(candidate,faces)) {
                if (std::binary_search(baseline.begin(),baseline.end(),pair)) continue;
                valid=false;
                for (int index:faces[pair.first]) bad[index]=true;
                for (int index:faces[pair.second]) bad[index]=true;
            }
        }
        if (!valid) for (std::size_t i=0;i<delta.size();++i) if (bad[i]) delta[i]=delta[i]*0.5;
    }
    if (!valid) std::fill(delta.begin(),delta.end(),Vec3{});
    for (std::size_t i=0;i<left.size();++i) left[i]+=delta[i]+offset*0.5;
    return true;
}

// Clip an averaged axial mesh on its midline, then reflect the retained half.
// Shared original vertices and edge intersections stay welded at the seam.
inline bool mirrorAxial(std::vector<Vec3>& points,std::vector<std::array<int,3>>& faces,double axis) {
    if (points.empty() || faces.empty()) return false;
    std::vector<Vec3> result;
    std::vector<std::array<int,3>> triangles;
    std::vector<int> original(points.size(),-1);
    std::unordered_map<std::uint64_t,int> intersections;
    const auto vertex=[&](int index) {
        int& mapped=original[static_cast<std::size_t>(index)];
        if (mapped<0) { mapped=static_cast<int>(result.size()); result.push_back(points[index]); }
        return mapped;
    };
    const auto cut=[&](int a,int b) {
        if (std::abs(points[a].x-axis)<1e-12) return vertex(a);
        if (std::abs(points[b].x-axis)<1e-12) return vertex(b);
        const auto lo=static_cast<std::uint32_t>(std::min(a,b)), hi=static_cast<std::uint32_t>(std::max(a,b));
        const std::uint64_t key=(static_cast<std::uint64_t>(lo)<<32)|hi;
        auto it=intersections.find(key); if (it!=intersections.end()) return it->second;
        const double t=(axis-points[a].x)/(points[b].x-points[a].x);
        Vec3 p=points[a]+(points[b]-points[a])*t; p.x=axis;
        const int index=static_cast<int>(result.size()); result.push_back(p); intersections.emplace(key,index);
        return index;
    };
    for (const auto& face:faces) {
        std::array<int,6> polygon{}; int count=0;
        const auto append=[&](int index) { if (!count || polygon[count-1]!=index) polygon[count++]=index; };
        for (int i=0;i<3;++i) {
            const int a=face[i],b=face[(i+1)%3];
            const bool insideA=points[a].x>=axis,insideB=points[b].x>=axis;
            if (insideA) append(vertex(a));
            if (insideA!=insideB) append(cut(a,b));
        }
        if (count>1 && polygon[0]==polygon[count-1]) --count;
        for (int i=1;i+1<count;++i) {
            const int a=polygon[0],b=polygon[i],c=polygon[i+1];
            if (a!=b && b!=c && c!=a && dot(cross(result[b]-result[a],result[c]-result[a]),
                                                cross(result[b]-result[a],result[c]-result[a]))>1e-24)
                triangles.push_back({a,b,c});
        }
    }
    const std::size_t half=result.size(),faceCount=triangles.size();
    std::vector<int> reflected(half);
    for (std::size_t i=0;i<half;++i) {
        if (std::abs(result[i].x-axis)<1e-12) reflected[i]=static_cast<int>(i);
        else { Vec3 p=result[i]; p.x=2*axis-p.x; reflected[i]=static_cast<int>(result.size()); result.push_back(p); }
    }
    for (std::size_t i=0;i<faceCount;++i) {
        const auto f=triangles[i]; triangles.push_back({reflected[f[0]],reflected[f[2]],reflected[f[1]]});
    }
    if (triangles.empty()) return false;
    points=std::move(result); faces=std::move(triangles); return true;
}
} // namespace gtc::native::bilateral
