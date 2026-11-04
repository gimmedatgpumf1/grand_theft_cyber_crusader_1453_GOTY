#pragma once

#include "client/skeleton_metatarsal_registration.h"
#include <algorithm>
#include <numeric>
#include <vector>

namespace gtc::native::foot_joint_clearance {
struct Box {
    Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
    void add(Vec3 p) {
        lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);
    }
};
inline bool intersects(const Box& a,const Box& b,Vec3 shift) {
    constexpr double e=1e-11;
    return a.lo.x<=b.hi.x+shift.x+e && a.hi.x+e>=b.lo.x+shift.x &&
           a.lo.y<=b.hi.y+shift.y+e && a.hi.y+e>=b.lo.y+shift.y &&
           a.lo.z<=b.hi.z+shift.z+e && a.hi.z+e>=b.lo.z+shift.z;
}
inline double component(Vec3 p,int axis) {return axis==0?p.x:axis==1?p.y:p.z;}
inline double cross2(Vec3 a,Vec3 b,Vec3 c,int axis) {
    const int u=(axis+1)%3,v=(axis+2)%3;
    return (component(b,u)-component(a,u))*(component(c,v)-component(a,v))-
           (component(b,v)-component(a,v))*(component(c,u)-component(a,u));
}
inline bool segmentTriangle(Vec3 p,Vec3 q,Vec3 a,Vec3 b,Vec3 c) {
    const Vec3 d=q-p,e1=b-a,e2=c-a,h=cross(d,e2);
    const double det=dot(e1,h);
    if (std::abs(det)<1e-18) return false;
    const double inv=1/det;const Vec3 s=p-a;const double u=dot(s,h)*inv;
    if (u<-1e-9 || u>1+1e-9) return false;
    const Vec3 r=cross(s,e1);const double v=dot(d,r)*inv;
    if (v<-1e-9 || u+v>1+1e-9) return false;
    const double t=dot(e2,r)*inv;
    return t>=-1e-9 && t<=1+1e-9;
}
inline bool triangleOverlap(const std::array<Vec3,3>& a,const std::array<Vec3,3>& b) {
    for (int i=0;i<3;++i) {
        if (segmentTriangle(a[i],a[(i+1)%3],b[0],b[1],b[2]) ||
            segmentTriangle(b[i],b[(i+1)%3],a[0],a[1],a[2])) return true;
    }
    // Handle nearly coplanar surface facets by projecting onto their largest
    // coordinate plane. Degenerate triangles do not contribute to a joint.
    const Vec3 n=cross(a[1]-a[0],a[2]-a[0]);const double nl=length(n);
    if (nl<1e-14) return false;
    for (Vec3 p:b) if (std::abs(dot(n,p-a[0]))>nl*1e-10) return false;
    const int axis=std::abs(n.x)>std::abs(n.y) ? (std::abs(n.x)>std::abs(n.z)?0:2)
                                                  : (std::abs(n.y)>std::abs(n.z)?1:2);
    const auto inside=[&](Vec3 p,const std::array<Vec3,3>& t) {
        const double x=cross2(t[0],t[1],p,axis),y=cross2(t[1],t[2],p,axis),z=cross2(t[2],t[0],p,axis);
        return (x>=-1e-15 && y>=-1e-15 && z>=-1e-15) || (x<=1e-15 && y<=1e-15 && z<=1e-15);
    };
    if (inside(a[0],b) || inside(b[0],a)) return true;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        const Vec3 p=a[i],q=a[(i+1)%3],r=b[j],s=b[(j+1)%3];
        const int u=(axis+1)%3,v=(axis+2)%3;
        if (std::max(component(p,u),component(q,u))+1e-12<std::min(component(r,u),component(s,u)) ||
            std::max(component(r,u),component(s,u))+1e-12<std::min(component(p,u),component(q,u)) ||
            std::max(component(p,v),component(q,v))+1e-12<std::min(component(r,v),component(s,v)) ||
            std::max(component(r,v),component(s,v))+1e-12<std::min(component(p,v),component(q,v))) continue;
        if (cross2(p,q,r,axis)*cross2(p,q,s,axis)<=0 &&
            cross2(r,s,p,axis)*cross2(r,s,q,axis)<=0) return true;
    }
    return false;
}
struct Triangle { std::array<Vec3,3> p; Box box; Vec3 center{}; };
struct Node { Box box; int left=-1,right=-1,first=0,count=0; };
struct Tree {
    std::vector<Triangle> faces;
    std::vector<int> order;
    std::vector<Node> nodes;
    template<class Part> explicit Tree(const Part& p) {
        for (auto f:p.faces) {
            Triangle t;
            for(int i=0;i<3;++i) {t.p[i]=p.points[std::size_t(f[i])];t.box.add(t.p[i]);t.center+=t.p[i]/3;}
            if(length(cross(t.p[1]-t.p[0],t.p[2]-t.p[0]))>1e-14) faces.push_back(t);
        }
        order.resize(faces.size());std::iota(order.begin(),order.end(),0);
        if(!order.empty()) build(0,static_cast<int>(order.size()));
    }
    int build(int first,int count) {
        const int index=static_cast<int>(nodes.size());nodes.push_back({});
        Box b,centers;
        for(int i=first;i<first+count;++i) {const auto& f=faces[std::size_t(order[std::size_t(i)])];b.add(f.box.lo);b.add(f.box.hi);centers.add(f.center);}
        nodes[std::size_t(index)].box=b;
        if(count<=8) {nodes[std::size_t(index)].first=first;nodes[std::size_t(index)].count=count;return index;}
        const Vec3 size=centers.hi-centers.lo;
        const int axis=size.x>size.y ? (size.x>size.z?0:2) : (size.y>size.z?1:2);
        const int half=count/2;
        std::nth_element(order.begin()+first,order.begin()+first+half,order.begin()+first+count,
                         [&](int a,int b0) {return component(faces[std::size_t(a)].center,axis)<component(faces[std::size_t(b0)].center,axis);});
        const int left=build(first,half),right=build(first+half,count-half);
        nodes[std::size_t(index)].left=left;nodes[std::size_t(index)].right=right;
        return index;
    }
};
inline bool overlap(const Tree& a,int ai,const Tree& b,int bi,Vec3 shift) {
    const Node& x=a.nodes[std::size_t(ai)];const Node& y=b.nodes[std::size_t(bi)];
    if (!intersects(x.box,y.box,shift)) return false;
    if (x.count && y.count) {
        for(int i=0;i<x.count;++i) for(int j=0;j<y.count;++j) {
            const auto& f=a.faces[std::size_t(a.order[std::size_t(x.first+i)])];
            const auto& g=b.faces[std::size_t(b.order[std::size_t(y.first+j)])];
            if (!intersects(f.box,g.box,shift)) continue;
            auto moved=g.p;for(Vec3& p:moved) p+=shift;
            if (triangleOverlap(f.p,moved)) return true;
        }
        return false;
    }
    if (x.count) return overlap(a,ai,b,y.left,shift)||overlap(a,ai,b,y.right,shift);
    return overlap(a,x.left,b,bi,shift)||overlap(a,x.right,b,bi,shift);
}
template<class Part>
bool clear(const std::vector<std::pair<const Part*,const Part*>>& pairs,Vec3 shift) {
    for(auto [a,b]:pairs) if(a && b) {
        const Tree x(*a),y(*b);
        if(!x.nodes.empty() && !y.nodes.empty() && overlap(x,0,y,0,shift)) return false;
    }
    return true;
}
// Search only a small, bounded translation along the anatomical joint-to-joint
// direction. Moving a whole distal branch preserves its internal articulations.
template<class Part>
Vec3 separation(const std::vector<std::pair<const Part*,const Part*>>& pairs,Vec3 direction,double maximum) {
    if (length(direction)<1e-10 || maximum<=0) return {};
    direction=normalized(direction);
    std::vector<std::pair<Tree,Tree>> trees;
    for (auto [a,b]:pairs) if(a && b) trees.emplace_back(Tree(*a),Tree(*b));
    const auto collides=[&](double d) {
        for(const auto& [a,b]:trees) if(!a.nodes.empty() && !b.nodes.empty() && overlap(a,0,b,0,direction*d)) return true;
        return false;
    };
    if(!collides(0)) return {};
    constexpr int steps=20;
    for(int i=1;i<=steps;++i) {
        const double hi=maximum*double(i)/steps;
        if(collides(hi)) continue;
        double lo=maximum*double(i-1)/steps,upper=hi;
        for(int k=0;k<8;++k) {const double mid=(lo+upper)*.5;if(collides(mid)) lo=mid;else upper=mid;}
        return direction*std::min(maximum,upper+maximum*.01);
    }
    return {};
}
// Translate a connected branch as a unit. Check every boundary between it
// and its neighbours, not just the first intersecting joint: clearing the
// cuboid against the navicular must not drive the fourth ray into the third.
// The small symmetric search is only run if a boundary already intersects.
template<class Part>
Vec3 separateGroups(const std::vector<const Part*>& fixed,const std::vector<const Part*>& moving,
                    double maximum,double side) {
    if (maximum<=0) return {};
    std::vector<Tree> a,b;
    for (const Part* p:fixed) if(p) a.emplace_back(*p);
    for (const Part* p:moving) if(p) b.emplace_back(*p);
    const auto collides=[&](Vec3 shift) {
        for (const auto& x:a) for (const auto& y:b)
            if(!x.nodes.empty() && !y.nodes.empty() && overlap(x,0,y,0,shift)) return true;
        return false;
    };
    if(!collides({})) return {};
    std::vector<Vec3> directions;
    for(int x=-2;x<=2;++x) for(int y=-2;y<=2;++y) for(int z=-2;z<=2;++z) {
        if(!x && !y && !z) continue;
        // Discard repeated parallel directions; mirror enumeration by side so
        // otherwise equivalent solutions stay bilaterally symmetric.
        if(std::gcd(std::gcd(std::abs(x),std::abs(y)),std::abs(z))!=1) continue;
        directions.push_back(normalized(Vec3{side*x,double(y),double(z)}));
    }
    constexpr int steps=40;
    for(int i=1;i<=steps;++i) for(Vec3 direction:directions) {
        const Vec3 shift=direction*(maximum*double(i)/steps);
        if(!collides(shift)) return shift;
    }
    return {};
}
} // namespace gtc::native::foot_joint_clearance
