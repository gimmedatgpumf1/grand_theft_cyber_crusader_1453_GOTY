#pragma once

// Nearest triangle-surface averaging for a reflected axial mesh. Vertex
// correspondence is unknown, so this is a geometric estimate, not an
// anatomical correspondence. Face orientation is protected after averaging.
#include "client/skeleton_body.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace gtc::native::bilateral {
class TriangleSurfaceTree {
    struct Bounds { Vec3 low,high; };
    struct Node { Bounds box; int first=0,count=0,left=-1,right=-1; };
    const std::vector<Vec3>& points_;
    const std::vector<std::array<int,3>>& faces_;
    std::vector<int> order_;
    std::vector<Node> nodes_;
    static double coord(Vec3 v,int a) { return a==0?v.x:a==1?v.y:v.z; }
    static Bounds emptyBox() { return {{1e100,1e100,1e100},{-1e100,-1e100,-1e100}}; }
    static void extend(Bounds& b,Vec3 p) {
        b.low={std::min(b.low.x,p.x),std::min(b.low.y,p.y),std::min(b.low.z,p.z)};
        b.high={std::max(b.high.x,p.x),std::max(b.high.y,p.y),std::max(b.high.z,p.z)};
    }
    Vec3 centroid(int face) const {
        const auto& f=faces_[static_cast<std::size_t>(face)];
        return (points_[f[0]]+points_[f[1]]+points_[f[2]])/3;
    }
    int build(int first,int end) {
        Bounds box=emptyBox(), centers=emptyBox();
        for (int i=first;i<end;++i) {
            const auto& f=faces_[static_cast<std::size_t>(order_[i])];
            for (int j:f) extend(box,points_[j]);
            extend(centers,centroid(order_[i]));
        }
        const int id=static_cast<int>(nodes_.size());
        nodes_.push_back({box,first,end-first,-1,-1});
        if (end-first<=8) return id;
        const Vec3 span=centers.high-centers.low;
        const int axis=span.x>=span.y && span.x>=span.z ? 0 : span.y>=span.z ? 1 : 2;
        const int middle=first+(end-first)/2;
        std::nth_element(order_.begin()+first,order_.begin()+middle,order_.begin()+end,
            [&](int a,int b) {
                const double x=coord(centroid(a),axis), y=coord(centroid(b),axis);
                return x==y ? a<b : x<y;
            });
        const int left=build(first,middle),right=build(middle,end);
        nodes_[id].left=left; nodes_[id].right=right;
        return id;
    }
    static double boxDistance(const Bounds& b,Vec3 p) {
        const auto gap=[](double x,double lo,double hi) { return std::max({lo-x,0.0,x-hi}); };
        const double x=gap(p.x,b.low.x,b.high.x), y=gap(p.y,b.low.y,b.high.y), z=gap(p.z,b.low.z,b.high.z);
        return x*x+y*y+z*z;
    }
    static Vec3 segment(Vec3 p,Vec3 a,Vec3 b) {
        const Vec3 d=b-a; const double n=dot(d,d);
        return n>1e-30 ? a+d*std::clamp(dot(p-a,d)/n,0.0,1.0) : a;
    }
    // Closest point on a triangle, including edges and vertices. Degenerate
    // source faces are treated as their three line segments.
    static Vec3 triangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
        const Vec3 ab=b-a,ac=c-a;
        if (dot(cross(ab,ac),cross(ab,ac))<1e-30) {
            Vec3 best=segment(p,a,b); double dist=dot(best-p,best-p);
            for (Vec3 q:{segment(p,b,c),segment(p,c,a)}) {
                const double d=dot(q-p,q-p); if (d<dist) { best=q; dist=d; }
            }
            return best;
        }
        const Vec3 ap=p-a; const double d1=dot(ab,ap),d2=dot(ac,ap);
        if (d1<=0 && d2<=0) return a;
        const Vec3 bp=p-b; const double d3=dot(ab,bp),d4=dot(ac,bp);
        if (d3>=0 && d4<=d3) return b;
        const double vc=d1*d4-d3*d2;
        if (vc<=0 && d1>=0 && d3<=0) return a+ab*(d1/(d1-d3));
        const Vec3 cp=p-c; const double d5=dot(ab,cp),d6=dot(ac,cp);
        if (d6>=0 && d5<=d6) return c;
        const double vb=d5*d2-d1*d6;
        if (vb<=0 && d2>=0 && d6<=0) return a+ac*(d2/(d2-d6));
        const double va=d3*d6-d5*d4;
        if (va<=0 && d4-d3>=0 && d5-d6>=0)
            return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
        const double inverse=1.0/(va+vb+vc);
        return a+ab*(vb*inverse)+ac*(vc*inverse);
    }
    void search(int id,Vec3 p,Vec3& best,double& distance) const {
        const Node& n=nodes_[id];
        if (boxDistance(n.box,p)>distance) return;
        if (n.left<0) {
            for (int i=n.first;i<n.first+n.count;++i) {
                const auto& f=faces_[static_cast<std::size_t>(order_[i])];
                const Vec3 q=triangle(p,points_[f[0]],points_[f[1]],points_[f[2]]);
                const double d=dot(p-q,p-q); if (d<distance) { distance=d; best=q; }
            }
            return;
        }
        const double left=boxDistance(nodes_[n.left].box,p),right=boxDistance(nodes_[n.right].box,p);
        if (left<=right) { search(n.left,p,best,distance); search(n.right,p,best,distance); }
        else { search(n.right,p,best,distance); search(n.left,p,best,distance); }
    }
public:
    TriangleSurfaceTree(const std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces)
        : points_(points),faces_(faces),order_(faces.size()) {
        std::iota(order_.begin(),order_.end(),0); nodes_.reserve(faces.size()*2);
        if (!faces.empty()) build(0,static_cast<int>(faces.size()));
    }
    Vec3 nearest(Vec3 p) const {
        Vec3 result{}; double distance=std::numeric_limits<double>::infinity();
        if (!nodes_.empty()) search(0,p,result,distance);
        return result;
    }
};

inline bool surfaceAverage(std::vector<Vec3>& points,const std::vector<std::array<int,3>>& faces) {
    if (points.empty() || faces.empty()) return false;
    for (const auto& f:faces) for (int i:f)
        if (i<0 || static_cast<std::size_t>(i)>=points.size()) return false;
    std::vector<Vec3> reflected=points;
    for (Vec3& p:reflected) p.x=-p.x;
    const TriangleSurfaceTree surface(reflected,faces);
    std::vector<Vec3> delta; delta.reserve(points.size());
    for (Vec3 p:points) delta.push_back((surface.nearest(p)-p)*0.5);
    // A nearest surface point is not guaranteed to be an anatomical landmark.
    // Limit only displacements that would reverse a face; otherwise retain the
    // exact half-way point, without smoothing away the other side's details.
    bool valid=false;
    for (int pass=0;pass<32 && !valid;++pass) {
        valid=true; std::vector<bool> bad(points.size());
        for (const auto& f:faces) {
            const Vec3 n=cross(points[f[1]]-points[f[0]],points[f[2]]-points[f[0]]);
            if (dot(n,n)<1e-24) continue;
            const Vec3 a=points[f[0]]+delta[f[0]],b=points[f[1]]+delta[f[1]],c=points[f[2]]+delta[f[2]];
            if (dot(n,cross(b-a,c-a))<=0) {
                valid=false; for (int i:f) bad[static_cast<std::size_t>(i)]=true;
            }
        }
        if (!valid) for (std::size_t i=0;i<delta.size();++i) if (bad[i]) delta[i]=delta[i]*0.5;
    }
    if (!valid) return false;
    for (std::size_t i=0;i<points.size();++i) points[i]+=delta[i];
    return true;
}
} // namespace gtc::native::bilateral
